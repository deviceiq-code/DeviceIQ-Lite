#include "MQTTClient.h"

#include "core/Settings.h"
#include "core/Logger.h"
#include "Version.h"

namespace {
    // Matches DeviceIQ's own Blinds state-name logic exactly: a blind that
    // isn't actively moving is described by where it ended up, not by the
    // fact that it happens to be stopped.
    const char* BlindMQTTState(blinds::Motion state, uint8_t position) {
        if(state == blinds::Motion::Opening) return "opening";
        if(state == blinds::Motion::Closing) return "closing";
        if(position == 0) return "closed";
        if(position == blinds::MAX_POSITION) return "open";
        return "stopped";
    }
}

mqttclient* mqttclient::pActiveInstance = nullptr;

void mqttclient::Start() {
    pEnabled = Settings.MQTT_Enabled();
    if(!pEnabled) { Logger.Write("MQTT disabled"); return; }

    pBroker = Settings.MQTT_Broker();
    pPort = Settings.MQTT_Port();
    pUser = Settings.MQTT_User();
    pPassword = Settings.MQTT_Password();
    pHostname = Settings.Network_Hostname();
    pDiscoveryEnabled = Settings.MQTT_DiscoveryEnabled();
    pDiscoveryPrefix = Settings.MQTT_DiscoveryPrefix();
    pDeviceID = DeviceIdentifier();

    if(pBroker.length() == 0 || !ValidTopicSegment(pHostname) || (pDiscoveryEnabled && !ValidTopicSegment(pDiscoveryPrefix))) {
        Logger.Write("MQTT configuration is invalid - disabling.", logger::Warning);
        pEnabled = false;
        return;
    }

    pActiveInstance = this;
    pClient.setServer(pBroker.c_str(), pPort);
    pClient.setCallback(MessageCallback);
    pClient.setKeepAlive(MQTT_KEEP_ALIVE_SECONDS);
    pClient.setSocketTimeout(MQTT_SOCKET_TIMEOUT_SECONDS);
    pClient.setBufferSize(MQTT_BUFFER_SIZE);
}

void mqttclient::Loop() {
    if(!pEnabled) return;
    if(WiFi.status() != WL_CONNECTED) return; // Broker isn't reachable from fallback AP mode anyway.

    if(!pClient.connected()) {
        unsigned long now = millis();
        if(now - pLastConnectAttemptMs >= RECONNECT_INTERVAL_MS) {
            pLastConnectAttemptMs = now;
            Connect();
        }
        return;
    }

    pClient.loop();

    if(pDiscoveryPending) {
        pDiscoveryPending = false;
        PublishDiscovery();
    }

    PublishStateIfChanged();
}

bool mqttclient::Connect() {
    String availability = AvailabilityTopic();
    bool connected = (pUser.length() == 0)
        ? pClient.connect(pDeviceID.c_str(), availability.c_str(), 0, true, "offline")
        : pClient.connect(pDeviceID.c_str(), pUser.c_str(), pPassword.c_str(), availability.c_str(), 0, true, "offline");

    if(!connected) {
        Logger.Write("MQTT connection failed (state " + String(pClient.state()) + ")", logger::Warning);
        return false;
    }

    Logger.Write("MQTT connected to " + pBroker + ":" + String(pPort));
    Publish(availability, "online", true);

    if(!pClient.subscribe((pHostname + "/+/Set/+").c_str())) {
        Logger.Write("MQTT command subscription failed", logger::Warning);
    }

    if(pDiscoveryEnabled) {
        pClient.subscribe((pDiscoveryPrefix + "/status").c_str());
        PublishDiscovery();
    }

    // A fresh connection means Home Assistant (or a broker with no
    // retained messages) has nothing to show yet - republish regardless of
    // whether the state actually changed since last time.
    for(auto& entry : pStateCache) entry.published = false;
    PublishStateIfChanged();

    return true;
}

