#pragma once

#include <Arduino.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include "IPAddress.h"
#include "core/Defaults.h"
#include "core/Users.h"

const uint8_t pinsReset[2] = { 5, 4 }; // Two pins must be used temporarily

const char* const CONFIG_FILE_NAME = "/config.json";
// Staging path for an uploaded config.json between /api/config/import
// (validates, writes here) and /api/config/import/apply (commits it).
const char* const CONFIG_IMPORT_FILE_NAME = "/config.import.json";

// Legacy EEPROM layout, used only to migrate preferences saved by firmware
// versions prior to the switch to config.json. Blind position (the only
// value still persisted to EEPROM, since it changes far too often for a
// flash-backed file) keeps living at its historical addresses below.
const uint16_t EEPROM_ADDR_WIFI_SSID = 0;
const uint16_t EEPROM_SIZE_WIFI_SSID = 32;
const uint16_t EEPROM_ADDR_WIFI_PASSWORD = EEPROM_SIZE_WIFI_SSID; // 32
const uint16_t EEPROM_SIZE_WIFI_PASSWORD = 64;
const uint16_t EEPROM_ADDR_WIFI_DHCPCLIENT = EEPROM_ADDR_WIFI_PASSWORD + EEPROM_SIZE_WIFI_PASSWORD; // 96
const uint16_t EEPROM_SIZE_WIFI_DHCPCLIENT = 1;
const uint16_t EEPROM_ADDR_WIFI_IPADDRESS = EEPROM_ADDR_WIFI_DHCPCLIENT + EEPROM_SIZE_WIFI_DHCPCLIENT; // 97
const uint16_t EEPROM_SIZE_WIFI_IPADDRESS = 4;
const uint16_t EEPROM_ADDR_WIFI_MASK = EEPROM_ADDR_WIFI_IPADDRESS + EEPROM_SIZE_WIFI_IPADDRESS; // 101
const uint16_t EEPROM_SIZE_WIFI_MASK = 4;
const uint16_t EEPROM_ADDR_WIFI_GATEWAY = EEPROM_ADDR_WIFI_MASK + EEPROM_SIZE_WIFI_MASK; // 101
const uint16_t EEPROM_SIZE_WIFI_GATEWAY = 4;
const uint16_t EEPROM_ADDR_WIFI_HTTP_PORT = EEPROM_ADDR_WIFI_GATEWAY + EEPROM_SIZE_WIFI_GATEWAY; // 105
const uint16_t EEPROM_SIZE_WIFI_HTTP_PORT = 2;
const uint16_t EEPROM_ADDR_BLINDL_STEPTIME = EEPROM_ADDR_WIFI_HTTP_PORT + EEPROM_SIZE_WIFI_HTTP_PORT; // 109
const uint16_t EEPROM_SIZE_BLINDL_STEPTIME = 2;
const uint16_t EEPROM_ADDR_BLINDR_STEPTIME = EEPROM_ADDR_BLINDL_STEPTIME + EEPROM_SIZE_BLINDL_STEPTIME; // 111
const uint16_t EEPROM_SIZE_BLINDR_STEPTIME = 2;
const uint16_t EEPROM_ADDR_ROOM_NAME = EEPROM_ADDR_BLINDR_STEPTIME + EEPROM_SIZE_BLINDR_STEPTIME; // 113
const uint16_t EEPROM_SIZE_ROOM_NAME = 32;
const uint16_t EEPROM_ADDR_BLINDL_NAME = EEPROM_ADDR_ROOM_NAME + EEPROM_SIZE_ROOM_NAME; // 113
const uint16_t EEPROM_SIZE_BLINDL_NAME = 32;
const uint16_t EEPROM_ADDR_BLINDR_NAME = EEPROM_ADDR_BLINDL_NAME + EEPROM_SIZE_BLINDL_NAME; // 145
const uint16_t EEPROM_SIZE_BLINDR_NAME = 32;
const uint16_t EEPROM_ADDR_SECURITY_AUTH = EEPROM_ADDR_BLINDR_NAME + EEPROM_SIZE_BLINDR_NAME; // 177
const uint16_t EEPROM_SIZE_SECURITY_AUTH = 1;
const uint16_t EEPROM_ADDR_SECURITY_WEBUI = EEPROM_ADDR_SECURITY_AUTH + EEPROM_SIZE_SECURITY_AUTH; // 178
const uint16_t EEPROM_SIZE_SECURITY_WEBUI = 1;
const uint16_t EEPROM_ADDR_SECURITY_JSON = EEPROM_ADDR_SECURITY_WEBUI + EEPROM_SIZE_SECURITY_WEBUI; // 179
const uint16_t EEPROM_SIZE_SECURITY_JSON = 1;
const uint16_t EEPROM_ADDR_SECURITY_CONFIG = EEPROM_ADDR_SECURITY_JSON + EEPROM_SIZE_SECURITY_JSON; // 180
const uint16_t EEPROM_SIZE_SECURITY_CONFIG = 1;
const uint16_t EEPROM_ADDR_SECURITY_ADMINPASSWORD = EEPROM_ADDR_SECURITY_CONFIG + EEPROM_SIZE_SECURITY_CONFIG; // 181
const uint16_t EEPROM_SIZE_SECURITY_ADMINPASSWORD = 32;
const uint16_t EEPROM_ADDR_BLINDL_POSITION = EEPROM_ADDR_SECURITY_ADMINPASSWORD + EEPROM_SIZE_SECURITY_ADMINPASSWORD; // 213
const uint16_t EEPROM_SIZE_BLINDL_POSITION = 1;
const uint16_t EEPROM_ADDR_BLINDR_POSITION = EEPROM_ADDR_BLINDL_POSITION + EEPROM_SIZE_BLINDL_POSITION; // 214
const uint16_t EEPROM_SIZE_BLINDR_POSITION = 1;
const uint16_t EEPROM_ADDR_BLINDL_BUTTONOPEN = EEPROM_ADDR_BLINDR_POSITION + EEPROM_SIZE_BLINDR_POSITION; // 215
const uint16_t EEPROM_SIZE_BLINDL_BUTTONOPEN = 1;
const uint16_t EEPROM_ADDR_BLINDR_BUTTONOPEN = EEPROM_ADDR_BLINDL_BUTTONOPEN + EEPROM_SIZE_BLINDL_BUTTONOPEN; // 216
const uint16_t EEPROM_SIZE_BLINDR_BUTTONOPEN = 1;
const uint16_t EEPROM_ADDR_BLINDL_BUTTONCLOSE = EEPROM_ADDR_BLINDR_BUTTONOPEN + EEPROM_SIZE_BLINDR_BUTTONOPEN; // 217
const uint16_t EEPROM_SIZE_BLINDL_BUTTONCLOSE = 1;
const uint16_t EEPROM_ADDR_BLINDR_BUTTONCLOSE = EEPROM_ADDR_BLINDL_BUTTONCLOSE + EEPROM_SIZE_BLINDL_BUTTONCLOSE; // 218
const uint16_t EEPROM_SIZE_BLINDR_BUTTONCLOSE = 1;
const uint16_t EEPROM_ADDR_BLINDL_INVBUTTON = EEPROM_ADDR_BLINDR_BUTTONCLOSE + EEPROM_SIZE_BLINDR_BUTTONCLOSE; // 219
const uint16_t EEPROM_SIZE_BLINDL_INVBUTTON = 1;
const uint16_t EEPROM_ADDR_BLINDR_INVBUTTON = EEPROM_ADDR_BLINDL_INVBUTTON + EEPROM_SIZE_BLINDL_INVBUTTON; // 220
const uint16_t EEPROM_SIZE_BLINDR_INVBUTTON = 1;
const uint16_t EEPROM_ADDR_SYSLOG_SERVER = EEPROM_ADDR_BLINDR_INVBUTTON + EEPROM_SIZE_BLINDR_INVBUTTON; // 221
const uint16_t EEPROM_SIZE_SYSLOG_SERVER = 4;
const uint16_t EEPROM_ADDR_SYSLOG_PORT = EEPROM_ADDR_SYSLOG_SERVER + EEPROM_SIZE_SYSLOG_SERVER; // 225
const uint16_t EEPROM_SIZE_SYSLOG_PORT = 2;
const uint16_t EEPROM_ADDR_SYSLOG_ENABLED = EEPROM_ADDR_SYSLOG_PORT + EEPROM_SIZE_SYSLOG_PORT; // 227
const uint16_t EEPROM_SIZE_SYSLOG_ENABLED = 1;

