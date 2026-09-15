#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "IPAddress.h"
#include "core/Defaults.h"
#include "core/Users.h"

const uint8_t pinsReset[2] = { 5, 4 }; // Two pins must be used temporarily

const char* const CONFIG_FILE_NAME = "/config.json";
// Staging path for an uploaded config.json between /api/config/import
// (validates, writes here) and /api/config/import/apply (commits it).
const char* const CONFIG_IMPORT_FILE_NAME = "/config.import.json";

// Live, frequently-changing component state (currently Relay.State and
// Blinds.Position), kept out of config.json and regenerated from scratch on
// every save - same split DeviceIQ uses between config.json and state.json.
// No EEPROM anywhere in Lite: LittleFS handles wear-leveling on its own,
// unlike the single fixed sector the ESP8266 EEPROM emulation writes to.
const char* const STATE_FILE_NAME = "/state.json";

// How often loop() checks whether any component's persisted state changed
// and, if so, writes state.json - not on every single change (which for a
// moving Blinds would mean a write per 1% step), matching DeviceIQ's
// default Settings.General.SaveStatePooling of 20 seconds (fixed here
// rather than user-configurable).
const uint32_t STATE_SAVE_INTERVAL_MS = 20000;

// Blind-specific values recovered from a pre-Components config.json during
// migration, used only once to seed the default Components object - see
// settings::EnsureDefaultComponents() in ComponentConfig.cpp. Not part of
// the live settings state.
struct LegacyBlindsSeed {
    String LeftName = Defaults.Components.Blinds.LeftName;
    String RightName = Defaults.Components.Blinds.RightName;
    uint16_t LeftStepTime = Defaults.Components.Blinds.StepTimeMs;
    uint16_t RightStepTime = Defaults.Components.Blinds.StepTimeMs;
    bool LeftButtonOpen = Defaults.Components.Blinds.ButtonOpen;
    bool LeftButtonClose = Defaults.Components.Blinds.ButtonClose;
    bool LeftInvertButtons = Defaults.Components.Blinds.InvertButtons;
    bool RightButtonOpen = Defaults.Components.Blinds.ButtonOpen;
    bool RightButtonClose = Defaults.Components.Blinds.ButtonClose;
    bool RightInvertButtons = Defaults.Components.Blinds.InvertButtons;
};

class settings {
    private:
        // Writes the default Components object (matching Lite's fixed PCB
        // wiring - two Blinds, pins as in main.cpp's old hardcoded
        // constructions) into ConfigFileName, unless a valid Components
        // object is already present (or Force is true, used by
        // FactoryReset()). Implemented in ComponentConfig.cpp.
        bool EnsureDefaultComponents(const String& ConfigFileName, const LegacyBlindsSeed& Seed, bool Force = false);

        void LoadDefaults();
        // Adds DeviceIQ's second default account (non-admin "user"). Called
        // alongside every place that bootstraps the default admin, so a
        // fresh or migrated device ends up with the same two accounts
        // DeviceIQ itself ships with.
        void AddDefaultUserAccount();

        // mNetwork_Hostname intentionally has no default-member-initializer:
        // Defaults.Network.Hostname() reads the WiFi MAC address, which
        // isn't reliably available yet this early (Settings is a global, so
        // this would run during static initialization, before setup() -
        // and before the SDK's own WiFi init). LoadDefaults() assigns it at
        // actual runtime instead.
        String mNetwork_Hostname;
        String mNetwork_SSID = Defaults.Network.SSID;
        String mNetwork_Password = Defaults.Network.Passphrase;
        bool mNetwork_DHCPClient = Defaults.Network.DHCPClient;
        IPAddress mNetwork_IPAddress;
        IPAddress mNetwork_Mask;
        IPAddress mNetwork_Gateway;
        IPAddress mNetwork_DNS1;
        IPAddress mNetwork_DNS2;
        uint16_t mNetwork_HTTP_Port = Defaults.Network.HTTPPort;
        uint16_t mNetwork_ConnectionTimeout = Defaults.Network.ConnectionTimeout;
        bool mNetwork_ReconnectEnabled = Defaults.Network.ReconnectEnabled;
        uint16_t mNetwork_ReconnectInitialInterval = Defaults.Network.ReconnectInitialInterval;
        uint16_t mNetwork_ReconnectMaximumInterval = Defaults.Network.ReconnectMaximumInterval;
        bool mNetwork_FallbackAPEnabled = Defaults.Network.FallbackAPEnabled;
        String mNetwork_FallbackAPSSID = Defaults.Network.FallbackAPSSID;
        String mNetwork_FallbackAPPassword = Defaults.Network.FallbackAPPassword;
        uint16_t mNetwork_FallbackAPRetention = Defaults.Network.FallbackAPRetention;
        bool mAP_Mode = false;