void mqttclient::HandleMessage(const String& topic, const uint8_t* payload, size_t length) {
    if(pDiscoveryEnabled && topic == pDiscoveryPrefix + "/status") {
        String status;
        for(size_t i = 0; i < length; i++) status += (char)payload[i];
        status.trim();
        if(status.equalsIgnoreCase("online")) pDiscoveryPending = true;
        return;
    }

    if(length == 0 || length > 32 || !topic.startsWith(pHostname + "/")) return;

    String relative = topic.substring(pHostname.length() + 1);
    int first = relative.indexOf('/');
    int second = (first < 0) ? -1 : relative.indexOf('/', first + 1);
    if(first <= 0 || second <= first + 1 || relative.indexOf('/', second + 1) >= 0) return;

    String componentName = relative.substring(0, first);
    String direction = relative.substring(first + 1, second);
    String property = relative.substring(second + 1);
    if(!direction.equalsIgnoreCase("Set") || property.length() == 0) return;

    component* found = Components.FindByName(componentName);
    if(found == nullptr || !found->IsPublic()) {
        Logger.Write("MQTT target not found: " + componentName, logger::Warning);
        return;
    }

    String value;
    value.reserve(length);
    for(size_t i = 0; i < length; i++) value += (char)payload[i];
    value.trim();

    // Same property setter the Dashboard and Webhooks use, so a Relay/Blinds
    // command means the same thing regardless of which of the three sent it.
    String error;
    if(!Settings.SetComponentProperty(found->ID(), property, value, error)) {
        Logger.Write("MQTT command rejected: " + componentName + "." + property + "=" + value + ": " + error, logger::Warning);
    }
}

void mqttclient::PublishStateIfChanged() {
    for(size_t i = 0; i < Components.Count() && i < ComponentManager::MAX_COMPONENTS; i++) {
        component* item = Components.At(i);
        if(item == nullptr || !item->IsPublic() || !ValidTopicSegment(item->Name())) continue;

        PublishState& cache = pStateCache[i];

        if(item->Class() == component::Classes::Relay) {
            const relay& target = static_cast<const relay&>(*item);
            bool state = target.State();
            if(!cache.published || state != cache.relayState) {
                Publish(ComponentTopic(target.Name(), "Get", "state"), state ? "on" : "off", true);
                cache.relayState = state;
                cache.published = true;
            }
        } else if(item->Class() == component::Classes::Button) {
            const button& target = static_cast<const button&>(*item);
            bool pressed = target.IsPressed();
            if(!cache.published || pressed != cache.buttonPressed) {
                Publish(ComponentTopic(target.Name(), "Get", "state"), pressed ? "pressed" : "released", true);
                cache.buttonPressed = pressed;
                cache.published = true;
            }
        } else if(item->Class() == component::Classes::Thermometer) {
            const thermometer& target = static_cast<const thermometer&>(*item);
            bool available = target.Available();
            if(!cache.published || available != cache.thermometerAvailable) {
                Publish(ComponentTopic(target.Name(), "Get", "availability"), available ? "online" : "offline", true);
                cache.thermometerAvailable = available;
                cache.published = true;
            }
            if(available) {
                float temperature = target.Temperature();
                float humidity = target.Humidity();
                if(temperature != cache.thermometerTemperature || (target.HasHumidity() && humidity != cache.thermometerHumidity)) {
                    Publish(ComponentTopic(target.Name(), "Get", "temperature"), String(temperature, 2), true);
                    if(target.HasHumidity()) Publish(ComponentTopic(target.Name(), "Get", "humidity"), String(humidity, 2), true);
                    cache.thermometerTemperature = temperature;
                    cache.thermometerHumidity = humidity;
                }
            }
        } else if(item->Class() == component::Classes::Blinds) {
            const blinds& target = static_cast<const blinds&>(*item);
            uint8_t position = target.Position();
            blinds::Motion state = target.State();
            if(!cache.published || position != cache.blindPosition || state != cache.blindState) {
                Publish(ComponentTopic(target.Name(), "Get", "state"), BlindMQTTState(state, position), true);
                Publish(ComponentTopic(target.Name(), "Get", "position"), String(position), true);
                cache.blindPosition = position;
                cache.blindState = state;
                cache.published = true;
            }
        }
    }
}

