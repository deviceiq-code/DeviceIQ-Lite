#include "WebhookServer.h"

#include <ArduinoJson.h>

#include "core/Settings.h"
#include "core/Logger.h"
#include "components/ComponentManager.h"
#include "components/Relay.h"
#include "components/Button.h"
#include "components/Thermometer.h"
#include "components/Blinds.h"

namespace {
    // Mirrors main.cpp's "/api/components" GET per-class field logic, so a
    // Webhooks client sees exactly the same shape the Dashboard does.
    void AppendComponentJson(JsonObject entry, const component& item) {
        entry["id"] = item.ID();
        entry["name"] = item.Name();
        entry["class"] = component::ClassName(item.Class());
        entry["enabled"] = item.Enabled();

        if(item.Class() == component::Classes::Relay) {
            entry["state"] = static_cast<const relay&>(item).State();
        } else if(item.Class() == component::Classes::Button) {
            entry["pressed"] = static_cast<const button&>(item).IsPressed();
        } else if(item.Class() == component::Classes::Thermometer) {
            const thermometer& sensor = static_cast<const thermometer&>(item);
            entry["available"] = sensor.Available();
            if(sensor.Available()) entry["temperature"] = sensor.Temperature();
            if(sensor.Available() && sensor.HasHumidity()) entry["humidity"] = sensor.Humidity();
        } else if(item.Class() == component::Classes::Blinds) {
            const blinds& target = static_cast<const blinds&>(item);
            entry["state"] = blinds::StateName(target.State());
            entry["position"] = target.Position();
            entry["targetPosition"] = target.TargetPosition();
        }
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
    if(pServer != nullptr) return;
    if(!Settings.Webhooks_Enabled()) { Logger.Write("Webhooks: Disabled"); return; }

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
        // No id: report every public component, mirroring the web UI's
        // /api/components.
        JsonArray items = doc["components"].to<JsonArray>();
        for(size_t i = 0; i < Components.Count(); i++) {
            component* item = Components.At(i);
            if(item != nullptr && item->IsPublic()) AppendComponentJson(items.add<JsonObject>(), *item);
        }
    } else {
        int id = ParamOr(request, "id").toInt();
        component* target = Components.FindByID((int16_t)id);
        if(target == nullptr || !target->IsPublic()) { request->send(404, "application/json", "{\"error\":\"component not found\"}"); return; }
        AppendComponentJson(doc.to<JsonObject>(), *target);
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

    int16_t id = (int16_t)ParamOr(request, "id").toInt();
    String property = ParamOr(request, "property");
    String value = ParamOr(request, "value");

    component* target = Components.FindByID(id);
    if(target == nullptr || !target->IsPublic()) { request->send(404, "application/json", "{\"error\":\"component not found\"}"); return; }

    String targetName = target->Name();
    String error;
    bool accepted = Settings.SetComponentProperty(id, property, value, error);

    Logger.Write(
        "Webhooks: component set " + String(accepted ? "accepted" : "rejected") + " from " + remoteIP.toString() + ": " + targetName + "." + property + "=" + value,
        accepted ? logger::Information : logger::Warning
    );

    if(!accepted) { request->send(422, "application/json", "{\"error\":\"" + error + "\"}"); return; }
    request->send(200, "application/json", "{\"success\":true}");
}

void webhookserver::HandleNotFound(AsyncWebServerRequest *request) {
    request->send(404, "application/json", "{\"error\":\"not found\"}");
}

webhookserver WebhookServer;
