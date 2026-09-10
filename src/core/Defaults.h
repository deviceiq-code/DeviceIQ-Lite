#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <ESP8266WiFi.h>
#include "core/Logger.h"

struct defaults {
    struct network {
        const bool DHCPClient = true;
        // A function rather than a field, like DeviceIQ's: the MAC it reads
        // from isn't available at static-init time, only once actually
        // called at runtime (from settings::LoadDefaults() or later).
        static String Hostname() { uint8_t mac[6]; WiFi.macAddress(mac); char buf[16]; snprintf(buf, sizeof(buf), "diqlite-%02x%02x%02x", mac[3], mac[4], mac[5]); return String(buf); }
        const char* IP_Address = "0.0.0.0";
        const char* Gateway = "0.0.0.0";
        const char* Netmask = "255.255.255.0";
        const char* DNS1 = "8.8.8.8";
        const char* DNS2 = "8.8.4.4";
        const char* SSID = "IOT-2";
        const char* Passphrase = "1921682GenesisIOT-2";
        const uint16_t ConnectionTimeout = 30;
        const bool ReconnectEnabled = true;
        const uint16_t ReconnectInitialInterval = 5;
        const uint16_t ReconnectMaximumInterval = 60;
        const bool FallbackAPEnabled = true;
        const char* FallbackAPSSID = ""; // Empty uses the device hostname.
        const char* FallbackAPPassword = "NoConnection#123!";
        const uint16_t FallbackAPRetention = 300;
        const uint16_t HTTPPort = 80;
    } Network;
    struct components {
        struct blinds {
            const uint16_t StepTimeMs = 250;
            const bool ButtonOpen = true;
            const bool ButtonClose = true;
            const bool InvertButtons = false;
            const char* LeftName = "Left";
            const char* RightName = "Right";
            // Original hardcoded wiring, kept as the default so existing
            // devices behave the same until someone changes them.
            const uint8_t LeftButtonOpenPin = 5;
            const uint8_t LeftButtonClosePin = 4;
            const uint8_t LeftSwitchOpenPin = 14;
            const uint8_t LeftSwitchClosePin = 12;
            const uint8_t RightButtonOpenPin = 0;
            const uint8_t RightButtonClosePin = 2;
            const uint8_t RightSwitchOpenPin = 13;
            const uint8_t RightSwitchClosePin = 10;
        } Blinds;
    } Components;
    struct users {
        struct admin {
            const char* Username = "admin";
            const char* Password = "admin1234";
        } Admin;
        struct user {
            const char* Username = "user";
            const char* Password = "user1234";
        } User;
    } Users;
    struct log {
        // Empty by default (disabled), same convention as MQTT.Broker below
        // - a hostname or IP, resolved at send time (see Logger::Syslog_Server).
        const char* SyslogServer = "syslog.svr";
        const uint16_t SyslogPort = 514;
        // Matches Lite's previous unconditional behavior: Serial + Syslog,
        // everything logged.
        const uint8_t Endpoint = logger::Endpoints::Serial | logger::Endpoints::File;
        const uint8_t Level = logger::LogLevels::All;
    } Log;
    struct general {
        // Unlike DeviceIQ, this only syncs once at boot - no periodic
        // re-sync - so there's no live drift to correct later.
        const bool NTPEnabled = true;
        const char* NTPServer = "pool.ntp.org";
        const int8_t TimeZone = 0; // UTC offset in hours.
    } General;
    struct mqtt {
        const bool Enabled = false;
        const char* Broker = "";
        const uint16_t Port = 1883;
        const char* User = "";
        const char* Password = "";
        const bool DiscoveryEnabled = true;
        const char* DiscoveryPrefix = "homeassistant";
    } MQTT;
    struct webhooks {
        // Disabled and tokenless by default - like MQTT, an optional
        // integration the admin opts into with a real token, rather than
        // an admin surface that should work out of the box.
        const bool Enabled = false;
        const char* Token = "";
        const uint16_t Port = 81;
    } Webhooks;
};

extern const defaults Defaults;
