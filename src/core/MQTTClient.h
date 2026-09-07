#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include "Blinds.h"

extern Blinds *BlindL, *BlindR;

// Same shape as DeviceIQ's mqttclient: connect, publish blind state,
// publish Home Assistant MQTT discovery for each blind as a "cover", and
// accept Set commands back. What's different is entirely about the
// platform, not the protocol: DeviceIQ runs this from its own FreeRTOS
// task with event queues; the ESP8266 here has neither, so Loop() is
// called from the sketch's own loop() instead, and state changes are
// noticed by comparing against the last-published value rather than
// reacting to a component event.
class mqttclient {
    public:
        mqttclient() : pClient(pNetworkClient) {}
        mqttclient(const mqttclient&) = delete;
        mqttclient& operator=(const mqttclient&) = delete;

        void Start();
        // Call every sketch loop() iteration - a no-op when MQTT is
        // disabled or there is no STA WiFi connection to use.
        void Loop();

    private:
        // A blocking pClient.connect() takes up to this long when the
        // broker is unreachable, stalling the whole device meanwhile
        // (blind control and the web server included) - there is no
        // second core or task to keep them running underneath it. Kept
        // short, and only attempted once per RECONNECT_INTERVAL_MS, so an
        // unreachable broker costs a couple of seconds occasionally rather
        // than constantly.
        static constexpr uint16_t MQTT_SOCKET_TIMEOUT_SECONDS = 2;
        static constexpr uint16_t MQTT_KEEP_ALIVE_SECONDS = 30;
        static constexpr uint16_t MQTT_BUFFER_SIZE = 1024;
        static constexpr uint32_t RECONNECT_INTERVAL_MS = 5000;

        bool Connect();
        void HandleMessage(const String& topic, const uint8_t* payload, size_t length);
        void PublishDiscovery();
        void PublishBlindDiscovery(const String& name, const String& uniqueSuffix);
        void PublishStateIfChanged();
        void PublishBlindState(const String& name, uint8_t position, const char* state);
        bool Publish(const String& topic, const String& payload, bool retained = false);
        String ComponentTopic(const String& name, const char* direction, const char* property) const;
        String AvailabilityTopic() const;
        String UniqueID(const String& suffix) const;
        static bool ValidTopicSegment(const String& value);
        static String DeviceIdentifier();
        static void MessageCallback(char* topic, uint8_t* payload, unsigned int length);

        static mqttclient* pActiveInstance;

        WiFiClient pNetworkClient;
        PubSubClient pClient;

        bool pEnabled = false;
        String pBroker;
        uint16_t pPort = 1883;
        String pUser;
        String pPassword;
        String pHostname;
        bool pDiscoveryEnabled = true;
        String pDiscoveryPrefix;
        String pDeviceID;

        unsigned long pLastConnectAttemptMs = 0;
        bool pDiscoveryPending = false;

        // Last state actually published, so PublishStateIfChanged() only
        // publishes on an actual change instead of every Loop() call.
        bool pHavePublishedLeft = false, pHavePublishedRight = false;
        uint8_t pLastLeftPosition = 0, pLastRightPosition = 0;
        Blinds_State pLastLeftState = Blinds_State::Stopped, pLastRightState = Blinds_State::Stopped;
};

extern mqttclient MQTTClient;
