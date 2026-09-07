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
        const char* SSID = "IOT-3";
        const char* Passphrase = "1921682GenesisIOT-3";
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
        const IPAddress SyslogServer = IPAddress(192, 168, 4, 100);
        const uint16_t SyslogPort = 514;
        // Matches Lite's previous unconditional behavior: Serial + Syslog,
        // everything logged.
        const uint8_t Endpoint = logger::Endpoints::Serial | logger::Endpoints::Syslog;
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
