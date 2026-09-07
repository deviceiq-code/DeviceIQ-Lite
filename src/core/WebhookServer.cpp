#include "WebhookServer.h"

#include <ArduinoJson.h>

#include "core/Settings.h"
#include "core/Logger.h"
#include "Blinds.h"

extern Blinds *BlindL, *BlindR;

namespace {
    void AppendBlindJson(JsonObject entry, int id, const String& name, Blinds* blind) {
        entry["id"] = id;
        entry["name"] = name;
        entry["class"] = "Blinds";
        entry["enabled"] = true;
        entry["state"] = BlindStateName(blind->State());
        entry["position"] = blind->Position();
        entry["targetPosition"] = blind->TargetPosition();
    }

    Blinds* BlindByID(int id) {
        if(id == 1) return BlindL;
        if(id == 2) return BlindR;
        return nullptr;
    }

    String NameByID(int id) {
        if(id == 1) return Settings.BlindL_Name();
        if(id == 2) return Settings.BlindR_Name();
        return String();
    }

    String ParamOr(AsyncWebServerRequest *request, const char* name, const String& fallback = "") {
        return request->hasParam(name) ? request->getParam(name)->value() : fallback;
    }
}

bool webhookserver::TokenValid(const String& provided) {
    String expected = Settings.Webhooks_Token();
    if(expected.length() == 0 || provided.length() != expected.length()) return false;

    uint8_t difference = 0;
    for(size_t i = 0; i < expected.length(); i++) difference |= (uint8_t)provided[i] ^ (uint8_t)expected[i];
    return difference == 0;
}

void webhookserver::Start() {
    if(!Settings.Webhooks_Enabled() || pServer != nullptr) return;

    pServer = new AsyncWebServer(Settings.Webhooks_Port());
    pServer->on("/component/get", HTTP_GET, HandleGet);
    pServer->on("/component/get", HTTP_POST, HandleGet);
    pServer->on("/component/set", HTTP_GET, HandleSet);
    pServer->on("/component/set", HTTP_POST, HandleSet);
    pServer->onNotFound(HandleNotFound);
    pServer->begin();

    Logger.Write("Webhooks server listening on port " + String(Settings.Webhooks_Port()));
}

void webhookserver::HandleGet(AsyncWebServerRequest *request) {
    if(!TokenValid(ParamOr(request, "token"))) {
        Logger.Write("Webhooks: rejected request from " + request->client()->remoteIP().toString() + ": invalid token", logger::Warning);
        request->send(401, "application/json", "{\"error\":\"invalid token\"}");
        return;
    }

    JsonDocument doc;

    if(!request->hasParam("id")) {
        // No id: report both blinds, mirroring the web UI's /api/blinds.
        JsonArray components = doc["components"].to<JsonArray>();
        AppendBlindJson(components.add<JsonObject>(), 1, Settings.BlindL_Name(), BlindL);
        AppendBlindJson(components.add<JsonObject>(), 2, Settings.BlindR_Name(), BlindR);
    } else {
        int id = ParamOr(request, "id").toInt();
        Blinds* target = BlindByID(id);
        if(target == nullptr) { request->send(404, "application/json", "{\"error\":\"component not found\"}"); return; }
        AppendBlindJson(doc.to<JsonObject>(), id, NameByID(id), target);
    }

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
}

void webhookserver::HandleSet(AsyncWebServerRequest *request) {
    IPAddress remoteIP = request->client()->remoteIP();

    if(!TokenValid(ParamOr(request, "token"))) {
        Logger.Write("Webhooks: rejected request from " + remoteIP.toString() + ": invalid token", logger::Warning);
        request->send(401, "application/json", "{\"error\":\"invalid token\"}");
        return;
    }

    if(!request->hasParam("id") || !request->hasParam("property")) {
        request->send(400, "application/json", "{\"error\":\"missing id/property\"}");
        return;
    }

    int id = ParamOr(request, "id").toInt();
    String property = ParamOr(request, "property");
    String value = ParamOr(request, "value");

    Blinds* target = BlindByID(id);
    if(target == nullptr) { request->send(404, "application/json", "{\"error\":\"component not found\"}"); return; }

    bool accepted = true;
    if(property.equalsIgnoreCase("state")) {
        if(value.equalsIgnoreCase("open")) target->Open();
        else if(value.equalsIgnoreCase("close")) target->Close();
        else if(value.equalsIgnoreCase("stop")) target->Stop();
        else accepted = false;
    } else if(property.equalsIgnoreCase("position")) {
        int position = value.toInt();
        if(position < 0 || position > DEF_Max_Position) accepted = false;
        else target->Position((uint8_t)position);
    } else {
        accepted = false;
    }

    Logger.Write(
        "Webhooks: component set " + String(accepted ? "accepted" : "rejected") + " from " + remoteIP.toString() + ": " + NameByID(id) + "." + property + "=" + value,
        accepted ? logger::Information : logger::Warning
    );

    if(!accepted) { request->send(422, "application/json", "{\"error\":\"invalid property or value\"}"); return; }
    request->send(200, "application/json", "{\"success\":true}");
}

void webhookserver::HandleNotFound(AsyncWebServerRequest *request) {
    request->send(404, "application/json", "{\"error\":\"not found\"}");
}

webhookserver WebhookServer;
