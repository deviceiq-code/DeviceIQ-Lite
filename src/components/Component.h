#pragma once

#include <Arduino.h>

// Simplified port of DeviceIQ's component base for Lite: no FreeRTOS, no
// command/event queue - everything runs synchronously on the single Arduino
// loop() thread, and properties are set with direct method calls (the same
// threading model Lite already relied on before this existed).
class component {
    public:
        enum class Classes : uint8_t { Base, Relay, Button, Thermometer, Blinds };

        // Address is a GPIO pin number for Relay/Button/Thermometer, and
        // unused (0) for Blinds - there is no "Bus" concept in Lite, since
        // only local GPIO is ever wired.
        component(String name, int16_t id, uint8_t address = 0, bool enabled = true);
        virtual ~component() = default;

        component(const component&) = delete;
        component& operator=(const component&) = delete;
        component(component&&) = delete;
        component& operator=(component&&) = delete;

        [[nodiscard]] const String& Name() const { return pName; }
        [[nodiscard]] int16_t ID() const { return pID; }
        [[nodiscard]] uint8_t Address() const { return pAddress; }
        [[nodiscard]] bool Enabled() const { return pEnabled; }
        [[nodiscard]] bool Configured() const { return pConfigured; }
        [[nodiscard]] bool Initialized() const { return pInitialized; }
        [[nodiscard]] const component* Owner() const { return pOwner; }
        [[nodiscard]] bool IsPublic() const { return pOwner == nullptr; }
        [[nodiscard]] virtual Classes Class() const { return Classes::Base; }
        // Whether this class has a live value worth persisting to
        // state.json (Relay.State, Blinds.Position) - see
        // ComponentManager::PersistenceRequired().
        [[nodiscard]] virtual bool HasPersistentState() const { return false; }
        [[nodiscard]] bool StateChanged() const { return pStateChanged; }

        // Returns whether the value actually changed.
        bool SetEnabled(bool value);
        virtual void GetInfo(String& output) const;
        [[nodiscard]] static const char* ClassName(Classes value);

        // Called only by ComponentManager.
        bool Configure();
        bool Initialize();
        void Control(uint32_t now);

    protected:
        // Configure() declares pins/resources. Initialize() activates
        // hardware only after every component has completed Configure() -
        // still useful even without a Bus, so that a pin conflict between
        // two components is caught before anything actually drives a GPIO.
        virtual bool DoConfigure() { return true; }
        virtual bool DoInitialize() { return true; }
        virtual void EnabledChanged(bool enabled) { (void)enabled; }
        virtual void DoControl(uint32_t now) { (void)now; }

        // Called by a HasPersistentState() subclass whenever its persisted
        // value (Relay.State, Blinds.Position) actually changes.
        void MarkStateChanged() { pStateChanged = true; }

    private:
        friend class ComponentManager;
        void SetOwner(component* owner) { pOwner = owner; }
        void ClearStateChanged() { pStateChanged = false; }

        const String pName;
        const int16_t pID;
        const uint8_t pAddress;
        bool pEnabled;
        bool pConfigured = false;
        bool pInitialized = false;
        bool pStateChanged = false;
        component* pOwner = nullptr;
};
