#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Minimal standalone HTTP server dedicated to triggering blind commands
// from external automation systems (Home Assistant, Node-RED, shortcuts,
// scripts) via a shared token instead of a login session. Runs on its own
// port, entirely independent of Webserver (the browser-facing admin UI) -
// no sessions, no cookies, no file serving. Route names and parameters
// (/component/get, /component/set, id/property/value/token) match
// DeviceIQ's own webhookserver; id 1 is the left blind, 2 the right one -
// DeviceIQ resolves id against its dynamic component list, Lite just has
// the two fixed blinds to pick from.
//
// Unlike DeviceIQ's version, this needs no FreeRTOS task of its own:
// AsyncWebServer is already event-driven on top of the same networking the
// main Webserver uses, so a second instance on its own port just works
// without anything polling it from loop().
class webhookserver {
    public:
        void Start();

    private:
        AsyncWebServer* pServer = nullptr;

        static bool TokenValid(const String& provided);
        static void HandleGet(AsyncWebServerRequest *request);
        static void HandleSet(AsyncWebServerRequest *request);
        static void HandleNotFound(AsyncWebServerRequest *request);
};

extern webhookserver WebhookServer;