        // Hostname or a plain IP, resolved at send time by Logger's own
        // ResolveSyslogAddress() - not restricted to dotted-decimal like an
        // IPAddress, so a typed hostname is never silently dropped on save.
        String mSyslog_Server = Defaults.Log.SyslogServer;
        uint16_t mSyslog_Port = Defaults.Log.SyslogPort;
        uint8_t mLog_Endpoint = Defaults.Log.Endpoint;
        uint8_t mLog_Level = Defaults.Log.Level;

        bool mGeneral_NTPEnabled = Defaults.General.NTPEnabled;
        String mGeneral_NTPServer = Defaults.General.NTPServer;
        int8_t mGeneral_TimeZone = Defaults.General.TimeZone;

        bool mMQTT_Enabled = Defaults.MQTT.Enabled;
        String mMQTT_Broker = Defaults.MQTT.Broker;
        uint16_t mMQTT_Port = Defaults.MQTT.Port;
        String mMQTT_User = Defaults.MQTT.User;
        String mMQTT_Password = Defaults.MQTT.Password;
        bool mMQTT_DiscoveryEnabled = Defaults.MQTT.DiscoveryEnabled;
        String mMQTT_DiscoveryPrefix = Defaults.MQTT.DiscoveryPrefix;

        bool mWebhooks_Enabled = Defaults.Webhooks.Enabled;
        String mWebhooks_Token = Defaults.Webhooks.Token;
        uint16_t mWebhooks_Port = Defaults.Webhooks.Port;

    public:
        settings() {}
        ~settings() {}

        users Users;

        inline String Network_Hostname() { return mNetwork_Hostname; }
        inline void Network_Hostname(String Name) { Name.replace(" ", ""); mNetwork_Hostname = Name.substring(0, 32); }
        inline String Network_SSID() { return mNetwork_SSID; }
        inline void Network_SSID(String SSID) { mNetwork_SSID = SSID.substring(0, 32); }
        inline String Network_Password() { return mNetwork_Password; }
        inline void Network_Password(String Password) { mNetwork_Password = Password.substring(0, 64); }
        inline bool Network_DHCPClient() { return mNetwork_DHCPClient; }
        inline void Network_DHCPClient(bool Value) { mNetwork_DHCPClient = Value; }
        inline IPAddress Network_IPAddress() { return mNetwork_IPAddress; }
        inline void Network_IPAddress(IPAddress IP) { mNetwork_IPAddress = IP; }
        inline IPAddress Network_Mask() { return mNetwork_Mask; }
        inline void Network_Mask(IPAddress Mask) { mNetwork_Mask = Mask; }
        inline IPAddress Network_Gateway() { return mNetwork_Gateway; }
        inline void Network_Gateway(IPAddress Gateway) { mNetwork_Gateway = Gateway; }
        inline IPAddress Network_DNS1() { return mNetwork_DNS1; }
        inline void Network_DNS1(IPAddress DNS) { mNetwork_DNS1 = DNS; }
        inline IPAddress Network_DNS2() { return mNetwork_DNS2; }
        inline void Network_DNS2(IPAddress DNS) { mNetwork_DNS2 = DNS; }
        inline uint16_t Network_HTTP_Port() { return mNetwork_HTTP_Port; }
        inline void Network_HTTP_Port(uint16_t Port) { mNetwork_HTTP_Port = Port; }
        inline uint16_t Network_ConnectionTimeout() { return mNetwork_ConnectionTimeout; }
        inline void Network_ConnectionTimeout(uint16_t Seconds) { mNetwork_ConnectionTimeout = Seconds; }
        inline bool Network_ReconnectEnabled() { return mNetwork_ReconnectEnabled; }
        inline void Network_ReconnectEnabled(bool Value) { mNetwork_ReconnectEnabled = Value; }
        inline uint16_t Network_ReconnectInitialInterval() { return mNetwork_ReconnectInitialInterval; }
        inline void Network_ReconnectInitialInterval(uint16_t Seconds) { mNetwork_ReconnectInitialInterval = Seconds; }
        inline uint16_t Network_ReconnectMaximumInterval() { return mNetwork_ReconnectMaximumInterval; }
        inline void Network_ReconnectMaximumInterval(uint16_t Seconds) { mNetwork_ReconnectMaximumInterval = Seconds; }
        inline bool Network_FallbackAPEnabled() { return mNetwork_FallbackAPEnabled; }
        inline void Network_FallbackAPEnabled(bool Value) { mNetwork_FallbackAPEnabled = Value; }
        inline String Network_FallbackAPSSID() { return mNetwork_FallbackAPSSID; }
        inline void Network_FallbackAPSSID(String SSID) { mNetwork_FallbackAPSSID = SSID.substring(0, 32); }
        inline String Network_FallbackAPPassword() { return mNetwork_FallbackAPPassword; }
        inline void Network_FallbackAPPassword(String Password) { mNetwork_FallbackAPPassword = Password.substring(0, 64); }
        inline uint16_t Network_FallbackAPRetention() { return mNetwork_FallbackAPRetention; }
        inline void Network_FallbackAPRetention(uint16_t Seconds) { mNetwork_FallbackAPRetention = Seconds; }
        inline bool AP_Mode() { return mAP_Mode; }
        inline void AP_Mode(bool Value) { mAP_Mode = Value; }

