#include "Settings.h"
#include <LittleFS.h>

namespace {
    IPAddress ParseIP(JsonObjectConst Object, const char* Key, IPAddress Fallback) {
        const char* Value = Object[Key] | "";
        IPAddress IP;
        if(strlen(Value) && IP.fromString(Value)) return IP;
        return Fallback;
    }

}

void settings::LoadDefaults() {
    mNetwork_Hostname = Defaults.Network.Hostname();
    mNetwork_SSID = Defaults.Network.SSID;
    mNetwork_Password = Defaults.Network.Passphrase;
    mNetwork_DHCPClient = Defaults.Network.DHCPClient;
    mNetwork_IPAddress = IPAddress(0, 0, 0, 0);
    mNetwork_Mask = IPAddress(0, 0, 0, 0);
    mNetwork_Gateway = IPAddress(0, 0, 0, 0);
    mNetwork_DNS1.fromString(Defaults.Network.DNS1);
    mNetwork_DNS2.fromString(Defaults.Network.DNS2);
    mNetwork_HTTP_Port = Defaults.Network.HTTPPort;
    mNetwork_ConnectionTimeout = Defaults.Network.ConnectionTimeout;
    mNetwork_ReconnectEnabled = Defaults.Network.ReconnectEnabled;
    mNetwork_ReconnectInitialInterval = Defaults.Network.ReconnectInitialInterval;
    mNetwork_ReconnectMaximumInterval = Defaults.Network.ReconnectMaximumInterval;
    mNetwork_FallbackAPEnabled = Defaults.Network.FallbackAPEnabled;
    mNetwork_FallbackAPSSID = Defaults.Network.FallbackAPSSID;
    mNetwork_FallbackAPPassword = Defaults.Network.FallbackAPPassword;
    mNetwork_FallbackAPRetention = Defaults.Network.FallbackAPRetention;

    mSyslog_Server = Defaults.Log.SyslogServer;
    mSyslog_Port = Defaults.Log.SyslogPort;
    mLog_Endpoint = Defaults.Log.Endpoint;
    mLog_Level = Defaults.Log.Level;

    mGeneral_NTPEnabled = Defaults.General.NTPEnabled;
    mGeneral_NTPServer = Defaults.General.NTPServer;
    mGeneral_TimeZone = Defaults.General.TimeZone;

    mMQTT_Enabled = Defaults.MQTT.Enabled;
    mMQTT_Broker = Defaults.MQTT.Broker;
    mMQTT_Port = Defaults.MQTT.Port;
    mMQTT_User = Defaults.MQTT.User;
    mMQTT_Password = Defaults.MQTT.Password;
    mMQTT_DiscoveryEnabled = Defaults.MQTT.DiscoveryEnabled;
    mMQTT_DiscoveryPrefix = Defaults.MQTT.DiscoveryPrefix;

    mWebhooks_Enabled = Defaults.Webhooks.Enabled;
    mWebhooks_Token = Defaults.Webhooks.Token;
    mWebhooks_Port = Defaults.Webhooks.Port;
}

void settings::AddDefaultUserAccount() {
    Users.Add(Defaults.Users.User.Username, Defaults.Users.User.Password, false);
}