class settings {
    private:
        // Legacy EEPROM readers, used once by LoadLegacyEEPROM() to migrate
        // devices that were still on the pre-config.json firmware.
        String LegacyReadString(uint16_t Address);
        bool LegacyReadBool(uint16_t Address);
        int8_t LegacyReadInt8(uint16_t Address);
        int16_t LegacyReadInt16(uint16_t Address);
        IPAddress LegacyReadIP(uint16_t Address);
        bool LoadLegacyEEPROM();

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

        uint16_t mBlindL_StepTime = Defaults.Components.Blinds.StepTimeMs, mBlindR_StepTime = Defaults.Components.Blinds.StepTimeMs;
        String mBlindL_Name = Defaults.Components.Blinds.LeftName, mBlindR_Name = Defaults.Components.Blinds.RightName;
        bool mBlindL_ButtonOpen = Defaults.Components.Blinds.ButtonOpen, mBlindR_ButtonOpen = Defaults.Components.Blinds.ButtonOpen;
        bool mBlindL_ButtonClose = Defaults.Components.Blinds.ButtonClose, mBlindR_ButtonClose = Defaults.Components.Blinds.ButtonClose;
        bool mBlindL_InvertButtons = Defaults.Components.Blinds.InvertButtons, mBlindR_InvertButtons = Defaults.Components.Blinds.InvertButtons;