void mqttclient::PublishDiscovery() {
    if(!pDiscoveryEnabled || !pClient.connected()) return;

    for(size_t i = 0; i < Components.Count(); i++) {
        component* item = Components.At(i);
        if(item == nullptr || !item->IsPublic() || !ValidTopicSegment(item->Name())) continue;

        if(item->Class() == component::Classes::Relay) PublishRelayDiscovery(static_cast<const relay&>(*item));
        else if(item->Class() == component::Classes::Button) PublishButtonDiscovery(static_cast<const button&>(*item));
        else if(item->Class() == component::Classes::Thermometer) PublishThermometerDiscovery(static_cast<const thermometer&>(*item));
        else if(item->Class() == component::Classes::Blinds) PublishBlindDiscovery(static_cast<const blinds&>(*item));
    }
}

void mqttclient::AddDiscoveryMetadata(JsonDocument& doc, const String& name, const String& uniqueId, bool includeAvailability) {
    doc["name"] = name;
    doc["uniq_id"] = uniqueId;
    if(includeAvailability) {
        doc["avty_t"] = AvailabilityTopic();
        doc["pl_avail"] = "online";
        doc["pl_not_avail"] = "offline";
    }

    JsonObject device = doc["dev"].to<JsonObject>();
    device["ids"] = pDeviceID;
    device["name"] = pHostname;
    device["mf"] = Version::ProductFamily;
    device["mdl"] = Version::ProductName;
    device["sw"] = Version::Software::Info();
    device["hw"] = Version::Hardware::Info();
    device["sn"] = Version::SerialNumber();

    JsonObject origin = doc["o"].to<JsonObject>();
    origin["name"] = Version::ProductFamily;
    origin["sw"] = Version::Software::Info();
}

void mqttclient::PublishRelayDiscovery(const relay& target) {
    const String& name = target.Name();
    String unique = UniqueID("relay" + String(target.ID()) + "_switch");

    JsonDocument doc;
    AddDiscoveryMetadata(doc, name, unique);
    doc["cmd_t"] = ComponentTopic(name, "Set", "state");
    doc["stat_t"] = ComponentTopic(name, "Get", "state");
    doc["pl_on"] = "on";
    doc["pl_off"] = "off";
    doc["stat_on"] = "on";
    doc["stat_off"] = "off";

    String payload;
    if(serializeJson(doc, payload) > 0) Publish(pDiscoveryPrefix + "/switch/" + unique + "/config", payload, true);
}

void mqttclient::PublishButtonDiscovery(const button& target) {
    const String& name = target.Name();
    String unique = UniqueID("button" + String(target.ID()) + "_state");

    JsonDocument doc;
    AddDiscoveryMetadata(doc, name, unique);
    doc["stat_t"] = ComponentTopic(name, "Get", "state");
    doc["pl_on"] = "pressed";
    doc["pl_off"] = "released";

    String payload;
    if(serializeJson(doc, payload) > 0) Publish(pDiscoveryPrefix + "/binary_sensor/" + unique + "/config", payload, true);
}

void mqttclient::AddThermometerAvailability(JsonDocument& doc, const String& name) {
    JsonArray availability = doc["avty"].to<JsonArray>();
    JsonObject device = availability.add<JsonObject>();
    device["t"] = AvailabilityTopic();
    device["pl_avail"] = "online";
    device["pl_not_avail"] = "offline";
    JsonObject sensor = availability.add<JsonObject>();
    sensor["t"] = ComponentTopic(name, "Get", "availability");
    sensor["pl_avail"] = "online";
    sensor["pl_not_avail"] = "offline";
    doc["avty_mode"] = "all";
}