bool settings::Load(const String& ConfigFileName) {
    LoadDefaults();
    LegacyBlindsSeed BlindsSeed;

    if(!LittleFS.exists(ConfigFileName)) {
        Users.Add(Defaults.Users.Admin.Username, Defaults.Users.Admin.Password, true);
        AddDefaultUserAccount();
        Save(ConfigFileName);
        EnsureDefaultComponents(ConfigFileName, BlindsSeed);
        return false;
    }

    File file = LittleFS.open(ConfigFileName, "r");
    if(!file) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if(err) return false;

    JsonObjectConst root = doc.as<JsonObjectConst>();

    JsonObjectConst network = root["Network"];
    mNetwork_Hostname = String((const char*)(network["Hostname"] | mNetwork_Hostname.c_str()));
    mNetwork_SSID = String((const char*)(network["SSID"] | Defaults.Network.SSID));
    mNetwork_Password = String((const char*)(network["Passphrase"] | Defaults.Network.Passphrase));
    mNetwork_DHCPClient = network["DHCP Client"] | Defaults.Network.DHCPClient;
    mNetwork_IPAddress = ParseIP(network, "IP Address", IPAddress(0, 0, 0, 0));
    mNetwork_Mask = ParseIP(network, "Netmask", IPAddress(0, 0, 0, 0));
    mNetwork_Gateway = ParseIP(network, "Gateway", IPAddress(0, 0, 0, 0));
    mNetwork_DNS1 = ParseIP(network, "DNS 1", mNetwork_DNS1);
    mNetwork_DNS2 = ParseIP(network, "DNS 2", mNetwork_DNS2);
    mNetwork_HTTP_Port = network["HTTP Port"] | Defaults.Network.HTTPPort;
    mNetwork_ConnectionTimeout = network["Connection Timeout"] | Defaults.Network.ConnectionTimeout;
    mNetwork_ReconnectEnabled = network["Reconnect Enabled"] | Defaults.Network.ReconnectEnabled;
    mNetwork_ReconnectInitialInterval = network["Reconnect Initial Interval"] | Defaults.Network.ReconnectInitialInterval;
    mNetwork_ReconnectMaximumInterval = network["Reconnect Maximum Interval"] | Defaults.Network.ReconnectMaximumInterval;
    mNetwork_FallbackAPEnabled = network["Fallback AP Enabled"] | Defaults.Network.FallbackAPEnabled;
    mNetwork_FallbackAPSSID = String((const char*)(network["Fallback AP SSID"] | Defaults.Network.FallbackAPSSID));
    mNetwork_FallbackAPPassword = String((const char*)(network["Fallback AP Password"] | Defaults.Network.FallbackAPPassword));
    mNetwork_FallbackAPRetention = network["Fallback AP Retention"] | Defaults.Network.FallbackAPRetention;

    // Only relevant on a config.json saved by a pre-Components firmware
    // version - used solely to seed the default Components object below,
    // not stored as live settings state anymore.
    JsonObjectConst blindL = root["Blinds"]["Left"];
    BlindsSeed.LeftName = String((const char*)(blindL["Name"] | Defaults.Components.Blinds.LeftName));
    BlindsSeed.LeftStepTime = blindL["Step Time"] | Defaults.Components.Blinds.StepTimeMs;
    BlindsSeed.LeftButtonOpen = blindL["Button Open"] | Defaults.Components.Blinds.ButtonOpen;
    BlindsSeed.LeftButtonClose = blindL["Button Close"] | Defaults.Components.Blinds.ButtonClose;
    BlindsSeed.LeftInvertButtons = blindL["Invert Buttons"] | Defaults.Components.Blinds.InvertButtons;

    JsonObjectConst blindR = root["Blinds"]["Right"];
    BlindsSeed.RightName = String((const char*)(blindR["Name"] | Defaults.Components.Blinds.RightName));
    BlindsSeed.RightStepTime = blindR["Step Time"] | Defaults.Components.Blinds.StepTimeMs;
    BlindsSeed.RightButtonOpen = blindR["Button Open"] | Defaults.Components.Blinds.ButtonOpen;
    BlindsSeed.RightButtonClose = blindR["Button Close"] | Defaults.Components.Blinds.ButtonClose;
    BlindsSeed.RightInvertButtons = blindR["Invert Buttons"] | Defaults.Components.Blinds.InvertButtons;

    // Kept only for the legacy-plaintext-admin migration below; the
    // Security_Method concept it used to also carry (Auth/WebUI/JSON/
    // Config) is gone - every page now always requires a session, like
    // DeviceIQ.
    JsonObjectConst security = root["Security"];

    if(root["Users"].is<JsonArrayConst>()) {
        for(JsonObjectConst item : root["Users"].as<JsonArrayConst>()) {
            String username = item["Username"] | "";
            bool admin = item["Admin"] | false;

            uint8_t salt[PASS_SALTLEN] = {0};
            uint8_t hash[PASS_HASHLEN] = {0};

            if(item["Salt"].is<JsonArrayConst>()) {
                size_t i = 0;
                for(JsonVariantConst v : item["Salt"].as<JsonArrayConst>()) { if(i >= PASS_SALTLEN) break; salt[i++] = v.as<uint8_t>(); }
            }
            if(item["Hash"].is<JsonArrayConst>()) {
                size_t i = 0;
                for(JsonVariantConst v : item["Hash"].as<JsonArrayConst>()) { if(i >= PASS_HASHLEN) break; hash[i++] = v.as<uint8_t>(); }
            }

            Users.AddStored(username, admin, salt, hash);
        }
    }

    if(Users.Count() == 0) {
        // Either a brand new config.json, or one saved by the firmware
        // version that still kept the admin credential in plaintext.
        const char* legacyPassword = security["Admin Password"];
        if(legacyPassword) Users.Add(security["Admin Username"] | Defaults.Users.Admin.Username, legacyPassword, true);
        else Users.Add(Defaults.Users.Admin.Username, Defaults.Users.Admin.Password, true);
        AddDefaultUserAccount();
        Save(ConfigFileName);
    }

    JsonObjectConst log = root["Log"];
    mSyslog_Server = ParseIP(log, "Syslog Server", Defaults.Log.SyslogServer);
    mSyslog_Port = log["Syslog Port"] | Defaults.Log.SyslogPort;
    if(!log["Endpoint"].isNull()) {
        mLog_Endpoint = log["Endpoint"] | Defaults.Log.Endpoint;
        mLog_Level = log["Level"] | Defaults.Log.Level;
    } else {
        // A config.json saved by a firmware version that only had a
        // "Syslog Enabled" bool - Serial logging was always implicitly on.
        bool LegacySyslogEnabled = log["Syslog Enabled"] | true;
        mLog_Endpoint = logger::Endpoints::Serial | (LegacySyslogEnabled ? logger::Endpoints::Syslog : logger::Endpoints::NoLog);
        mLog_Level = Defaults.Log.Level;
    }

    JsonObjectConst general = root["General"];
    mGeneral_NTPEnabled = general["NTP Enabled"] | Defaults.General.NTPEnabled;
    mGeneral_NTPServer = String((const char*)(general["NTP Server"] | Defaults.General.NTPServer));
    mGeneral_TimeZone = general["Time Zone"] | Defaults.General.TimeZone;

    JsonObjectConst mqtt = root["MQTT"];
    mMQTT_Enabled = mqtt["Enabled"] | Defaults.MQTT.Enabled;
    mMQTT_Broker = String((const char*)(mqtt["Broker"] | Defaults.MQTT.Broker));
    mMQTT_Port = mqtt["Port"] | Defaults.MQTT.Port;
    mMQTT_User = String((const char*)(mqtt["User"] | Defaults.MQTT.User));
    mMQTT_Password = String((const char*)(mqtt["Password"] | Defaults.MQTT.Password));
    mMQTT_DiscoveryEnabled = mqtt["Discovery Enabled"] | Defaults.MQTT.DiscoveryEnabled;
    mMQTT_DiscoveryPrefix = String((const char*)(mqtt["Discovery Prefix"] | Defaults.MQTT.DiscoveryPrefix));

    JsonObjectConst webhooks = root["Webhooks"];
    mWebhooks_Enabled = webhooks["Enabled"] | Defaults.Webhooks.Enabled;
    mWebhooks_Token = String((const char*)(webhooks["Token"] | Defaults.Webhooks.Token));
    mWebhooks_Port = webhooks["Port"] | Defaults.Webhooks.Port;

    EnsureDefaultComponents(ConfigFileName, BlindsSeed);

    return true;
}

