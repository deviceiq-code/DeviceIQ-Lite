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
    if(!pEnabled) return;

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
    // whether the position/state actually changed since last time.
    for(auto& entry : pBlindCache) entry.published = false;
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
    if(found == nullptr || !found->IsPublic() || found->Class() != component::Classes::Blinds) {
        Logger.Write("MQTT target not found: " + componentName, logger::Warning);
        return;
    }
    blinds* target = static_cast<blinds*>(found);

    String value;
    value.reserve(length);
    for(size_t i = 0; i < length; i++) value += (char)payload[i];
    value.trim();

    if(property.equalsIgnoreCase("state")) {
        if(value.equalsIgnoreCase("open")) target->Open();
        else if(value.equalsIgnoreCase("close")) target->Close();
        else if(value.equalsIgnoreCase("stop")) target->Stop();
        else Logger.Write("MQTT command rejected: " + componentName + ".state=" + value, logger::Warning);
    } else if(property.equalsIgnoreCase("position")) {
        int position = value.toInt();
        if(position < 0 || position > blinds::MAX_POSITION) {
            Logger.Write("MQTT command rejected: " + componentName + ".position=" + value, logger::Warning);
        } else {
            target->SetPosition((uint8_t)position);
        }
    } else {
        Logger.Write("MQTT command rejected: " + componentName + "." + property + " not supported", logger::Warning);
    }
}

void mqttclient::PublishStateIfChanged() {
    for(size_t i = 0; i < Components.Count() && i < ComponentManager::MAX_COMPONENTS; i++) {
        component* item = Components.At(i);
        if(item == nullptr || !item->IsPublic() || item->Class() != component::Classes::Blinds) continue;

        const blinds& target = static_cast<const blinds&>(*item);
        BlindPublishState& cache = pBlindCache[i];
        uint8_t position = target.Position();
        blinds::Motion state = target.State();

        if(!cache.published || position != cache.position || state != cache.state) {
            PublishBlindState(target.Name(), position, BlindMQTTState(state, position));
            cache.position = position;
            cache.state = state;
            cache.published = true;
        }
    }
}

void mqttclient::PublishBlindState(const String& name, uint8_t position, const char* state) {
    if(!ValidTopicSegment(name)) return;
    Publish(ComponentTopic(name, "Get", "state"), state, true);
    Publish(ComponentTopic(name, "Get", "position"), String(position), true);
}

void mqttclient::PublishDiscovery() {
    if(!pDiscoveryEnabled || !pClient.connected()) return;

    for(size_t i = 0; i < Components.Count(); i++) {
        component* item = Components.At(i);
        if(item == nullptr || !item->IsPublic() || item->Class() != component::Classes::Blinds) continue;
        const blinds& target = static_cast<const blinds&>(*item);
        if(ValidTopicSegment(target.Name())) PublishBlindDiscovery(target);
    }
}

void mqttclient::PublishBlindDiscovery(const blinds& target) {
    const String& name = target.Name();
    String unique = UniqueID("blind" + String(target.ID()) + "_cover");

    JsonDocument doc;
    doc["name"] = name;
    doc["uniq_id"] = unique;
    doc["avty_t"] = AvailabilityTopic();
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";
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