        IPAddress mSyslog_Server = Defaults.Log.SyslogServer;
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

        inline uint16_t BlindL_StepTime() { return mBlindL_StepTime; }
        inline uint16_t BlindR_StepTime() { return mBlindR_StepTime; }
        inline void BlindL_StepTime(uint16_t StepTime) { mBlindL_StepTime = StepTime; }
        inline void BlindR_StepTime(uint16_t StepTime) { mBlindR_StepTime = StepTime; }
        inline String BlindL_Name() { return mBlindL_Name; }
        inline String BlindR_Name() { return mBlindR_Name; }
        inline void BlindL_Name(String Name) { Name.replace(" ", ""); mBlindL_Name = Name.substring(0, 32); }
        inline void BlindR_Name(String Name) { Name.replace(" ", ""); mBlindR_Name = Name.substring(0, 32); }
        inline bool BlindL_ButtonOpen() { return mBlindL_ButtonOpen; }
        inline bool BlindR_ButtonOpen() { return mBlindR_ButtonOpen; }
        inline void BlindL_ButtonOpen(bool Value) { mBlindL_ButtonOpen = Value; }
        inline void BlindR_ButtonOpen(bool Value) { mBlindR_ButtonOpen = Value; }
        inline bool BlindL_ButtonClose() { return mBlindL_ButtonClose; }
        inline bool BlindR_ButtonClose() { return mBlindR_ButtonClose; }
        inline void BlindL_ButtonClose(bool Value) { mBlindL_ButtonClose = Value; }
        inline void BlindR_ButtonClose(bool Value) { mBlindR_ButtonClose = Value; }
        inline bool BlindL_InvertButtons() { return mBlindL_InvertButtons; }
        inline bool BlindR_InvertButtons() { return mBlindR_InvertButtons; }
        inline void BlindL_InvertButtons(bool Value) { mBlindL_InvertButtons = Value; }
        inline void BlindR_InvertButtons(bool Value) { mBlindR_InvertButtons = Value; }

        inline IPAddress Syslog_Server() { return mSyslog_Server; }
        inline void Syslog_Server(IPAddress Server) { mSyslog_Server = Server; }
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

        void FactoryReset();
        void CheckButtonsFactoryReset();
};

extern settings Settings;