void mqttclient::PublishThermometerDiscovery(const thermometer& target) {
    const String& name = target.Name();

    String temperatureUnique = UniqueID("thermometer" + String(target.ID()) + "_temperature");
    JsonDocument temperatureDoc;
    AddDiscoveryMetadata(temperatureDoc, name + " Temperature", temperatureUnique, false);
    temperatureDoc["stat_t"] = ComponentTopic(name, "Get", "temperature");
    temperatureDoc["dev_cla"] = "temperature";
    temperatureDoc["unit_of_meas"] = "°C";
    temperatureDoc["stat_cla"] = "measurement";
    temperatureDoc["sug_dsp_prc"] = 1;
    AddThermometerAvailability(temperatureDoc, name);
    String temperaturePayload;
    if(serializeJson(temperatureDoc, temperaturePayload) > 0) {
        Publish(pDiscoveryPrefix + "/sensor/" + temperatureUnique + "/config", temperaturePayload, true);
    }

    if(!target.HasHumidity()) return;

    String humidityUnique = UniqueID("thermometer" + String(target.ID()) + "_humidity");
    JsonDocument humidityDoc;
    AddDiscoveryMetadata(humidityDoc, name + " Humidity", humidityUnique, false);
    humidityDoc["stat_t"] = ComponentTopic(name, "Get", "humidity");
    humidityDoc["dev_cla"] = "humidity";
    humidityDoc["unit_of_meas"] = "%";
    humidityDoc["stat_cla"] = "measurement";
    humidityDoc["sug_dsp_prc"] = 1;
    AddThermometerAvailability(humidityDoc, name);
    String humidityPayload;
    if(serializeJson(humidityDoc, humidityPayload) > 0) {
        Publish(pDiscoveryPrefix + "/sensor/" + humidityUnique + "/config", humidityPayload, true);
    }
}

void mqttclient::PublishBlindDiscovery(const blinds& target) {
    const String& name = target.Name();
    String unique = UniqueID("blind" + String(target.ID()) + "_cover");

    JsonDocument doc;
    AddDiscoveryMetadata(doc, name, unique);
    doc["dev_cla"] = "blind";
    doc["cmd_t"] = ComponentTopic(name, "Set", "state");
    doc["stat_t"] = ComponentTopic(name, "Get", "state");
    doc["pos_t"] = ComponentTopic(name, "Get", "position");
    doc["set_pos_t"] = ComponentTopic(name, "Set", "position");
    doc["pos_clsd"] = 0;
    doc["pos_open"] = 100;
    doc["pl_open"] = "open";
    doc["pl_cls"] = "close";
    doc["pl_stop"] = "stop";
    doc["stat_clsd"] = "closed";
    doc["stat_open"] = "open";
    doc["stat_opening"] = "opening";
    doc["stat_closing"] = "closing";
    doc["stat_stopped"] = "stopped";

    String payload;
    if(serializeJson(doc, payload) > 0) Publish(pDiscoveryPrefix + "/cover/" + unique + "/config", payload, true);
}

bool mqttclient::Publish(const String& topic, const String& payload, bool retained) {
    if(!pClient.connected() || topic.length() == 0) return false;
    if(pClient.publish(topic.c_str(), payload.c_str(), retained)) return true;
    Logger.Write("MQTT publish failed: " + topic, logger::Warning);
    return false;
}

String mqttclient::ComponentTopic(const String& name, const char* direction, const char* property) const {
    return pHostname + "/" + name + "/" + direction + "/" + property;
}

String mqttclient::AvailabilityTopic() const {
    return pHostname + "/Online";
}

String mqttclient::UniqueID(const String& suffix) const {
    return pDeviceID + "_" + suffix;
}

bool mqttclient::ValidTopicSegment(const String& value) {
    return value.length() > 0 && value.indexOf('/') < 0 && value.indexOf('+') < 0 && value.indexOf('#') < 0;
}

String mqttclient::DeviceIdentifier() {
    String mac = WiFi.macAddress();
    mac.toLowerCase();
    mac.replace(":", "");
    return "deviceiqlite_" + mac;
}

void mqttclient::MessageCallback(char* topic, uint8_t* payload, unsigned int length) {
    if(pActiveInstance != nullptr && topic != nullptr) pActiveInstance->HandleMessage(String(topic), payload, length);
}

mqttclient MQTTClient;