bool settings::Save(const String& ConfigFileName) {
    // Read whatever is already on disk first, so this rewrite doesn't wipe
    // out the Components catalog - Save() only ever touches Network/Log/
    // General/MQTT/Webhooks/Users; components are installed once at boot
    // and edited independently (config import, or a future component
    // property editor), never regenerated from live state here.
    JsonDocument existingDoc;
    if(LittleFS.exists(ConfigFileName)) {
        File existingFile = LittleFS.open(ConfigFileName, "r");
        if(existingFile) {
            deserializeJson(existingDoc, existingFile);
            existingFile.close();
        }
    }

    JsonDocument doc;

    JsonObject network = doc["Network"].to<JsonObject>();
    network["Hostname"] = mNetwork_Hostname;
    network["SSID"] = mNetwork_SSID;
    network["Passphrase"] = mNetwork_Password;
    network["DHCP Client"] = mNetwork_DHCPClient;
    network["IP Address"] = mNetwork_IPAddress.toString();
    network["Netmask"] = mNetwork_Mask.toString();
    network["Gateway"] = mNetwork_Gateway.toString();
    network["DNS 1"] = mNetwork_DNS1.toString();
    network["DNS 2"] = mNetwork_DNS2.toString();
    network["HTTP Port"] = mNetwork_HTTP_Port;
    network["Connection Timeout"] = mNetwork_ConnectionTimeout;
    network["Reconnect Enabled"] = mNetwork_ReconnectEnabled;
    network["Reconnect Initial Interval"] = mNetwork_ReconnectInitialInterval;
    network["Reconnect Maximum Interval"] = mNetwork_ReconnectMaximumInterval;
    network["Fallback AP Enabled"] = mNetwork_FallbackAPEnabled;
    network["Fallback AP SSID"] = mNetwork_FallbackAPSSID;
    network["Fallback AP Password"] = mNetwork_FallbackAPPassword;
    network["Fallback AP Retention"] = mNetwork_FallbackAPRetention;

    JsonArray users = doc["Users"].to<JsonArray>();
    Users.ForEachStored([&](const String& username, bool admin, const uint8_t (&salt)[PASS_SALTLEN], const uint8_t (&hash)[PASS_HASHLEN]) {
        JsonObject item = users.add<JsonObject>();
        item["Username"] = username;
        item["Admin"] = admin;
        JsonArray saltArray = item["Salt"].to<JsonArray>();
        for(uint8_t i = 0; i < PASS_SALTLEN; i++) saltArray.add(salt[i]);
        JsonArray hashArray = item["Hash"].to<JsonArray>();
        for(uint8_t i = 0; i < PASS_HASHLEN; i++) hashArray.add(hash[i]);
    });

    JsonObject log = doc["Log"].to<JsonObject>();
    log["Syslog Server"] = mSyslog_Server.toString();
    log["Syslog Port"] = mSyslog_Port;
    log["Endpoint"] = mLog_Endpoint;
    log["Level"] = mLog_Level;

    JsonObject general = doc["General"].to<JsonObject>();
    general["NTP Enabled"] = mGeneral_NTPEnabled;
    general["NTP Server"] = mGeneral_NTPServer;
    general["Time Zone"] = mGeneral_TimeZone;

    JsonObject mqtt = doc["MQTT"].to<JsonObject>();
    mqtt["Enabled"] = mMQTT_Enabled;
    mqtt["Broker"] = mMQTT_Broker;
    mqtt["Port"] = mMQTT_Port;
    mqtt["User"] = mMQTT_User;
    mqtt["Password"] = mMQTT_Password;
    mqtt["Discovery Enabled"] = mMQTT_DiscoveryEnabled;
    mqtt["Discovery Prefix"] = mMQTT_DiscoveryPrefix;

    JsonObject webhooks = doc["Webhooks"].to<JsonObject>();
    webhooks["Enabled"] = mWebhooks_Enabled;
    webhooks["Token"] = mWebhooks_Token;
    webhooks["Port"] = mWebhooks_Port;

    // Carry the Components catalog forward untouched.
    if(!existingDoc["Components"].isNull()) {
        doc["ComponentSchemaVersion"] = existingDoc["ComponentSchemaVersion"] | 1;
        doc["Components"].set(existingDoc["Components"]);
    }

    File file = LittleFS.open(ConfigFileName, "w");
    if(!file) return false;

    bool ok = serializeJsonPretty(doc, file) > 0;
    file.close();
    return ok;
}

void settings::FactoryReset() {
    Serial.println("Reseting device to Factory settings.");
    LoadDefaults();
    Users = users();
    Users.Add(Defaults.Users.Admin.Username, Defaults.Users.Admin.Password, true);
    AddDefaultUserAccount();
    Save();
    EnsureDefaultComponents(CONFIG_FILE_NAME, LegacyBlindsSeed(), true);

    // Stale runtime state (keyed by component ID) from before the reset
    // could otherwise silently override the fresh defaults on next boot.
    LittleFS.remove(STATE_FILE_NAME);
}

void settings::CheckButtonsFactoryReset() {
    pinMode(pinsReset[0], INPUT_PULLUP);
    pinMode(pinsReset[1], INPUT_PULLUP);

    if((digitalRead(pinsReset[0]) == LOW) && (digitalRead(pinsReset[1]) == LOW)) {
        FactoryReset();
        delay(3000);
        ESP.restart();
    }
}

settings Settings;
