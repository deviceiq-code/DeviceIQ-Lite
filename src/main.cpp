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
#include "core/MQTTClient.h"
#include "core/WebhookServer.h"
#include "Version.h"
#include "Timer.h"
#include "components/ComponentManager.h"
#include "components/Relay.h"
#include "components/Button.h"
#include "components/Thermometer.h"
#include "components/Blinds.h"

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

// DeviceIQ's own "Web Server: ..." log lines always identify the caller as
// username@remoteIP - matched here so Lite's log reads the same way.
String ClientIP(AsyncWebServerRequest *request) {
  return request->client() ? request->client()->remoteIP().toString() : String("?");
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
  uint32_t now = millis();
  Components.Control(now);
  GeneralTimer->Control();
  MQTTClient.Loop();

  // Checked periodically rather than on every state change - a moving
  // Blinds would otherwise mean a state.json write per 1% step.
  static uint32_t lastStateSaveCheck = 0;
  if(now - lastStateSaveCheck >= STATE_SAVE_INTERVAL_MS) {
    lastStateSaveCheck = now;
    if(Components.PersistenceRequired() && !Settings.SaveComponentsState()) {
      Logger.Write("Error saving component state.", logger::Warning);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting " + String(Version::ProductFamily) + "...");

  // Matches DeviceIQ's App::Start(), which does the same before anything
  // else - without it the clock sits at the Unix epoch (1970) until NTP
  // syncs, or forever if it's disabled/unreachable.
  struct timeval InitialTime = { (time_t)Defaults.General.InitialTimeAndDate, 0 };
  settimeofday(&InitialTime, nullptr);

  if(!LittleFS.begin()) {
    Serial.println("Error while mounting LittleFS. Unable to continue.");
    return;
  }

  GeneralTimer = new Timer();
  bool ConfigurationLoaded = Settings.Load();

  // Settings.FactoryReset();

  Settings.CheckButtonsFactoryReset();
  Logger.Syslog_Server(Settings.Syslog_Server());
  Logger.Syslog_Port(Settings.Syslog_Port());
  Logger.Hostname(Settings.Network_Hostname());
  Logger.Endpoint(Settings.Log_Endpoint());
  Logger.Level(Settings.Log_Level());

  // Matches DeviceIQ's own first three boot log lines, once Logger is
  // configured enough to write them (LittleFS/Settings.Load() above already
  // succeeded by this point, so both are retroactively "initialized").
  Logger.Write(Version::Info());
  Logger.Write("Logger initialized");
  Logger.Write("FileSystem initialized");

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
    Logger.Write("Network: WiFi Client, connected to " + Settings.Network_SSID() + " (Hostname: " + Settings.Network_Hostname() + " | IP: " + WiFi.localIP().toString() + " | MAC: " + WiFi.macAddress() + " | RSSI: " + String(WiFi.RSSI()) + " dBm)");
    Logger.Write("Web UI address: http://" + WiFi.localIP().toString() + ":" + Settings.Network_HTTP_Port() + "/");

    if(Settings.General_NTPEnabled()) {
      configTime(Settings.General_TimeZone() * 3600, 0, Settings.General_NTPServer().c_str());

      uint8_t NTPRetry = 0;
      while(time(nullptr) < 100000 && NTPRetry < 20) { delay(500); NTPRetry++; }

      if(time(nullptr) >= 100000) {
        // Unlike DeviceIQ, the clock is synchronized once here and never
        // again - stopping SNTP keeps it that way instead of the lwIP
        // client quietly re-syncing on its own schedule.
        sntp_stop();
        Logger.Write("Date and time: Updated from NTP server " + Settings.General_NTPServer());
      } else {
        Logger.Write("Date and time: NTP update failed using " + Settings.General_NTPServer() + " (attempts: " + String(NTPRetry) + ")", logger::Warning);
      }
    } else {
      Logger.Write("Date and time: NTP disabled, using local clock");
    }
  } else if(Settings.Network_FallbackAPEnabled()) {
    String AP_SSID = Settings.Network_FallbackAPSSID().length() ? Settings.Network_FallbackAPSSID() : (String(Version::ProductFamily) + "-" + Settings.Network_Hostname());
    WiFi.softAP(AP_SSID.c_str(), Settings.Network_FallbackAPPassword().c_str(), 11, 0, 4);
    Settings.AP_Mode(true);
    Logger.Write("Network: SoftAP active as " + AP_SSID + " (Hostname: " + Settings.Network_Hostname() + " | IP: " + WiFi.softAPIP().toString() + " | MAC: " + WiFi.softAPmacAddress() + ")");
    Logger.Write("Web UI address: http://" + WiFi.softAPIP().toString() + ":" + Settings.Network_HTTP_Port() + "/");
  } else {
    Logger.Write("Network status: Offline (unable to connect to " + Settings.Network_SSID() + "; fallback AP disabled)", logger::Warning);
  }

  MDNS.begin(Settings.Network_Hostname());

  if(!Settings.InstallComponents()) {
    Logger.Write("Failed to install components from config.json.", logger::Error);
  } else if(!Components.Start()) {
    Logger.Write("Failed to start components: " + Components.StartError(), logger::Error);
  }

  Webserver = new AsyncWebServer(Settings.Network_HTTP_Port());

  Webserver->onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "404: Not found");
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
  });

  Webserver->on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", String(), false, cgi);
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

    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"success\":true}");
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
      Logger.Write("Web Server: Logon failed for " + username + "@" + ClientIP(request) + " - invalid credentials", logger::Warning);
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

    Logger.Write("Web Server: Logon successful for " + info.Username + "@" + ClientIP(request));
  });

  // Every page below is a static file that gates itself client-side via
  // /api/session (DeviceIQ's pattern) - none of them need a server-side
  // check anymore, now that logging in is always required.
  Webserver->on("/dashboard.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/dashboard.html", "text/html");
  });

  Webserver->on("/component.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/component.html", "text/html");
  });

  Webserver->on("/setup.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/setup.html", "text/html");
  });

  Webserver->on("/about.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/about.html", "text/html");
  });

  Webserver->on("/users.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/users.html", "text/html");
  });

  Webserver->on("/log.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/log.html", "text/html");
  });

  // Registered before the plain "/api/log" GET below: ESPAsyncWebServer's
  // default URI matching treats "/api/log" as matching both an exact
  // request and any path starting with "/api/log/" (its "backward
  // compatible" matcher), and handlers are tried in registration order -
  // registered after, "/api/log/export" would never be reached, every
  // request to it swallowed by "/api/log" first (same hazard the
  // /api/components/* routes above are ordered to avoid).
  Webserver->on("/api/log/export", HTTP_GET, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }
    if(!session->Admin) { request->send(403, "application/json", "{\"error\":\"admin session required\"}"); return; }

    AsyncWebServerResponse *response = LittleFS.exists(LOG_FILE_NAME)
      ? request->beginResponse(LittleFS, LOG_FILE_NAME, "text/plain")
      : request->beginResponse(200, "text/plain", "");
    response->addHeader("Content-Disposition", "attachment; filename=\"device.log\"");
    request->send(response);
    Logger.Write("Web Server: log exported by " + session->Username + "@" + ClientIP(request));
  });

  Webserver->on("/api/log", HTTP_GET, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }
    if(!session->Admin) { request->send(403, "application/json", "{\"error\":\"admin session required\"}"); return; }

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

  Webserver->on("/api/log/clear", HTTP_POST, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }
    if(!session->Admin) { request->send(403, "application/json", "{\"error\":\"admin session required\"}"); return; }

    bool success = !LittleFS.exists(LOG_FILE_NAME) || LittleFS.remove(LOG_FILE_NAME);

    if(!success) {
      request->send(500, "application/json", "{\"error\":\"Unable to clear the log file.\"}");
      Logger.Write("Web Server: log clear rejected for " + session->Username + "@" + ClientIP(request), logger::Warning);
      return;
    }

    request->send(200, "application/json", "{\"success\":true}");
    // Logged after clearing: if File is one of the active log endpoints,
    // this becomes the first entry of the fresh log.
    Logger.Write("Web Server: log clear accepted for " + session->Username + "@" + ClientIP(request));
  });

  // These four routes must be registered before the plain "/api/components"
  // GET/POST handlers below: ESPAsyncWebServer's default URI matching is
  // "backward compatible" (AsyncURIMatcher::Type::BackwardCompatible) -
  // "/api/components" matches an exact request to "/api/components" *and*
  // anything starting with "/api/components/", and handlers are tried in
  // registration order. Registered after, these would never be reached -
  // every request to them would be swallowed by "/api/components" first.

  // Backs component.html's Add/Edit form: with no params, every configured
  // component (id/name/class only, including private Blinds members) - used
  // to populate the Relay/Button selectors when adding or editing a Blinds
  // group. With ?id=, one component's full Setup - used to prefill the Edit
  // form.
  Webserver->on("/api/components/catalog", HTTP_GET, [](AsyncWebServerRequest *request) {
    WebSession *session = AuthenticatedSession(request);
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }
    if(!session->Admin) { request->send(403, "application/json", "{\"error\":\"admin session required\"}"); return; }

    File file = LittleFS.open(CONFIG_FILE_NAME, "r");
    JsonDocument doc;
    if(file) { deserializeJson(doc, file); file.close(); }
    JsonObjectConst components = doc["Components"].as<JsonObjectConst>();

    JsonDocument response;
    if(request->hasParam("id")) {
      int16_t id = (int16_t)request->getParam("id")->value().toInt();
      JsonObjectConst item = components.isNull() ? JsonObjectConst() : components[String(id)].as<JsonObjectConst>();
      if(item.isNull()) { request->send(404, "application/json", "{\"error\":\"component not found\"}"); return; }

      response["id"] = id;
      response["setup"] = item["Setup"];
      response["enabled"] = item["Properties"]["Enabled"] | true;
    } else {
      JsonArray items = response["components"].to<JsonArray>();
      if(!components.isNull()) {
        for(JsonPairConst entry : components) {
          JsonObjectConst setup = entry.value()["Setup"].as<JsonObjectConst>();
          JsonObject out = items.add<JsonObject>();
          out["id"] = String(entry.key().c_str()).toInt();
          out["name"] = setup["Name"] | "";
          out["class"] = setup["Class"] | "";
        }
      }
    }

    AsyncResponseStream *responseStream = request->beginResponseStream("application/json");
    serializeJson(response, *responseStream);
    request->send(responseStream);
  });

  // A newly created component must already be a fully valid catalog entry,
  // so only the identity fields validation depends on (name, and either
  // address or relayOpen/relayClose) are accepted here - everything else
  // follows via /api/components/update. Requires a restart to take effect
  // (see settings::AddComponent()).
  Webserver->on("/api/components/add", HTTP_POST, [](AsyncWebServerRequest *request) {
    WebSession *session = AuthenticatedSession(request);
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }
    if(!session->Admin) { request->send(403, "application/json", "{\"error\":\"admin session required\"}"); return; }

    String className, name, addressText, relayOpenText, relayCloseText;
    for(uint8_t i = 0; i < (uint8_t)request->args(); i++) {
      const AsyncWebParameter *p = request->getParam(i);
      if(p->name() == "class") className = p->value();
      else if(p->name() == "name") name = p->value();
      else if(p->name() == "address") addressText = p->value();
      else if(p->name() == "relayOpen") relayOpenText = p->value();
      else if(p->name() == "relayClose") relayCloseText = p->value();
    }

    int16_t newID = 0;
    String error;
    bool ok;
    if(className == "Blinds") {
      if(relayOpenText.length() == 0 || relayCloseText.length() == 0) {
        request->send(400, "application/json", "{\"error\":\"relayOpen and relayClose are required\"}");
        return;
      }
      ok = Settings.AddComponent(className, name, -1, (int16_t)relayOpenText.toInt(), (int16_t)relayCloseText.toInt(), newID, error);
    } else {
      if(addressText.length() == 0) {
        request->send(400, "application/json", "{\"error\":\"address is required\"}");
        return;
      }
      ok = Settings.AddComponent(className, name, addressText.toInt(), 0, 0, newID, error);
    }

    Logger.Write("Web Server: component add " + String(ok ? "accepted" : "rejected") + " for " + session->Username + "@" + ClientIP(request) + ": " + className + (ok ? " (#" + String(newID) + ")" : ": " + error));
    if(!ok) { request->send(422, "application/json", "{\"error\":\"" + error + "\"}"); return; }
    request->send(200, "application/json", "{\"success\":true,\"id\":" + String(newID) + "}");
  });

  // Every submitted param except "id" is forwarded as-is into
  // settings::UpdateComponent()'s Fields object; it only recognizes the
  // field names relevant to the component's own class and ignores the
  // rest. Requires a restart to take effect.
  Webserver->on("/api/components/update", HTTP_POST, [](AsyncWebServerRequest *request) {
    WebSession *session = AuthenticatedSession(request);
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }
    if(!session->Admin) { request->send(403, "application/json", "{\"error\":\"admin session required\"}"); return; }

    String idText;
    JsonDocument fieldsDoc;
    JsonObject fields = fieldsDoc.to<JsonObject>();
    for(uint8_t i = 0; i < (uint8_t)request->args(); i++) {
      const AsyncWebParameter *p = request->getParam(i);
      if(p->name() == "id") idText = p->value();
      else fields[p->name()] = p->value();
    }

    if(idText.length() == 0) { request->send(400, "application/json", "{\"error\":\"missing id\"}"); return; }
    int16_t id = (int16_t)idText.toInt();

    String error;
    bool ok = Settings.UpdateComponent(id, fields, error);
    Logger.Write("Web Server: component update " + String(ok ? "accepted" : "rejected") + " for " + session->Username + "@" + ClientIP(request) + ": #" + String(id) + (ok ? "" : ": " + error));

    if(!ok) { request->send(422, "application/json", "{\"error\":\"" + error + "\"}"); return; }
    request->send(200, "application/json", "{\"success\":true,\"restart\":true}");
  });

  // Removes the config.json entry only - the running component isn't torn
  // down live, so this requires a restart to actually take effect. Refused
  // while the component is still a Blinds group's member (see
  // settings::RemoveComponent()).
  Webserver->on("/api/components/remove", HTTP_POST, [](AsyncWebServerRequest *request) {
    WebSession *session = AuthenticatedSession(request);
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }
    if(!session->Admin) { request->send(403, "application/json", "{\"error\":\"admin session required\"}"); return; }

    String idText;
    for(uint8_t i = 0; i < (uint8_t)request->args(); i++) {
      const AsyncWebParameter *p = request->getParam(i);
      if(p->name() == "id") idText = p->value();
    }
    if(idText.length() == 0) { request->send(400, "application/json", "{\"error\":\"missing id\"}"); return; }
    int16_t id = (int16_t)idText.toInt();

    String error;
    bool ok = Settings.RemoveComponent(id, error);
    Logger.Write("Web Server: component remove " + String(ok ? "accepted" : "rejected") + " for " + session->Username + "@" + ClientIP(request) + ": #" + String(id) + (ok ? "" : ": " + error));

    if(!ok) { request->send(422, "application/json", "{\"error\":\"" + error + "\"}"); return; }
    request->send(200, "application/json", "{\"success\":true,\"restart\":true}");
  });

  // Every configured component (Relay/Button/Thermometer/Blinds) - backs
  // both the Dashboard (every logged-in user, matching the original
  // DeviceIQ's own /api/components) and setup.html's Components panel
  // (admin-only, with a handful of safe properties editable in place via
  // the POST below). Adding, removing or rewiring components is done via
  // the /api/components/add|update|remove endpoints above, or by importing
  // an updated config.json - either way requires a restart.
  Webserver->on("/api/components", HTTP_GET, [](AsyncWebServerRequest *request) {
    WebSession *session = AuthenticatedSession(request);
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }

    JsonDocument doc;
    JsonArray items = doc["components"].to<JsonArray>();
    for(size_t i = 0; i < Components.Count(); i++) {
      component *item = Components.At(i);
      if(item == nullptr || !item->IsPublic()) continue;

      JsonObject entry = items.add<JsonObject>();
      entry["id"] = item->ID();
      entry["name"] = item->Name();
      entry["class"] = component::ClassName(item->Class());
      entry["enabled"] = item->Enabled();

      if(item->Class() == component::Classes::Relay) {
        entry["address"] = item->Address();
        entry["state"] = static_cast<const relay&>(*item).State();
      } else if(item->Class() == component::Classes::Button) {
        entry["address"] = item->Address();
        entry["pressed"] = static_cast<const button&>(*item).IsPressed();
      } else if(item->Class() == component::Classes::Thermometer) {
        const thermometer &sensor = static_cast<const thermometer&>(*item);
        entry["address"] = item->Address();
        entry["available"] = sensor.Available();
        if(sensor.Available()) entry["temperature"] = sensor.Temperature();
        if(sensor.Available() && sensor.HasHumidity()) entry["humidity"] = sensor.Humidity();
      } else if(item->Class() == component::Classes::Blinds) {
        const blinds &b = static_cast<const blinds&>(*item);
        entry["state"] = blinds::StateName(b.State());
        entry["position"] = b.Position();
        entry["stepTimeMs"] = b.StepTime();
        entry["buttonOpenEnabled"] = b.ButtonOpenEnabled();
        entry["buttonCloseEnabled"] = b.ButtonCloseEnabled();
        entry["invertButtons"] = b.InvertButtons();
      }
    }

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
  });

  Webserver->on("/api/components", HTTP_POST, [](AsyncWebServerRequest *request) {
    WebSession *session = AuthenticatedSession(request);
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }
    if(!session->Admin) { request->send(403, "application/json", "{\"error\":\"admin session required\"}"); return; }

    String idText, property, value;
    for(uint8_t i = 0; i < (uint8_t)request->args(); i++) {
      const AsyncWebParameter *p = request->getParam(i);
      if(p->name() == "id") idText = p->value();
      if(p->name() == "property") property = p->value();
      if(p->name() == "value") value = p->value();
    }

    if(idText.length() == 0 || property.length() == 0) {
      request->send(400, "application/json", "{\"error\":\"missing id/property\"}");
      return;
    }

    int16_t id = (int16_t)idText.toInt();
    String error;
    bool ok = Settings.SetComponentProperty(id, property, value, error);
    Logger.Write("Web Server: component set " + String(ok ? "accepted" : "rejected") + " for " + session->Username + "@" + ClientIP(request) + ": #" + String(id) + "." + property + "=" + value + (ok ? "" : ": " + error));

    if(!ok) { request->send(422, "application/json", "{\"error\":\"" + error + "\"}"); return; }
    request->send(200, "application/json", "{\"success\":true}");
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
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }
    if(!session->Admin) { request->send(403, "application/json", "{\"error\":\"admin session required\"}"); return; }

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
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }
    if(!session->Admin) { request->send(403, "application/json", "{\"error\":\"admin session required\"}"); return; }

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
    Logger.Write("Web Server: user add " + String(saved ? "accepted" : "accepted but not saved") + " by " + session->Username + "@" + ClientIP(request) + ": " + Username);
    request->send(200, "application/json", saved ? "{\"success\":true}" : "{\"success\":true,\"warning\":\"could not save to disk; this change will be lost on restart\"}");
  });

  Webserver->on("/api/users/remove", HTTP_POST, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }
    if(!session->Admin) { request->send(403, "application/json", "{\"error\":\"admin session required\"}"); return; }

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
    Logger.Write("Web Server: user remove " + String(saved ? "accepted" : "accepted but not saved") + " by " + session->Username + "@" + ClientIP(request) + ": " + Username);
    request->send(200, "application/json", saved ? "{\"success\":true}" : "{\"success\":true,\"warning\":\"could not save to disk; this change will be lost on restart\"}");
  });

  Webserver->on("/api/users/rename", HTTP_POST, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }
    if(!session->Admin) { request->send(403, "application/json", "{\"error\":\"admin session required\"}"); return; }

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
    Logger.Write("Web Server: user rename " + String(saved ? "accepted" : "accepted but not saved") + " by " + session->Username + "@" + ClientIP(request) + ": " + Username + " -> " + NewUsername);
    request->send(200, "application/json", saved ? "{\"success\":true}" : "{\"success\":true,\"warning\":\"could not save to disk; this change will be lost on restart\"}");
  });

  Webserver->on("/api/users/set-admin", HTTP_POST, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }
    if(!session->Admin) { request->send(403, "application/json", "{\"error\":\"admin session required\"}"); return; }

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
    Logger.Write("Web Server: user set-admin " + String(saved ? "accepted" : "accepted but not saved") + " by " + session->Username + "@" + ClientIP(request) + ": " + Username + "=" + (Admin ? "true" : "false"));
    request->send(200, "application/json", saved ? "{\"success\":true}" : "{\"success\":true,\"warning\":\"could not save to disk; this change will be lost on restart\"}");
  });

  Webserver->on("/api/users/set-password", HTTP_POST, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }
    if(!session->Admin) { request->send(403, "application/json", "{\"error\":\"admin session required\"}"); return; }

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
    Logger.Write("Web Server: user set-password " + String(saved ? "accepted" : "accepted but not saved") + " by " + session->Username + "@" + ClientIP(request) + ": " + Username);
    request->send(200, "application/json", saved ? "{\"success\":true}" : "{\"success\":true,\"warning\":\"could not save to disk; this change will be lost on restart\"}");
  });

  Webserver->on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }
    if(!session->Admin) { request->send(403, "application/json", "{\"error\":\"admin session required\"}"); return; }

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
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }
    if(!session->Admin) { request->send(403, "application/json", "{\"error\":\"admin session required\"}"); return; }

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
    Logger.Write("Web Server: settings update accepted for " + session->Username + "@" + ClientIP(request) + ": " + section);
  });

  // Manual override for when NTP is off (or unreachable) - only meaningful
  // pairing with the boot-only sync above, same as DeviceIQ's own use for
  // this endpoint.
  Webserver->on("/api/clock/set", HTTP_POST, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }
    if(!session->Admin) { request->send(403, "application/json", "{\"error\":\"admin session required\"}"); return; }

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

    request->send(200, "application/json", "{\"success\":true}");
    Logger.Write("Web Server: clock set manually by " + session->Username + "@" + ClientIP(request));
  });

  Webserver->on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }
    if(!session->Admin) { request->send(403, "application/json", "{\"error\":\"admin session required\"}"); return; }

    request->send(200, "application/json", "{\"success\":true}");
    Logger.Write("Web Server: reboot requested by " + session->Username + "@" + ClientIP(request));
    RestartDevice();
  });

  Webserver->on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }
    if(!session->Admin) { request->send(403, "application/json", "{\"error\":\"admin session required\"}"); return; }

    request->send(200, "application/json", "{\"success\":true}");
    Logger.Write("Web Server: configuration reset to factory defaults by " + session->Username + "@" + ClientIP(request) + "; restarting", logger::Warning);
    Settings.FactoryReset();
    RestartDevice();
  });

  Webserver->on("/api/config/export", HTTP_GET, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }
    if(!session->Admin) { request->send(403, "application/json", "{\"error\":\"admin session required\"}"); return; }

    AsyncWebServerResponse *response = request->beginResponse(LittleFS, CONFIG_FILE_NAME, "application/json");
    response->addHeader("Content-Disposition", "attachment; filename=\"config-" + Settings.Network_Hostname() + ".json\"");
    request->send(response);
    Logger.Write("Web Server: configuration exported by " + session->Username + "@" + ClientIP(request));
  });

  // Registered before "/api/config/import" below: ESPAsyncWebServer's
  // default URI matching treats "/api/config/import" as matching both an
  // exact request and any path starting with "/api/config/import/" (its
  // "backward compatible" matcher), and handlers are tried in registration
  // order - registered after, "/api/config/import/apply" would never be
  // reached, every request to it swallowed by "/api/config/import" first
  // (same hazard the /api/components/* and /api/log/* routes are ordered
  // to avoid).
  Webserver->on("/api/config/import/apply", HTTP_POST, [](AsyncWebServerRequest *request){
    WebSession *session = AuthenticatedSession(request);
    if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }
    if(!session->Admin) { request->send(403, "application/json", "{\"error\":\"admin session required\"}"); return; }

    if(!LittleFS.exists(CONFIG_IMPORT_FILE_NAME)) {
      request->send(400, "application/json", "{\"error\":\"No validated import to apply.\"}");
      return;
    }

    LittleFS.remove(CONFIG_FILE_NAME);
    LittleFS.rename(CONFIG_IMPORT_FILE_NAME, CONFIG_FILE_NAME);

    // The imported catalog becomes the seed of truth again - stale runtime
    // state (keyed by component ID) from before the import could otherwise
    // silently override it on the next boot.
    LittleFS.remove(STATE_FILE_NAME);

    request->send(200, "application/json", "{\"success\":true}");
    Logger.Write("Web Server: configuration imported by " + session->Username + "@" + ClientIP(request) + "; restarting");
    RestartDevice();
  });

  // Two-step, like DeviceIQ: /import stages and validates the uploaded file
  // without touching the live configuration; /import/apply (above) commits
  // the already-validated staging file and restarts. Nothing is overwritten
  // just from an upload the admin hasn't confirmed yet.
  Webserver->on("/api/config/import", HTTP_POST,
    [](AsyncWebServerRequest *request){
      WebSession *session = AuthenticatedSession(request);
      if(!session) { request->send(401, "application/json", "{\"error\":\"unauthenticated\"}"); return; }
      if(!session->Admin) { request->send(403, "application/json", "{\"error\":\"admin session required\"}"); return; }

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

      request->send(200, "application/json", "{\"success\":true}");
      Logger.Write("Web Server: configuration import staged by " + session->Username + "@" + ClientIP(request));
    },
    [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final){
      WebSession *session = AuthenticatedSession(request);
      if(!session || !session->Admin) return;

      if(index == 0) request->_tempFile = LittleFS.open(CONFIG_IMPORT_FILE_NAME, "w");
      if(request->_tempFile) request->_tempFile.write(data, len);
      if(final && request->_tempFile) request->_tempFile.close();
    }
  );

  Webserver->begin();

  MQTTClient.Start();
  WebhookServer.Start();

  if(ConfigurationLoaded) Logger.Write("Configuration initialized - file " + String(CONFIG_FILE_NAME) + " read");
  else Logger.Write("Configuration initialized with defaults - file " + String(CONFIG_FILE_NAME) + " not loaded", logger::Warning);

  Logger.Write("Ready!");
}