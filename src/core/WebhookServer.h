#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Minimal standalone HTTP server dedicated to triggering component commands
// from external automation systems (Home Assistant, Node-RED, shortcuts,
// scripts) via a shared token instead of a login session. Runs on its own
// port, entirely independent of Webserver (the browser-facing admin UI) -
// no sessions, no cookies, no file serving. Route names and parameters
// (/component/get, /component/set, id/property/value/token) match
// DeviceIQ's own webhookserver; id is resolved against the dynamic
// component list installed from config.json, same as DeviceIQ.
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
