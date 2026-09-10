#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <time.h>
#include <sys/time.h>
#include <sntp.h>

#include "core/Logger.h"
#include "core/Settings.h"
#include "core/State.h"
#include "core/MQTTClient.h"
#include "core/WebhookServer.h"
#include "Version.h"
#include "Blinds.h"
#include "Timer.h"

Blinds *BlindL, *BlindR;
AsyncWebServer *Webserver;
Timer *GeneralTimer;

String Message;

// Cookie-based session store, DeviceIQ-style: every page requires a signed
// in session, always - there is no "no login required" mode anymore (that
// was the old Basic Auth / Security_Method::None world). Login happens via
// index.html -> /api/login, which hands back this cookie.
constexpr uint8_t MAX_SESSIONS = 4;
constexpr uint32_t SESSION_IDLE_TIMEOUT_MS = 1800000UL; // 30 minutes

struct WebSession {
  String Token;
  String Username;
  bool Admin = false;
  unsigned long LastSeenMs = 0;
  bool InUse = false;
};

WebSession Sessions[MAX_SESSIONS];

String GenerateSessionToken() {
  char token[33];
  for(uint8_t i = 0; i < 32; i++) {
    uint8_t nibble = RANDOM_REG32 & 0xF;
    token[i] = nibble < 10 ? ('0' + nibble) : ('a' + (nibble - 10));
  }
  token[32] = '\0';
  return String(token);
}

WebSession* FindSession(const String& token) {
  if(token.length() == 0) return nullptr;

  unsigned long now = millis();
  for(uint8_t i = 0; i < MAX_SESSIONS; i++) {
    if(!Sessions[i].InUse) continue;
    if((now - Sessions[i].LastSeenMs) > SESSION_IDLE_TIMEOUT_MS) { Sessions[i].InUse = false; continue; }
    if(Sessions[i].Token == token) { Sessions[i].LastSeenMs = now; return &Sessions[i]; }
  }
  return nullptr;
}

WebSession* AuthenticatedSession(AsyncWebServerRequest *request) {
  const AsyncWebHeader *cookieHeader = request->getHeader("Cookie");
  if(!cookieHeader) return nullptr;

  String cookies = cookieHeader->value();
  int idx = cookies.indexOf("session=");
  if(idx < 0) return nullptr;

  int start = idx + 8;
  int end = cookies.indexOf(';', start);
  String token = (end < 0) ? cookies.substring(start) : cookies.substring(start, end);
  token.trim();

  return FindSession(token);
}

// Reuses the oldest slot once all MAX_SESSIONS are taken, rather than
// rejecting the login - a handful of concurrent browser sessions is already
// generous for a single-blind device with one or two admins.
WebSession* CreateSession(const String& username, bool admin) {
  int freeIndex = 0;
  for(uint8_t i = 0; i < MAX_SESSIONS; i++) {
    if(!Sessions[i].InUse) { freeIndex = i; break; }
    if(Sessions[i].LastSeenMs < Sessions[freeIndex].LastSeenMs) freeIndex = i;
  }

  Sessions[freeIndex].Token = GenerateSessionToken();
  Sessions[freeIndex].Username = username;
  Sessions[freeIndex].Admin = admin;
  Sessions[freeIndex].LastSeenMs = millis();
  Sessions[freeIndex].InUse = true;

  return &Sessions[freeIndex];
}

void RestartDevice() {
  GeneralTimer->SetTimeout(1000);
  GeneralTimer->Start();
  GeneralTimer->OnTimeout([] { ESP.restart(); });
}

// Only index.html and restarting.html still use server-side templating -
// every other page (dashboard/setup/about) is a static file that fetches
// its own data from /api/*, like DeviceIQ's.
String cgi(const String& var){
  if(var == "PRODUCTFAMILY") { return Version::ProductFamily; }
  if(var == "PRODUCTNAME") { return Version::ProductName; }
  if(var == "VERSION") { return Version::Software::Info(); }

  return "";
}

// Mirrors DeviceIQ's UserManagementMessage in HTTPServer.cpp.
const char* UserManagementMessage(UserReturn result) {
  switch(result) {
    case UserReturn::NoError: return "ok";
    case UserReturn::UserExists: return "a user with that name already exists";
    case UserReturn::UserNotFound: return "user not found";
    case UserReturn::MaxUsersReached: return "maximum number of users reached";
    case UserReturn::NoAdminRemaining: return "at least one admin user must remain";
    case UserReturn::InvalidUsername: return "invalid username (3-32 characters: lowercase letters, digits, '.', '_', '-')";
    case UserReturn::InvalidPassword: return "invalid password (8-64 characters)";
    default: return "unexpected error";
  }
}

