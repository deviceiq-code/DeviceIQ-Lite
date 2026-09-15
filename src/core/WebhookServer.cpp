#include "WebhookServer.h"

#include <ArduinoJson.h>

#include "core/Settings.h"
#include "core/Logger.h"
#include "components/ComponentManager.h"
#include "components/Blinds.h"

namespace {
    void AppendBlindJson(JsonObject entry, const blinds& target) {
        entry["id"] = target.ID();
        entry["name"] = target.Name();
        entry["class"] = "Blinds";
        entry["enabled"] = target.Enabled();
        entry["state"] = blinds::StateName(target.State());
        entry["position"] = target.Position();
        entry["targetPosition"] = target.TargetPosition();
    }

    blinds* BlindByID(int id) {
        component* found = Components.FindByID((int16_t)id);
        if(found == nullptr || found->Class() != component::Classes::Blinds) return nullptr;
        return static_cast<blinds*>(found);
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
        // No id: report every registered blind, mirroring the web UI's
        // /api/blinds.
        JsonArray items = doc["components"].to<JsonArray>();
        for(size_t i = 0; i < Components.Count(); i++) {
            component* item = Components.At(i);
            if(item != nullptr && item->IsPublic() && item->Class() == component::Classes::Blinds) {
                AppendBlindJson(items.add<JsonObject>(), static_cast<const blinds&>(*item));
            }
        }
    } else {
        int id = ParamOr(request, "id").toInt();
        blinds* target = BlindByID(id);
        if(target == nullptr) { request->send(404, "application/json", "{\"error\":\"component not found\"}"); return; }
        AppendBlindJson(doc.to<JsonObject>(), *target);
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

    blinds* target = BlindByID(id);
    if(target == nullptr) { request->send(404, "application/json", "{\"error\":\"component not found\"}"); return; }

    bool accepted = true;
    if(property.equalsIgnoreCase("state")) {
        if(value.equalsIgnoreCase("open")) target->Open();
        else if(value.equalsIgnoreCase("close")) target->Close();
        else if(value.equalsIgnoreCase("stop")) target->Stop();
        else accepted = false;
    } else if(property.equalsIgnoreCase("position")) {
        int position = value.toInt();
        if(position < 0 || position > blinds::MAX_POSITION) accepted = false;
        else target->SetPosition((uint8_t)position);
    } else {
        accepted = false;
    }

    Logger.Write(
        "Webhooks: component set " + String(accepted ? "accepted" : "rejected") + " from " + remoteIP.toString() + ": " + target->Name() + "." + property + "=" + value,
        accepted ? logger::Information : logger::Warning
    );

    if(!accepted) { request->send(422, "application/json", "{\"error\":\"invalid property or value\"}"); return; }
    request->send(200, "application/json", "{\"success\":true}");
}

void webhookserver::HandleNotFound(AsyncWebServerRequest *request) {
    request->send(404, "application/json", "{\"error\":\"not found\"}");
}

webhookserver WebhookServer;