        inline String Syslog_Server() { return mSyslog_Server; }
        // Hostname or a plain IP, resolved at send time (Logger's own
        // ResolveSyslogAddress()) - not restricted to dotted-decimal like an
        // IPAddress, so a typed hostname is never silently dropped on save.
        inline void Syslog_Server(String Server) { mSyslog_Server = Server.substring(0, 64); }
        inline uint16_t Syslog_Port() { return mSyslog_Port; }
        inline void Syslog_Port(uint16_t Port) { mSyslog_Port = Port; }
        inline uint8_t Log_Endpoint() { return mLog_Endpoint; }
        inline void Log_Endpoint(uint8_t Value) { mLog_Endpoint = Value; }
        inline uint8_t Log_Level() { return mLog_Level; }
        inline void Log_Level(uint8_t Value) { mLog_Level = Value; }

        inline bool General_NTPEnabled() { return mGeneral_NTPEnabled; }
        inline void General_NTPEnabled(bool Value) { mGeneral_NTPEnabled = Value; }
        inline String General_NTPServer() { return mGeneral_NTPServer; }
        inline void General_NTPServer(String Server) { mGeneral_NTPServer = Server.substring(0, 64); }
        inline int8_t General_TimeZone() { return mGeneral_TimeZone; }
        inline void General_TimeZone(int8_t Value) { mGeneral_TimeZone = constrain(Value, -12, 14); }

        inline bool MQTT_Enabled() { return mMQTT_Enabled; }
        inline void MQTT_Enabled(bool Value) { mMQTT_Enabled = Value; }
        inline String MQTT_Broker() { return mMQTT_Broker; }
        inline void MQTT_Broker(String Broker) { mMQTT_Broker = Broker; }
        inline uint16_t MQTT_Port() { return mMQTT_Port; }
        inline void MQTT_Port(uint16_t Port) { mMQTT_Port = Port; }
        inline String MQTT_User() { return mMQTT_User; }
        inline void MQTT_User(String User) { mMQTT_User = User; }
        inline String MQTT_Password() { return mMQTT_Password; }
        inline void MQTT_Password(String Password) { mMQTT_Password = Password; }
        inline bool MQTT_DiscoveryEnabled() { return mMQTT_DiscoveryEnabled; }
        inline void MQTT_DiscoveryEnabled(bool Value) { mMQTT_DiscoveryEnabled = Value; }
        inline String MQTT_DiscoveryPrefix() { return mMQTT_DiscoveryPrefix; }
        inline void MQTT_DiscoveryPrefix(String Prefix) { mMQTT_DiscoveryPrefix = Prefix; }

        inline bool Webhooks_Enabled() { return mWebhooks_Enabled; }
        inline void Webhooks_Enabled(bool Value) { mWebhooks_Enabled = Value; }
        inline String Webhooks_Token() { return mWebhooks_Token; }
        inline void Webhooks_Token(String Token) { mWebhooks_Token = Token; }
        inline uint16_t Webhooks_Port() { return mWebhooks_Port; }
        inline void Webhooks_Port(uint16_t Port) { mWebhooks_Port = Port; }

        bool Load(const String& ConfigFileName = CONFIG_FILE_NAME);
        bool Save(const String& ConfigFileName = CONFIG_FILE_NAME);

        // Builds the dynamic component set (Relay/Button/Thermometer/Blinds)
        // from the "Components" object in config.json and registers it with
        // the global ComponentManager. Implemented in ComponentConfig.cpp.
        // Must run after Load() and before ComponentManager::Start().
        bool InstallComponents(const String& ConfigFileName = CONFIG_FILE_NAME);

        // Writes state.json from every public component's live persisted
        // state (Relay.State, Blinds.Position) - called from loop() at most
        // once every STATE_SAVE_INTERVAL_MS, and only when
        // ComponentManager::PersistenceRequired() says something actually
        // changed. Implemented in ComponentConfig.cpp.
        bool SaveComponentsState(const String& StateFileName = STATE_FILE_NAME);

        // Applies a property change to a live component (Enabled for any
        // class; StepTimeMs/ButtonOpenEnabled/ButtonCloseEnabled/
        // InvertButtons for Blinds) and persists it into config.json so it
        // survives a reboot. Backs POST /api/components. Implemented in
        // ComponentConfig.cpp.
        bool SetComponentProperty(int16_t ID, const String& Property, const String& Value, String& Error);

        void FactoryReset();
        void CheckButtonsFactoryReset();
};

extern settings Settings;