void loop() {
  BlindL->Control();
  BlindR->Control();
  GeneralTimer->Control();
  DeviceState.Control();
  MQTTClient.Loop();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting " + String(Version::ProductFamily) + "...");

  if(!LittleFS.begin()) {
    Serial.println("Error while mounting LittleFS. Unable to continue.");
    return;
  }

  GeneralTimer = new Timer();
  Settings.Load();
  DeviceState.Load();

  // Settings.FactoryReset();

  Settings.CheckButtonsFactoryReset();
  Logger.Syslog_Server(Settings.Syslog_Server());
  Logger.Syslog_Port(Settings.Syslog_Port());
  Logger.Hostname(Settings.Network_Hostname());
  Logger.Endpoint(Settings.Log_Endpoint());
  Logger.Level(Settings.Log_Level());

  WiFi.mode(WIFI_STA);
  // hostname() only takes effect reliably once STA mode is already set, and
  // must run before begin() to be included in the DHCP request.
  WiFi.hostname(Settings.Network_Hostname());
  WiFi.setAutoReconnect(Settings.Network_ReconnectEnabled());

  if(!Settings.Network_DHCPClient()) {
    if (!WiFi.config(Settings.Network_IPAddress(), Settings.Network_Gateway(), Settings.Network_Mask(), Settings.Network_DNS1(), Settings.Network_DNS2())) {
      Logger.Write("Warning: Failed to set IP address.", logger::Warning);
    }
  }

  WiFi.begin(Settings.Network_SSID().c_str(), Settings.Network_Password().c_str());

  uint16_t Retry = 0;

  Logger.Write("Connecting to " + Settings.Network_SSID() + " network:");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000); if(Retry > Settings.Network_ConnectionTimeout()) break; else Retry++;
  }

  if(WiFi.status() == WL_CONNECTED) {
    Logger.Write("Connected to SSID: " + Settings.Network_SSID() + ", IP address: " + WiFi.localIP().toString());
    Logger.Write("Web UI address: http://" + WiFi.localIP().toString() + ":" + Settings.Network_HTTP_Port() + "/");

    if(Settings.General_NTPEnabled()) {
      Logger.Write("Synchronizing clock via NTP (" + Settings.General_NTPServer() + ")...");
      configTime(Settings.General_TimeZone() * 3600, 0, Settings.General_NTPServer().c_str());

      uint8_t NTPRetry = 0;
      while(time(nullptr) < 100000 && NTPRetry < 20) { delay(500); NTPRetry++; }

      if(time(nullptr) >= 100000) {
        // Unlike DeviceIQ, the clock is synchronized once here and never
        // again - stopping SNTP keeps it that way instead of the lwIP
        // client quietly re-syncing on its own schedule.
        sntp_stop();
        time_t Now = time(nullptr);
        String NowStr(ctime(&Now));
        NowStr.trim();
        Logger.Write("Clock synchronized: " + NowStr);
      } else {
        Logger.Write("Warning: Unable to synchronize clock via NTP.", logger::Warning);
      }
    }
  } else if(Settings.Network_FallbackAPEnabled()) {
    String AP_SSID = Settings.Network_FallbackAPSSID().length() ? Settings.Network_FallbackAPSSID() : (String(Version::ProductFamily) + "-" + Settings.Network_Hostname());
    Logger.Write("Unable to connect to " + Settings.Network_SSID() + " network. Going AP mode SSID " + AP_SSID + ", password '" + Settings.Network_FallbackAPPassword() + "'");
    WiFi.softAP(AP_SSID.c_str(), Settings.Network_FallbackAPPassword().c_str(), 11, 0, 4);
    Settings.AP_Mode(true);
    Logger.Write("Connected to AP SSID: " + AP_SSID + " | IP address: " + WiFi.softAPIP().toString());
    Logger.Write("Web UI address: http://" + WiFi.softAPIP().toString() + ":" + Settings.Network_HTTP_Port() + "/");
  } else {
    Logger.Write("Unable to connect to " + Settings.Network_SSID() + " network. Fallback AP is disabled.", logger::Warning);
  }

  MDNS.begin(Settings.Network_Hostname());

  // Left Blind
  BlindL = new Blinds(Settings.BlindL_Name(), Settings.BlindL_PinButtonOpen(), Settings.BlindL_PinButtonClose(), Settings.BlindL_PinSwitchOpen(), Settings.BlindL_PinSwitchClose(), BlindSlot::Left);
  BlindL->Step_Ms(Settings.BlindL_StepTime());
  BlindL->ButtonOpenEnabled(Settings.BlindL_ButtonOpen());
  BlindL->ButtonCloseEnabled(Settings.BlindL_ButtonClose());
  BlindL->InvertButtons(Settings.BlindL_InvertButtons());  

  // Right Blind
  BlindR = new Blinds(Settings.BlindR_Name(), Settings.BlindR_PinButtonOpen(), Settings.BlindR_PinButtonClose(), Settings.BlindR_PinSwitchOpen(), Settings.BlindR_PinSwitchClose(), BlindSlot::Right);
  BlindR->Step_Ms(Settings.BlindR_StepTime());
  BlindR->ButtonOpenEnabled(Settings.BlindR_ButtonOpen());
  BlindR->ButtonCloseEnabled(Settings.BlindR_ButtonClose());
  BlindR->InvertButtons(Settings.BlindR_InvertButtons());

  Webserver = new AsyncWebServer(Settings.Network_HTTP_Port());

  Webserver->onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "404: Not found");
    Logger.Write("Web GET " + request->url() + " - Not Found");
  });

  Webserver->on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/style.css", "text/css");
  });

  Webserver->on("/notifications.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/notifications.js", "application/javascript");
  });

  // The login page itself is always reachable without credentials - it is
  // the thing that produces them (via /api/login). It figures out on its
  // own, via /api/session, whether to skip straight to the dashboard.
  Webserver->on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", String(), false, cgi);
    Logger.Write("Web GET " + request->url());
  });

  Webserver->on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", String(), false, cgi);
    Logger.Write("Web GET " + request->url());
  });

  Webserver->on("/restarting.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/restarting.html", String(), false, cgi);
  });

  Webserver->on("/api/session", HTTP_GET, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session) { request->send(200, "application/json", "{\"authenticated\":false}"); return; }

    JsonDocument doc;
    doc["authenticated"] = true;
    doc["username"] = session->Username;
    doc["admin"] = session->Admin;
    doc["hostname"] = Settings.Network_Hostname();
    doc["productFamily"] = Version::ProductFamily;
    doc["productName"] = Version::ProductName;
    doc["softwareVersion"] = Version::Software::Info();
    doc["idleTimeoutMs"] = SESSION_IDLE_TIMEOUT_MS;
    doc["logFileEnabled"] = (Settings.Log_Endpoint() & logger::Endpoints::File) != 0;

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
  });

  Webserver->on("/api/logout", HTTP_POST, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(session) session->InUse = false;

    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"ok\":true}");
    response->addHeader("Set-Cookie", "session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    request->send(response);
  });

  Webserver->on("/api/about", HTTP_GET, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }

    JsonDocument doc;

    JsonObject version = doc["version"].to<JsonObject>();
    version["product"] = Version::ProductName;
    version["family"] = Version::ProductFamily;
    version["serial"] = Version::SerialNumber();
    version["hardware"] = Version::Hardware::Info();
    version["hardwareModel"] = Version::Hardware::Model;
    version["software"] = Version::Software::Info();

    JsonObject hardware = doc["hardware"].to<JsonObject>();
    hardware["chipId"] = String(ESP.getChipId(), HEX);
    hardware["cpuFrequencyMHz"] = ESP.getCpuFreqMHz();
    hardware["coreVersion"] = ESP.getCoreVersion();
    hardware["sdkVersion"] = ESP.getSdkVersion();
    hardware["wifiMac"] = WiFi.macAddress();

    JsonObject flash = doc["flash"].to<JsonObject>();
    flash["chipId"] = String(ESP.getFlashChipId(), HEX);
    flash["sizeBytes"] = ESP.getFlashChipSize();
    flash["realSizeBytes"] = ESP.getFlashChipRealSize();
    flash["speedMHz"] = ESP.getFlashChipSpeed() / 1000000;
    const char* FlashModeNames[] = { "QIO", "QOUT", "DIO", "DOUT" };
    FlashMode_t mode = ESP.getFlashChipMode();
    flash["mode"] = (mode <= FM_DOUT) ? FlashModeNames[mode] : "Unknown";

    JsonObject firmware = doc["firmware"].to<JsonObject>();
    firmware["sketchSizeBytes"] = ESP.getSketchSize();
    firmware["freeSketchSpaceBytes"] = ESP.getFreeSketchSpace();
    firmware["sketchMD5"] = ESP.getSketchMD5();

    JsonObject runtime = doc["runtime"].to<JsonObject>();
    runtime["resetReason"] = ESP.getResetReason();
    runtime["resetInfo"] = ESP.getResetInfo();
    runtime["uptimeSeconds"] = millis() / 1000;

    JsonObject memory = doc["memory"].to<JsonObject>();
    memory["freeHeapBytes"] = ESP.getFreeHeap();
    memory["heapFragmentationPercent"] = ESP.getHeapFragmentation();
    memory["maxFreeBlockSizeBytes"] = ESP.getMaxFreeBlockSize();

    JsonObject filesystem = doc["filesystem"].to<JsonObject>();
    FSInfo fsInfo;
    if(LittleFS.info(fsInfo)) {
      filesystem["mounted"] = true;
      filesystem["totalBytes"] = fsInfo.totalBytes;
      filesystem["usedBytes"] = fsInfo.usedBytes;
      filesystem["availableBytes"] = fsInfo.totalBytes - fsInfo.usedBytes;
      filesystem["usagePercent"] = fsInfo.totalBytes ? (fsInfo.usedBytes * 100 / fsInfo.totalBytes) : 0;
    } else {
      filesystem["mounted"] = false;
    }

    doc["admin"] = session->Admin;

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
  });

  Webserver->on("/api/login", HTTP_POST, [](AsyncWebServerRequest *request){
    String username, password;

    for(uint8_t i = 0; i < (uint8_t)request->args(); i++) {
      const AsyncWebParameter *p = request->getParam(i);
      if(p->name() == "username") username = p->value();
      if(p->name() == "password") password = p->value();
    }

    if(!Settings.Users.Authenticate(username, password)) {
      Logger.Write("Web POST " + request->url() + " - Invalid credentials", logger::Warning);
      request->send(401, "application/json", "{\"error\":\"Invalid username or password.\"}");
      return;
    }

    users::NormalizeUsername(username);
    UserInfo info;
    Settings.Users.Find(username, &info);

    WebSession *session = CreateSession(info.Username, info.Admin);

    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"authenticated\":true}");
    response->addHeader("Set-Cookie", "session=" + session->Token + "; Path=/; HttpOnly; SameSite=Strict");
    request->send(response);

    Logger.Write("Web POST " + request->url() + " - Login (" + info.Username + ")");
  });

  // Every page below is a static file that gates itself client-side via
  // /api/session (DeviceIQ's pattern) - none of them need a server-side
  // check anymore, now that logging in is always required.
  Webserver->on("/dashboard.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/dashboard.html", "text/html");
    Logger.Write("Web GET " + request->url());
  });

  Webserver->on("/setup.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/setup.html", "text/html");
    Logger.Write("Web GET " + request->url());
  });

  Webserver->on("/about.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/about.html", "text/html");
    Logger.Write("Web GET " + request->url());
  });

  Webserver->on("/users.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/users.html", "text/html");
    Logger.Write("Web GET " + request->url());
  });

  Webserver->on("/log.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/log.html", "text/html");
    Logger.Write("Web GET " + request->url());
  });

  Webserver->on("/api/log", HTTP_GET, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session || !session->Admin) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }

    String content;
    if(LittleFS.exists(LOG_FILE_NAME)) {
      File file = LittleFS.open(LOG_FILE_NAME, "r");
      if(file) { content = file.readString(); file.close(); }
    }

    // Mirrors DeviceIQ's own log viewer: walk backward from the end
    // counting newlines to keep only the requested tail of the file.
    String linesParam = request->hasParam("lines") ? request->getParam("lines")->value() : String();
    bool showAll = linesParam == "all";
    uint32_t requestedLines = 500;
    if(linesParam.length() && !showAll) {
      long parsed = linesParam.toInt();
      if(parsed > 0) requestedLines = (uint32_t)parsed;
    }

    size_t start = 0;
    if(!showAll) {
      size_t cursor = content.length();
      while(cursor > 0 && (content[cursor - 1] == '\n' || content[cursor - 1] == '\r')) cursor--;
      start = cursor;
      uint32_t linesFound = 0;
      while(start > 0) {
        start--;
        if(content[start] != '\n') continue;
        linesFound++;
        if(linesFound == requestedLines) { start++; break; }
      }
    }

    request->send(200, "text/plain", content.substring(start));
  });

  Webserver->on("/api/log/export", HTTP_GET, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session || !session->Admin) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }

    AsyncWebServerResponse *response = LittleFS.exists(LOG_FILE_NAME)
      ? request->beginResponse(LittleFS, LOG_FILE_NAME, "text/plain")
      : request->beginResponse(200, "text/plain", "");
    response->addHeader("Content-Disposition", "attachment; filename=\"device.log\"");
    request->send(response);
    Logger.Write("Web GET " + request->url() + " - Log exported (" + session->Username + ")");
  });

  Webserver->on("/api/log/clear", HTTP_POST, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session || !session->Admin) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }

    bool success = !LittleFS.exists(LOG_FILE_NAME) || LittleFS.remove(LOG_FILE_NAME);

    if(!success) {
      request->send(500, "application/json", "{\"error\":\"Unable to clear the log file.\"}");
      Logger.Write("Web POST " + request->url() + " - Log clear rejected (" + session->Username + ")", logger::Warning);
      return;
    }

    request->send(200, "application/json", "{\"success\":true}");
    // Logged after clearing: if File is one of the active log endpoints,
    // this becomes the first entry of the fresh log.
    Logger.Write("Web POST " + request->url() + " - Log clear accepted (" + session->Username + ")");
  });

  Webserver->on("/api/blinds", HTTP_GET, [](AsyncWebServerRequest *request) {
    WebSession *session = AuthenticatedSession(request);
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }

    if(request->args() == 0) { // Reading Info
      JsonDocument doc;
      doc["leftposition"] = BlindL->Position();
      doc["leftsteptime"] = BlindL->Step_Ms();
      doc["leftstate"] = BlindStateName(BlindL->State());
      doc["leftname"] = BlindL->Name;
      doc["rightposition"] = BlindR->Position();
      doc["rightsteptime"] = BlindR->Step_Ms();
      doc["rightstate"] = BlindStateName(BlindR->State());
      doc["rightname"] = BlindR->Name;

      AsyncResponseStream *response = request->beginResponseStream("application/json");
      serializeJson(doc, *response);
      request->send(response);
      Logger.Write("Web GET " + request->url());
    } else { // Setting Info
      for(uint8_t i = 0; i < (uint8_t)request->args(); i++) {
        String Arg = request->argName(i);
        String Val = request->arg(i);

        Arg.toUpperCase();
        
        if(Arg == "LEFTPOSITION") {
          if(Val == "?") {
            request->send(200, "application/json", "{\"position\":" + String(BlindL->Position()) + "}");
            Logger.Write("Web GET " + request->url());
          } else {
            request->send(200, "application/json", "{\"position\":" + Val + "}");
            Logger.Write("Web SET " + request->url() + " - Left-Position: " + Val);

            volatile uint8_t tmpPos = constrain(Val.toInt(), 0, DEF_Max_Position);
            BlindL->Position(tmpPos);
          }
        }

        if(Arg == "LPOS") {
          if(Val == "?") {
            request->send(200, "text/plain", String(BlindL->Position()));
            //Logger.Write("Web GET " + request->url());
          } else {
            request->send(204);
            Logger.Write("Web SET " + request->url() + " - Left-Position: " + Val);

            volatile uint8_t tmpPos = constrain(Val.toInt(), 0, DEF_Max_Position);
            BlindL->Position(tmpPos);
          }
        }

        if(Arg == "RIGHTPOSITION") {
          if(Val == "?") {
            request->send(200, "application/json", "{\"position\":" + String(BlindR->Position()) + "}");
            Logger.Write("Web GET " + request->url());
          } else {
            request->send(200, "application/json", "{\"position\":" + Val + "}");
            Logger.Write("Web SET " + request->url() + " - Right-Position: " + Val);

            volatile uint8_t tmpPos = constrain(Val.toInt(), 0, DEF_Max_Position);
            BlindR->Position(tmpPos);
          }
        }

        if(Arg == "RPOS") {
          if(Val == "?") {
            request->send(200, "text/plain", String(BlindR->Position()));
            //Logger.Write("Web GET " + request->url());
          } else {
            request->send(204);
            Logger.Write("Web SET " + request->url() + " - Right-Position: " + Val);

            volatile uint8_t tmpPos = constrain(Val.toInt(), 0, DEF_Max_Position);
            BlindR->Position(tmpPos);
          }
        }

        if(Arg == "LEFTSTEPTIME") {
          if(Val == "?") {
            request->send(200, "application/json", "{\"steptime\":\"" + String(BlindL->Step_Ms()) + "\"}");
            Logger.Write("Web GET " + request->url());
          } else {
            request->send(200, "application/json", "{\"steptime\":" + Val + "}");
            Logger.Write("Web SET " + request->url() + " - Left-StepTime: " + Val);

            volatile uint16_t tmpPos = constrain(Val.toInt(), 1, 65535);
            BlindL->Step_Ms(tmpPos);
          }
        }

        if(Arg == "RIGHTSTEPTIME") {
          if(Val == "?") {
            request->send(200, "application/json", "{\"steptime\":" + String(BlindR->Step_Ms()) + "}");
            Logger.Write("Web GET " + request->url());
          } else {
            request->send(200, "application/json", "{\"steptime\":" + Val + "}");
            Logger.Write("Web SET " + request->url() + " - Right-StepTime: " + Val);

            volatile uint16_t tmpPos = constrain(Val.toInt(), 1, 65535);
            BlindR->Step_Ms(tmpPos);
          }
        }

        if(Arg == "LSTT") {
            request->send(200, "text/plain", String(BlindL->State()));
            //Logger.Write("Web GET " + request->url());
        }

        if(Arg == "RSTT") {
            request->send(200, "text/plain", String(BlindR->State()));
            //Logger.Write("Web GET " + request->url());
        }

        // Open/close/stop, as used by the dashboard's card buttons - a
        // motion command rather than a bare target percentage.
        if(Arg == "LEFTSTATE") {
          if(Val == "open") BlindL->Open(); else if(Val == "close") BlindL->Close(); else if(Val == "stop") BlindL->Stop();
          request->send(200, "application/json", "{\"state\":\"" + String(BlindStateName(BlindL->State())) + "\"}");
          Logger.Write("Web SET " + request->url() + " - Left-State: " + Val);
        }

        if(Arg == "RIGHTSTATE") {
          if(Val == "open") BlindR->Open(); else if(Val == "close") BlindR->Close(); else if(Val == "stop") BlindR->Stop();
          request->send(200, "application/json", "{\"state\":\"" + String(BlindStateName(BlindR->State())) + "\"}");
          Logger.Write("Web SET " + request->url() + " - Right-State: " + Val);
        }

      }
    }
  });

  // Everything below is session/admin gated (like about.html), replacing
  // the old Basic-Auth /changepassword and /settings endpoints that the
  // previous setup.html used.

  // User management is admin-only, same as DeviceIQ (there is no
  // self-service "change my own password" - an admin manages every account,
  // including their own, from users.html). A save failure after an
  // already-applied mutation is reported as a warning rather than a
  // failure, since there is no way to roll back a removed password hash.
  Webserver->on("/api/users", HTTP_GET, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session || !session->Admin) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }

    JsonDocument doc;
    JsonArray users = doc["users"].to<JsonArray>();
    Settings.Users.ForEachStored([&users](const String& username, bool admin, const uint8_t*, const uint8_t*) {
      JsonObject entry = users.add<JsonObject>();
      entry["username"] = username;
      entry["admin"] = admin;
    });

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
  });

  Webserver->on("/api/users/add", HTTP_POST, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session || !session->Admin) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }

    String Username, Password;
    bool Admin = false;
    for(uint8_t i = 0; i < (uint8_t)request->args(); i++) {
      const AsyncWebParameter *p = request->getParam(i);
      if(p->name() == "username") Username = p->value();
      if(p->name() == "password") Password = p->value();
      if(p->name() == "admin") Admin = (p->value() == "true");
    }

    UserReturn result = Settings.Users.Add(Username, Password, Admin);
    if(result != UserReturn::NoError) {
      request->send(422, "application/json", "{\"error\":\"" + String(UserManagementMessage(result)) + "\"}");
      return;
    }

    bool saved = Settings.Save();
    Logger.Write("Web POST " + request->url() + " - User add " + String(saved ? "" : "(not saved) ") + "by " + session->Username + ": " + Username);
    request->send(200, "application/json", saved ? "{\"success\":true}" : "{\"success\":true,\"warning\":\"could not save to disk; this change will be lost on restart\"}");
  });

  Webserver->on("/api/users/remove", HTTP_POST, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session || !session->Admin) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }

    String Username;
    for(uint8_t i = 0; i < (uint8_t)request->args(); i++) {
      const AsyncWebParameter *p = request->getParam(i);
      if(p->name() == "username") Username = p->value();
    }

    UserReturn result = Settings.Users.Remove(Username);
    if(result != UserReturn::NoError) {
      request->send(422, "application/json", "{\"error\":\"" + String(UserManagementMessage(result)) + "\"}");
      return;
    }

    bool saved = Settings.Save();
    Logger.Write("Web POST " + request->url() + " - User remove " + String(saved ? "" : "(not saved) ") + "by " + session->Username + ": " + Username);
    request->send(200, "application/json", saved ? "{\"success\":true}" : "{\"success\":true,\"warning\":\"could not save to disk; this change will be lost on restart\"}");
  });

  Webserver->on("/api/users/rename", HTTP_POST, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session || !session->Admin) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }

    String Username, NewUsername;
    for(uint8_t i = 0; i < (uint8_t)request->args(); i++) {
      const AsyncWebParameter *p = request->getParam(i);
      if(p->name() == "username") Username = p->value();
      if(p->name() == "newUsername") NewUsername = p->value();
    }

    UserReturn result = Settings.Users.Rename(Username, NewUsername);
    if(result != UserReturn::NoError) {
      request->send(422, "application/json", "{\"error\":\"" + String(UserManagementMessage(result)) + "\"}");
      return;
    }

    bool saved = Settings.Save();
    Logger.Write("Web POST " + request->url() + " - User rename " + String(saved ? "" : "(not saved) ") + "by " + session->Username + ": " + Username + " -> " + NewUsername);
    request->send(200, "application/json", saved ? "{\"success\":true}" : "{\"success\":true,\"warning\":\"could not save to disk; this change will be lost on restart\"}");
  });

  Webserver->on("/api/users/set-admin", HTTP_POST, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session || !session->Admin) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }

    String Username;
    bool Admin = false;
    for(uint8_t i = 0; i < (uint8_t)request->args(); i++) {
      const AsyncWebParameter *p = request->getParam(i);
      if(p->name() == "username") Username = p->value();
      if(p->name() == "admin") Admin = (p->value() == "true");
    }

    UserReturn result = Settings.Users.SetAdmin(Username, Admin);
    if(result != UserReturn::NoError) {
      request->send(422, "application/json", "{\"error\":\"" + String(UserManagementMessage(result)) + "\"}");
      return;
    }

    bool saved = Settings.Save();
    Logger.Write("Web POST " + request->url() + " - User set-admin " + String(saved ? "" : "(not saved) ") + "by " + session->Username + ": " + Username + "=" + (Admin ? "true" : "false"));
    request->send(200, "application/json", saved ? "{\"success\":true}" : "{\"success\":true,\"warning\":\"could not save to disk; this change will be lost on restart\"}");
  });

  Webserver->on("/api/users/set-password", HTTP_POST, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session || !session->Admin) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }

    String Username, Password;
    for(uint8_t i = 0; i < (uint8_t)request->args(); i++) {
      const AsyncWebParameter *p = request->getParam(i);
      if(p->name() == "username") Username = p->value();
      if(p->name() == "password") Password = p->value();
    }

    UserReturn result = Settings.Users.SetPassword(Username, Password);
    if(result != UserReturn::NoError) {
      request->send(422, "application/json", "{\"error\":\"" + String(UserManagementMessage(result)) + "\"}");
      return;
    }

    bool saved = Settings.Save();
    Logger.Write("Web POST " + request->url() + " - User set-password " + String(saved ? "" : "(not saved) ") + "by " + session->Username + ": " + Username);
    request->send(200, "application/json", saved ? "{\"success\":true}" : "{\"success\":true,\"warning\":\"could not save to disk; this change will be lost on restart\"}");
  });

  Webserver->on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session || !session->Admin) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }

    JsonDocument doc;

    JsonObject network = doc["Network"].to<JsonObject>();
    network["DHCP Client"] = Settings.Network_DHCPClient();
    network["Hostname"] = Settings.Network_Hostname();
    network["IP Address"] = Settings.Network_IPAddress().toString();
    network["Gateway"] = Settings.Network_Gateway().toString();
    network["Netmask"] = Settings.Network_Mask().toString();
    network["Current IP"] = WiFi.localIP().toString();
    network["Current Gateway"] = WiFi.gatewayIP().toString();
    network["Current Netmask"] = WiFi.subnetMask().toString();
    network["DNS 1"] = Settings.Network_DNS1().toString();
    network["DNS 2"] = Settings.Network_DNS2().toString();
    network["SSID"] = Settings.Network_SSID();
    network["Passphrase"] = Settings.Network_Password();
    network["Connection Timeout"] = Settings.Network_ConnectionTimeout();
    network["Reconnect Enabled"] = Settings.Network_ReconnectEnabled();
    network["Reconnect Initial Interval"] = Settings.Network_ReconnectInitialInterval();
    network["Reconnect Maximum Interval"] = Settings.Network_ReconnectMaximumInterval();
    network["Fallback AP Enabled"] = Settings.Network_FallbackAPEnabled();
    network["Fallback AP SSID"] = Settings.Network_FallbackAPSSID();
    network["Fallback AP Password"] = Settings.Network_FallbackAPPassword();
    network["Fallback AP Retention"] = Settings.Network_FallbackAPRetention();
    network["HTTP Port"] = Settings.Network_HTTP_Port();

    JsonObject blinds = doc["Blinds"].to<JsonObject>();
    blinds["Left Name"] = Settings.BlindL_Name();
    blinds["Left Step Time"] = Settings.BlindL_StepTime();
    blinds["Left Button Open"] = Settings.BlindL_ButtonOpen();
    blinds["Left Button Close"] = Settings.BlindL_ButtonClose();
    blinds["Left Invert Buttons"] = Settings.BlindL_InvertButtons();
    blinds["Left Button Open Pin"] = Settings.BlindL_PinButtonOpen();
    blinds["Left Button Close Pin"] = Settings.BlindL_PinButtonClose();
    blinds["Left Switch Open Pin"] = Settings.BlindL_PinSwitchOpen();
    blinds["Left Switch Close Pin"] = Settings.BlindL_PinSwitchClose();
    blinds["Right Name"] = Settings.BlindR_Name();
    blinds["Right Step Time"] = Settings.BlindR_StepTime();
    blinds["Right Button Open"] = Settings.BlindR_ButtonOpen();
    blinds["Right Button Close"] = Settings.BlindR_ButtonClose();
    blinds["Right Invert Buttons"] = Settings.BlindR_InvertButtons();
    blinds["Right Button Open Pin"] = Settings.BlindR_PinButtonOpen();
    blinds["Right Button Close Pin"] = Settings.BlindR_PinButtonClose();
    blinds["Right Switch Open Pin"] = Settings.BlindR_PinSwitchOpen();
    blinds["Right Switch Close Pin"] = Settings.BlindR_PinSwitchClose();

    JsonObject log = doc["Log"].to<JsonObject>();
    log["Endpoint"] = Settings.Log_Endpoint();
    log["Level"] = Settings.Log_Level();
    log["Syslog Server"] = Settings.Syslog_Server();
    log["Syslog Port"] = Settings.Syslog_Port();

    JsonObject general = doc["General"].to<JsonObject>();
    general["NTP Enabled"] = Settings.General_NTPEnabled();
    general["NTP Server"] = Settings.General_NTPServer();
    general["Time Zone"] = Settings.General_TimeZone();

    JsonObject mqtt = doc["MQTT"].to<JsonObject>();
    mqtt["Enabled"] = Settings.MQTT_Enabled();
    mqtt["Broker"] = Settings.MQTT_Broker();
    mqtt["Port"] = Settings.MQTT_Port();
    mqtt["User"] = Settings.MQTT_User();
    mqtt["Password"] = Settings.MQTT_Password();
    mqtt["Discovery Enabled"] = Settings.MQTT_DiscoveryEnabled();
    mqtt["Discovery Prefix"] = Settings.MQTT_DiscoveryPrefix();

    JsonObject webhooks = doc["Webhooks"].to<JsonObject>();
    webhooks["Enabled"] = Settings.Webhooks_Enabled();
    webhooks["Token"] = Settings.Webhooks_Token();
    webhooks["Port"] = Settings.Webhooks_Port();

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
  });

  Webserver->on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session || !session->Admin) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }

    String section;
    for(uint8_t i = 0; i < (uint8_t)request->args(); i++) {
      if(request->getParam(i)->name() == "section") section = request->getParam(i)->value();
    }

    bool restart = false;

    if(section == "Network") {
      String Hostname(Settings.Network_Hostname()), SSID(Settings.Network_SSID()), Passphrase(Settings.Network_Password());
      String FallbackAPSSID(Settings.Network_FallbackAPSSID()), FallbackAPPassword(Settings.Network_FallbackAPPassword());
      bool DHCPClient(Settings.Network_DHCPClient()), ReconnectEnabled(Settings.Network_ReconnectEnabled()), FallbackAPEnabled(Settings.Network_FallbackAPEnabled());
      IPAddress IPAddr(Settings.Network_IPAddress()), Gateway(Settings.Network_Gateway()), Netmask(Settings.Network_Mask()), DNS1(Settings.Network_DNS1()), DNS2(Settings.Network_DNS2());
      uint16_t HTTPPort(Settings.Network_HTTP_Port()), ConnTimeout(Settings.Network_ConnectionTimeout()), ReconnInit(Settings.Network_ReconnectInitialInterval()), ReconnMax(Settings.Network_ReconnectMaximumInterval()), FallbackRetention(Settings.Network_FallbackAPRetention());

      for(uint8_t i = 0; i < (uint8_t)request->args(); i++) {
        const AsyncWebParameter *p = request->getParam(i);
        String n = p->name(), v = p->value();
        if(n == "Hostname") Hostname = v;
        if(n == "SSID") SSID = v;
        if(n == "Passphrase") Passphrase = v;
        if(n == "DHCP Client") DHCPClient = (v == "true");
        if(n == "IP Address") IPAddr.fromString(v);
        if(n == "Gateway") Gateway.fromString(v);
        if(n == "Netmask") Netmask.fromString(v);
        if(n == "DNS 1") DNS1.fromString(v);
        if(n == "DNS 2") DNS2.fromString(v);
        if(n == "HTTP Port") HTTPPort = v.toInt();
        if(n == "Connection Timeout") ConnTimeout = v.toInt();
        if(n == "Reconnect Enabled") ReconnectEnabled = (v == "true");
        if(n == "Reconnect Initial Interval") ReconnInit = v.toInt();
        if(n == "Reconnect Maximum Interval") ReconnMax = v.toInt();
        if(n == "Fallback AP Enabled") FallbackAPEnabled = (v == "true");
        if(n == "Fallback AP SSID") FallbackAPSSID = v;
        if(n == "Fallback AP Password") FallbackAPPassword = v;
        if(n == "Fallback AP Retention") FallbackRetention = v.toInt();
      }

      Settings.Network_Hostname(Hostname);
      Settings.Network_SSID(SSID);
      Settings.Network_Password(Passphrase);
      Settings.Network_DHCPClient(DHCPClient);
      Settings.Network_IPAddress(IPAddr);
      Settings.Network_Gateway(Gateway);
      Settings.Network_Mask(Netmask);
      Settings.Network_DNS1(DNS1);
      Settings.Network_DNS2(DNS2);
      Settings.Network_HTTP_Port(HTTPPort);
      Settings.Network_ConnectionTimeout(ConnTimeout);
      Settings.Network_ReconnectEnabled(ReconnectEnabled);
      Settings.Network_ReconnectInitialInterval(ReconnInit);
      Settings.Network_ReconnectMaximumInterval(ReconnMax);
      Settings.Network_FallbackAPEnabled(FallbackAPEnabled);
      Settings.Network_FallbackAPSSID(FallbackAPSSID);
      Settings.Network_FallbackAPPassword(FallbackAPPassword);
      Settings.Network_FallbackAPRetention(FallbackRetention);
      Settings.Save();
      restart = true;
    } else if(section == "Blinds") {
      String LeftName(Settings.BlindL_Name()), RightName(Settings.BlindR_Name());
      uint16_t LeftStepTime(Settings.BlindL_StepTime()), RightStepTime(Settings.BlindR_StepTime());
      bool LeftButtonOpen(Settings.BlindL_ButtonOpen()), LeftButtonClose(Settings.BlindL_ButtonClose()), LeftInvert(Settings.BlindL_InvertButtons());
      bool RightButtonOpen(Settings.BlindR_ButtonOpen()), RightButtonClose(Settings.BlindR_ButtonClose()), RightInvert(Settings.BlindR_InvertButtons());
      uint8_t LeftPinButtonOpen(Settings.BlindL_PinButtonOpen()), LeftPinButtonClose(Settings.BlindL_PinButtonClose());
      uint8_t LeftPinSwitchOpen(Settings.BlindL_PinSwitchOpen()), LeftPinSwitchClose(Settings.BlindL_PinSwitchClose());
      uint8_t RightPinButtonOpen(Settings.BlindR_PinButtonOpen()), RightPinButtonClose(Settings.BlindR_PinButtonClose());
      uint8_t RightPinSwitchOpen(Settings.BlindR_PinSwitchOpen()), RightPinSwitchClose(Settings.BlindR_PinSwitchClose());

      for(uint8_t i = 0; i < (uint8_t)request->args(); i++) {
        const AsyncWebParameter *p = request->getParam(i);
        String n = p->name(), v = p->value();
        if(n == "Left Name") LeftName = v;
        if(n == "Left Step Time") LeftStepTime = v.toInt();
        if(n == "Left Button Open") LeftButtonOpen = (v == "true");
        if(n == "Left Button Close") LeftButtonClose = (v == "true");
        if(n == "Left Invert Buttons") LeftInvert = (v == "true");
        if(n == "Left Button Open Pin") LeftPinButtonOpen = v.toInt();
        if(n == "Left Button Close Pin") LeftPinButtonClose = v.toInt();
        if(n == "Left Switch Open Pin") LeftPinSwitchOpen = v.toInt();
        if(n == "Left Switch Close Pin") LeftPinSwitchClose = v.toInt();
        if(n == "Right Name") RightName = v;
        if(n == "Right Step Time") RightStepTime = v.toInt();
        if(n == "Right Button Open") RightButtonOpen = (v == "true");
        if(n == "Right Button Close") RightButtonClose = (v == "true");
        if(n == "Right Invert Buttons") RightInvert = (v == "true");
        if(n == "Right Button Open Pin") RightPinButtonOpen = v.toInt();
        if(n == "Right Button Close Pin") RightPinButtonClose = v.toInt();
        if(n == "Right Switch Open Pin") RightPinSwitchOpen = v.toInt();
        if(n == "Right Switch Close Pin") RightPinSwitchClose = v.toInt();
      }

      Settings.BlindL_Name(LeftName); BlindL->Name = Settings.BlindL_Name();
      Settings.BlindL_StepTime(LeftStepTime); BlindL->Step_Ms(LeftStepTime);
      Settings.BlindL_ButtonOpen(LeftButtonOpen); BlindL->ButtonOpenEnabled(LeftButtonOpen);
      Settings.BlindL_ButtonClose(LeftButtonClose); BlindL->ButtonCloseEnabled(LeftButtonClose);
      Settings.BlindL_InvertButtons(LeftInvert); BlindL->InvertButtons(LeftInvert);
      Settings.BlindR_Name(RightName); BlindR->Name = Settings.BlindR_Name();
      Settings.BlindR_StepTime(RightStepTime); BlindR->Step_Ms(RightStepTime);
      Settings.BlindR_ButtonOpen(RightButtonOpen); BlindR->ButtonOpenEnabled(RightButtonOpen);
      Settings.BlindR_ButtonClose(RightButtonClose); BlindR->ButtonCloseEnabled(RightButtonClose);
      Settings.BlindR_InvertButtons(RightInvert); BlindR->InvertButtons(RightInvert);

      // Pins are only read when the Blinds objects are constructed at boot,
      // so - unlike the fields above - changing them has no live effect.
      bool pinsChanged = LeftPinButtonOpen != Settings.BlindL_PinButtonOpen() || LeftPinButtonClose != Settings.BlindL_PinButtonClose()
        || LeftPinSwitchOpen != Settings.BlindL_PinSwitchOpen() || LeftPinSwitchClose != Settings.BlindL_PinSwitchClose()
        || RightPinButtonOpen != Settings.BlindR_PinButtonOpen() || RightPinButtonClose != Settings.BlindR_PinButtonClose()
        || RightPinSwitchOpen != Settings.BlindR_PinSwitchOpen() || RightPinSwitchClose != Settings.BlindR_PinSwitchClose();
      Settings.BlindL_PinButtonOpen(LeftPinButtonOpen);
      Settings.BlindL_PinButtonClose(LeftPinButtonClose);
      Settings.BlindL_PinSwitchOpen(LeftPinSwitchOpen);
      Settings.BlindL_PinSwitchClose(LeftPinSwitchClose);
      Settings.BlindR_PinButtonOpen(RightPinButtonOpen);
      Settings.BlindR_PinButtonClose(RightPinButtonClose);
      Settings.BlindR_PinSwitchOpen(RightPinSwitchOpen);
      Settings.BlindR_PinSwitchClose(RightPinSwitchClose);

      Settings.Save();
      restart = pinsChanged;
    } else if(section == "Log") {
      uint8_t Endpoint(Settings.Log_Endpoint());
      uint8_t Level(Settings.Log_Level());
      String Server(Settings.Syslog_Server());
      uint16_t Port(Settings.Syslog_Port());

      for(uint8_t i = 0; i < (uint8_t)request->args(); i++) {
        const AsyncWebParameter *p = request->getParam(i);
        String n = p->name(), v = p->value();
        if(n == "Endpoint") Endpoint = v.toInt();
        if(n == "Level") Level = v.toInt();
        // A hostname, not just a dotted IP - Server.fromString() used to
        // silently fail (and drop the change) for anything but a literal
        // IP; resolution now happens at send time in Logger::LogToSyslog().
        if(n == "Syslog Server") Server = v;
        if(n == "Syslog Port") Port = v.toInt();
      }

      Settings.Log_Endpoint(Endpoint); Logger.Endpoint(Endpoint);
      Settings.Log_Level(Level); Logger.Level(Level);
      Settings.Syslog_Server(Server); Logger.Syslog_Server(Server);
      Settings.Syslog_Port(Port); Logger.Syslog_Port(Port);
      Settings.Save();
      // Applied live above (Logger just re-reads its own bitmask/host on
      // every Write()) - unlike most other sections, nothing here actually
      // needs a restart to take effect.
      restart = false;
    } else if(section == "General") {
      bool NTPEnabled(Settings.General_NTPEnabled());
      String NTPServer(Settings.General_NTPServer());
      int8_t TimeZone(Settings.General_TimeZone());

      for(uint8_t i = 0; i < (uint8_t)request->args(); i++) {
        const AsyncWebParameter *p = request->getParam(i);
        String n = p->name(), v = p->value();
        if(n == "NTP Enabled") NTPEnabled = (v == "true");
        if(n == "NTP Server") NTPServer = v;
        if(n == "Time Zone") TimeZone = v.toInt();
      }

      Settings.General_NTPEnabled(NTPEnabled);
      Settings.General_NTPServer(NTPServer);
      Settings.General_TimeZone(TimeZone);
      Settings.Save();
      // The clock is only ever synchronized at boot, so a Server/Time Zone/
      // Enabled change has nothing to apply to until the device restarts.
      restart = true;
    } else if(section == "MQTT") {
      bool Enabled(Settings.MQTT_Enabled());
      String Broker(Settings.MQTT_Broker()), User(Settings.MQTT_User()), Password(Settings.MQTT_Password()), DiscoveryPrefix(Settings.MQTT_DiscoveryPrefix());
      uint16_t Port(Settings.MQTT_Port());
      bool DiscoveryEnabled(Settings.MQTT_DiscoveryEnabled());

      for(uint8_t i = 0; i < (uint8_t)request->args(); i++) {
        const AsyncWebParameter *p = request->getParam(i);
        String n = p->name(), v = p->value();
        if(n == "Enabled") Enabled = (v == "true");
        if(n == "Broker") Broker = v;
        if(n == "Port") Port = v.toInt();
        if(n == "User") User = v;
        if(n == "Password") Password = v;
        if(n == "Discovery Enabled") DiscoveryEnabled = (v == "true");
        if(n == "Discovery Prefix") DiscoveryPrefix = v;
      }

      Settings.MQTT_Enabled(Enabled);
      Settings.MQTT_Broker(Broker);
      Settings.MQTT_Port(Port);
      Settings.MQTT_User(User);
      Settings.MQTT_Password(Password);
      Settings.MQTT_DiscoveryEnabled(DiscoveryEnabled);
      Settings.MQTT_DiscoveryPrefix(DiscoveryPrefix);
      Settings.Save();
      // MQTTClient.Start() only ever runs once, at boot.
      restart = true;
    } else if(section == "Webhooks") {
      bool Enabled(Settings.Webhooks_Enabled());
      String Token(Settings.Webhooks_Token());
      uint16_t Port(Settings.Webhooks_Port());

      for(uint8_t i = 0; i < (uint8_t)request->args(); i++) {
        const AsyncWebParameter *p = request->getParam(i);
        String n = p->name(), v = p->value();
        if(n == "Enabled") Enabled = (v == "true");
        if(n == "Token") Token = v;
        if(n == "Port") Port = v.toInt();
      }

      Settings.Webhooks_Enabled(Enabled);
      Settings.Webhooks_Token(Token);
      Settings.Webhooks_Port(Port);
      Settings.Save();
      // WebhookServer.Start() only ever runs once, at boot.
      restart = true;
    } else {
      request->send(400, "application/json", "{\"error\":\"Unknown section.\"}");
      return;
    }

    request->send(200, "application/json", String("{\"restart\":") + (restart ? "true" : "false") + "}");
    Logger.Write("Web POST " + request->url() + " - Settings changed (" + section + ") (" + session->Username + ")");
  });

  // Manual override for when NTP is off (or unreachable) - only meaningful
  // pairing with the boot-only sync above, same as DeviceIQ's own use for
  // this endpoint.
  Webserver->on("/api/clock/set", HTTP_POST, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session || !session->Admin) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }

    String DateTime;
    for(uint8_t i = 0; i < (uint8_t)request->args(); i++) {
      if(request->getParam(i)->name() == "datetime") DateTime = request->getParam(i)->value();
    }

    struct tm TimeInfo = {};
    if(sscanf(DateTime.c_str(), "%d-%d-%dT%d:%d:%d", &TimeInfo.tm_year, &TimeInfo.tm_mon, &TimeInfo.tm_mday, &TimeInfo.tm_hour, &TimeInfo.tm_min, &TimeInfo.tm_sec) != 6) {
      request->send(400, "application/json", "{\"error\":\"Invalid date/time.\"}");
      return;
    }

    TimeInfo.tm_year -= 1900;
    TimeInfo.tm_mon -= 1;

    // Establishes the same local-time offset mktime() below relies on to
    // turn the entered local time into a UTC epoch - independent of
    // whether NTP ever actually ran.
    configTime(Settings.General_TimeZone() * 3600, 0, "");
    time_t Epoch = mktime(&TimeInfo);
    struct timeval NewTime = { Epoch, 0 };
    settimeofday(&NewTime, nullptr);

    request->send(200, "application/json", "{\"ok\":true}");
    Logger.Write("Web POST " + request->url() + " - Clock set manually (" + session->Username + ")");
  });

  Webserver->on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session || !session->Admin) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }

    request->send(200, "application/json", "{\"ok\":true}");
    Logger.Write("Web POST " + request->url() + " - Restart (" + session->Username + ")");
    RestartDevice();
  });

  Webserver->on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session || !session->Admin) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }

    request->send(200, "application/json", "{\"ok\":true}");
    Logger.Write("Web POST " + request->url() + " - Reset to Factory Defaults (" + session->Username + ")");
    Settings.FactoryReset();
    RestartDevice();
  });

  Webserver->on("/api/config/export", HTTP_GET, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session || !session->Admin) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }

    AsyncWebServerResponse *response = request->beginResponse(LittleFS, CONFIG_FILE_NAME, "application/json");
    response->addHeader("Content-Disposition", "attachment; filename=\"config-" + Settings.Network_Hostname() + ".json\"");
    request->send(response);
    Logger.Write("Web GET " + request->url() + " - Config exported (" + session->Username + ")");
  });

  // Two-step, like DeviceIQ: /import stages and validates the uploaded file
  // without touching the live configuration; /import/apply commits the
  // already-validated staging file and restarts. Nothing is overwritten
  // just from an upload the admin hasn't confirmed yet.
  Webserver->on("/api/config/import", HTTP_POST,
    [](AsyncWebServerRequest *request){
      WebSession *session = AuthenticatedSession(request);
      if(!session || !session->Admin) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }

      File file = LittleFS.open(CONFIG_IMPORT_FILE_NAME, "r");
      if(!file) { request->send(400, "application/json", "{\"error\":\"No file received.\"}"); return; }

      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, file);
      file.close();

      bool valid = !err && doc["Network"].is<JsonObject>() && doc["Users"].is<JsonArrayConst>() && doc["Users"].as<JsonArrayConst>().size() > 0;
      if(!valid) {
        LittleFS.remove(CONFIG_IMPORT_FILE_NAME);
        request->send(400, "application/json", "{\"error\":\"The uploaded file is not a valid configuration.\"}");
        return;
      }

      request->send(200, "application/json", "{\"ok\":true}");
      Logger.Write("Web POST " + request->url() + " - Config validated (" + session->Username + ")");
    },
    [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final){
      WebSession *session = AuthenticatedSession(request);
      if(!session || !session->Admin) return;

      if(index == 0) request->_tempFile = LittleFS.open(CONFIG_IMPORT_FILE_NAME, "w");
      if(request->_tempFile) request->_tempFile.write(data, len);
      if(final && request->_tempFile) request->_tempFile.close();
    }
  );

  Webserver->on("/api/config/import/apply", HTTP_POST, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session || !session->Admin) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }

    if(!LittleFS.exists(CONFIG_IMPORT_FILE_NAME)) {
      request->send(400, "application/json", "{\"error\":\"No validated import to apply.\"}");
      return;
    }

    LittleFS.remove(CONFIG_FILE_NAME);
    LittleFS.rename(CONFIG_IMPORT_FILE_NAME, CONFIG_FILE_NAME);

    // The imported file becomes the seed of truth again - stale persisted
    // state (blind position) from before the import could otherwise
    // silently override it on the next boot, same as DeviceIQ.
    LittleFS.remove(STATE_FILE_NAME);

    request->send(200, "application/json", "{\"ok\":true}");
    Logger.Write("Web POST " + request->url() + " - Config imported (" + session->Username + ")");
    RestartDevice();
  });

  Webserver->begin();

  MQTTClient.Start();
  WebhookServer.Start();

  Logger.Write("Ready!");
}