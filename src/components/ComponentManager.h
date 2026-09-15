#pragma once

#include <Arduino.h>
#include <memory>

#include "Component.h"

// Simplified port of DeviceIQ's ComponentManager for Lite's single-threaded
// Arduino loop(): a plain registry plus a synchronous Control() sweep, with
// no task/queue runtime.
class ComponentManager {
    public:
        // Plenty for ESP8266's handful of usable GPIOs (a Blinds group plus
        // its member Relay/Button components already uses 5 slots per
        // blind).
        static constexpr size_t MAX_COMPONENTS = 24;

        ComponentManager() = default;
        ~ComponentManager() = default;

        ComponentManager(const ComponentManager&) = delete;
        ComponentManager& operator=(const ComponentManager&) = delete;

        // Components must have application-long lifetime. Registration is
        // only allowed before Start().
        [[nodiscard]] bool Register(std::unique_ptr<component> instance);
        [[nodiscard]] bool AssignOwner(component& member, component& owner);
        [[nodiscard]] bool Start();

        [[nodiscard]] bool IsStarted() const { return pStarted; }
        [[nodiscard]] size_t Count() const { return pCount; }
        [[nodiscard]] component* FindByName(const String& name) const;
        [[nodiscard]] component* FindByID(int16_t id) const;
        [[nodiscard]] component* At(size_t index) const;
        [[nodiscard]] const String& StartError() const { return pStartError; }

        // True when some public, HasPersistentState() component's value has
        // changed since the last ClearStateChanged() - i.e. state.json is
        // stale. Backs the periodic save check in loop() (see
        // settings::SaveComponentsState()).
        [[nodiscard]] bool PersistenceRequired() const;
        void ClearStateChanged();

        void Control(uint32_t now);
        void RemoveAll();

    private:
        [[nodiscard]] bool IsRegistered(const component* candidate) const;

        std::unique_ptr<component> pComponents[MAX_COMPONENTS];
        size_t pCount = 0;
        bool pStarted = false;
        String pStartError;
};

extern ComponentManager Components;
