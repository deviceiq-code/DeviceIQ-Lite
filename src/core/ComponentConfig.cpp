#include "Settings.h"

#include <LittleFS.h>

#include "core/Logger.h"
#include "components/ComponentManager.h"
#include "components/Relay.h"
#include "components/Button.h"
#include "components/Thermometer.h"
#include "components/Blinds.h"

namespace {
    constexpr uint8_t ComponentSchemaVersion = 1;
    constexpr size_t MaxConfiguredComponents = ComponentManager::MAX_COMPONENTS;

    // GPIO pins actually usable on Lite's target board (NodeMCU/ESP8266):
    // excludes UART (1, 3) and the flash pins that aren't broken out and
    // would crash the board if driven. Pin 10 (SD3) is included since it's
    // part of this board's fixed wiring for the right blind's close relay -
    // handle with the same caution the old DEF_*_Special_Pins arrays used
    // to flag it with.
    bool ValidGPIOPin(int pin) {
        switch(pin) {
            case 0: case 2: case 4: case 5:
            case 10: case 12: case 13: case 14: case 15: case 16:
                return true;
            default:
                return false;
        }
    }

    bool ParseComponentID(const char* key, int16_t& id) {
        if(key == nullptr || *key == '\0') return false;
        char* end = nullptr;
        long parsed = strtol(key, &end, 10);
        if(*end != '\0' || parsed < 1 || parsed > INT16_MAX || String(parsed) != key) return false;
        id = (int16_t)parsed;
        return true;
    }

    bool HasComponentSections(JsonObjectConst object) {
        return object["Setup"].is<JsonObjectConst>() && object["Properties"].is<JsonObjectConst>();
    }

    JsonObjectConst ComponentSetup(JsonObjectConst object) { return object["Setup"].as<JsonObjectConst>(); }
    JsonObjectConst ComponentProperties(JsonObjectConst object) { return object["Properties"].as<JsonObjectConst>(); }

    bool ReadOptionalUint32(JsonObjectConst object, const char* name, uint32_t& result) {
        if(object[name].isNull()) return true;
        if(!object[name].is<uint32_t>()) return false;
        result = object[name].as<uint32_t>();
        return true;
    }

    JsonObjectConst ComponentState(JsonObjectConst stateRoot, int16_t id) {
        return stateRoot[String(id)].as<JsonObjectConst>();
    }

    struct RelayConfig {
        String name;
        int16_t id = 0;
        uint8_t address = 0;
        relay::RelayTypes type = relay::RelayTypes::NormallyOpen;
        relay::DriveModes driveMode = relay::DriveModes::ActiveHigh;
        bool state = false;
        bool enabled = true;
        uint32_t pulseTimeMs = 0;
    };

    bool ParseRelayConfig(JsonObjectConst object, int16_t id, RelayConfig& result, JsonObjectConst stateOverride = JsonObjectConst()) {
        JsonObjectConst setup = ComponentSetup(object);
        if(!setup["Name"].is<const char*>() || !setup["Address"].is<int>()) return false;

        result.name = setup["Name"].as<const char*>();
        result.name.trim();
        if(result.name.isEmpty()) return false;

        int address = setup["Address"].as<int>();
        if(!ValidGPIOPin(address)) return false;
        result.id = id;
        result.address = (uint8_t)address;

        if(!setup["Type"].isNull()) {
            if(!setup["Type"].is<const char*>()) return false;
            String type = setup["Type"].as<const char*>();
            if(type.equalsIgnoreCase("NormallyOpen")) result.type = relay::RelayTypes::NormallyOpen;
            else if(type.equalsIgnoreCase("NormallyClosed")) result.type = relay::RelayTypes::NormallyClosed;
            else return false;
        }

        if(!setup["DriveMode"].isNull()) {
            if(!setup["DriveMode"].is<const char*>()) return false;
            String driveMode = setup["DriveMode"].as<const char*>();
            if(driveMode.equalsIgnoreCase("ActiveHigh")) result.driveMode = relay::DriveModes::ActiveHigh;
            else if(driveMode.equalsIgnoreCase("ActiveLow")) result.driveMode = relay::DriveModes::ActiveLow;
            else return false;
        }

        if(!ReadOptionalUint32(setup, "PulseTimeMs", result.pulseTimeMs)) return false;
        if(result.pulseTimeMs != 0 && (result.pulseTimeMs < relay::MIN_PULSE_TIME_MS || result.pulseTimeMs > relay::MAX_PULSE_TIME_MS)) return false;

        JsonObjectConst properties = ComponentProperties(object);
        if(!properties["Enabled"].isNull()) {
            if(!properties["Enabled"].is<bool>()) return false;
            result.enabled = properties["Enabled"].as<bool>();
        }
        if(!properties["State"].isNull()) {
            if(!properties["State"].is<bool>()) return false;
            result.state = properties["State"].as<bool>();
        }

        // A persisted state.json entry overrides the configured seed value.
        // Malformed overrides are ignored rather than failing installation,
        // since the state file is disposable/regenerable.
        if(stateOverride["State"].is<bool>()) result.state = stateOverride["State"].as<bool>();

        // A momentary relay has no meaningful "on" at rest - Control()
        // always releases it well before any of this ever gets read back -
        // so a stale Properties.State or state.json entry is ignored rather
        // than risking it latching on at boot.
        if(result.pulseTimeMs > 0) result.state = false;

        return true;
    }

    struct ButtonConfig {
        String name;
        int16_t id = 0;
        uint8_t address = 0;
        button::ActiveLevels activeLevel = button::ActiveLevels::Low;
        button::InputModes inputMode = button::InputModes::PullUp;
        uint32_t debounceTimeMs = button::DEFAULT_DEBOUNCE_TIME_MS;
        uint32_t longClickTimeMs = button::DEFAULT_LONG_CLICK_TIME_MS;
        uint32_t multiClickTimeMs = button::DEFAULT_MULTI_CLICK_TIME_MS;
        bool enabled = true;
    };

    bool ParseButtonConfig(JsonObjectConst object, int16_t id, ButtonConfig& result) {
        JsonObjectConst setup = ComponentSetup(object);
        if(!setup["Name"].is<const char*>() || !setup["Address"].is<int>()) return false;

        result.name = setup["Name"].as<const char*>();
        result.name.trim();
        if(result.name.isEmpty()) return false;

        int address = setup["Address"].as<int>();
        if(!ValidGPIOPin(address)) return false;
        result.id = id;
        result.address = (uint8_t)address;

        if(!setup["ActiveLevel"].isNull()) {
            if(!setup["ActiveLevel"].is<const char*>()) return false;
            String activeLevel = setup["ActiveLevel"].as<const char*>();
            if(activeLevel.equalsIgnoreCase("High")) result.activeLevel = button::ActiveLevels::High;
            else if(activeLevel.equalsIgnoreCase("Low")) result.activeLevel = button::ActiveLevels::Low;
            else return false;
        }

        if(!setup["InputMode"].isNull()) {
            if(!setup["InputMode"].is<const char*>()) return false;
            String inputMode = setup["InputMode"].as<const char*>();
            if(inputMode.equalsIgnoreCase("Floating")) result.inputMode = button::InputModes::Floating;
            else if(inputMode.equalsIgnoreCase("PullUp")) result.inputMode = button::InputModes::PullUp;
            else if(inputMode.equalsIgnoreCase("PullDown")) result.inputMode = button::InputModes::PullDown;
            else return false;
        }

        if(!ReadOptionalUint32(setup, "DebounceTimeMs", result.debounceTimeMs) ||
           !ReadOptionalUint32(setup, "LongClickTimeMs", result.longClickTimeMs) ||
           !ReadOptionalUint32(setup, "MultiClickTimeMs", result.multiClickTimeMs)) return false;

        JsonObjectConst properties = ComponentProperties(object);
        if(!properties["Enabled"].isNull()) {
            if(!properties["Enabled"].is<bool>()) return false;
            result.enabled = properties["Enabled"].as<bool>();
        }

        return true;
    }

    struct ThermometerConfig {
        String name;
        int16_t id = 0;
        uint8_t address = 0;
        thermometer::ThermometerTypes type = thermometer::ThermometerTypes::Ds18b20;
        uint32_t pollingIntervalMs = thermometer::DEFAULT_POLLING_INTERVAL_MS;
        bool enabled = true;
    };

    bool ParseThermometerConfig(JsonObjectConst object, int16_t id, ThermometerConfig& result) {
        JsonObjectConst setup = ComponentSetup(object);
        if(!setup["Name"].is<const char*>() || !setup["Address"].is<int>()) return false;

        result.name = setup["Name"].as<const char*>();
        result.name.trim();
        if(result.name.isEmpty()) return false;

        int address = setup["Address"].as<int>();
        if(!ValidGPIOPin(address)) return false;
        result.id = id;
        result.address = (uint8_t)address;

        if(!setup["Type"].isNull()) {
            if(!setup["Type"].is<const char*>() || !thermometer::ParseType(setup["Type"].as<const char*>(), result.type)) return false;
        }

        if(!ReadOptionalUint32(setup, "PollingIntervalMs", result.pollingIntervalMs) ||
           result.pollingIntervalMs < thermometer::MINIMUM_POLLING_INTERVAL_MS) return false;

        JsonObjectConst properties = ComponentProperties(object);
        if(!properties["Enabled"].isNull()) {
            if(!properties["Enabled"].is<bool>()) return false;
            result.enabled = properties["Enabled"].as<bool>();
        }

        return true;
    }

    struct BlindsConfig {
        String name;
        int16_t id = 0;
        int16_t relayOpen = 0;
        int16_t relayClose = 0;
        int16_t buttonOpen = 0;
        int16_t buttonClose = 0;
        uint8_t position = 0;
        uint16_t stepTimeMs = blinds::DEFAULT_STEP_TIME_MS;
        bool buttonOpenEnabled = true;
        bool buttonCloseEnabled = true;
        bool invertButtons = false;
        bool enabled = true;
    };

    bool ParseBlindsConfig(JsonObjectConst object, int16_t id, BlindsConfig& result, JsonObjectConst stateOverride = JsonObjectConst()) {
        JsonObjectConst setup = ComponentSetup(object);
        if(!setup["Name"].is<const char*>() || !setup["RelayOpen"].is<int>() || !setup["RelayClose"].is<int>()) return false;

        result.name = setup["Name"].as<const char*>();
        result.name.trim();
        if(result.name.isEmpty()) return false;

        int relayOpen = setup["RelayOpen"].as<int>();
        int relayClose = setup["RelayClose"].as<int>();
        if(relayOpen < 1 || relayOpen > INT16_MAX || relayClose < 1 || relayClose > INT16_MAX || relayOpen == relayClose) return false;
        result.id = id;
        result.relayOpen = (int16_t)relayOpen;
        result.relayClose = (int16_t)relayClose;

        if(!setup["ButtonOpen"].isNull()) {
            if(!setup["ButtonOpen"].is<int>()) return false;
            int buttonOpen = setup["ButtonOpen"].as<int>();
            if(buttonOpen < 1 || buttonOpen > INT16_MAX) return false;
            result.buttonOpen = (int16_t)buttonOpen;
        }
        if(!setup["ButtonClose"].isNull()) {
            if(!setup["ButtonClose"].is<int>()) return false;
            int buttonClose = setup["ButtonClose"].as<int>();
            if(buttonClose < 1 || buttonClose > INT16_MAX) return false;
            result.buttonClose = (int16_t)buttonClose;
        }
        if(result.buttonOpen != 0 && result.buttonOpen == result.buttonClose) return false;

        uint32_t stepTimeMs = result.stepTimeMs;
        if(!ReadOptionalUint32(setup, "StepTimeMs", stepTimeMs) || stepTimeMs == 0 || stepTimeMs > UINT16_MAX) return false;
        result.stepTimeMs = (uint16_t)stepTimeMs;

        JsonObjectConst properties = ComponentProperties(object);
        if(!properties["Enabled"].isNull()) {
            if(!properties["Enabled"].is<bool>()) return false;
            result.enabled = properties["Enabled"].as<bool>();
        }
        if(!properties["ButtonOpenEnabled"].isNull()) {
            if(!properties["ButtonOpenEnabled"].is<bool>()) return false;
            result.buttonOpenEnabled = properties["ButtonOpenEnabled"].as<bool>();
        }
        if(!properties["ButtonCloseEnabled"].isNull()) {
            if(!properties["ButtonCloseEnabled"].is<bool>()) return false;
            result.buttonCloseEnabled = properties["ButtonCloseEnabled"].as<bool>();
        }
        if(!properties["InvertButtons"].isNull()) {
            if(!properties["InvertButtons"].is<bool>()) return false;
            result.invertButtons = properties["InvertButtons"].as<bool>();
        }
        if(!properties["Position"].isNull()) {
            if(!properties["Position"].is<int>()) return false;
            int position = properties["Position"].as<int>();
            if(position < 0 || position > blinds::MAX_POSITION) return false;
            result.position = (uint8_t)position;
        }

        // A persisted state.json entry overrides the configured seed value.
        // Malformed overrides are ignored rather than failing installation,
        // since the state file is disposable/regenerable.
        if(stateOverride["Position"].is<int>()) {
            int position = stateOverride["Position"].as<int>();
            if(position >= 0 && position <= blinds::MAX_POSITION) result.position = (uint8_t)position;
        }

        return true;
    }

    struct Identity {
        String name;
        int16_t id;
        uint8_t address;
        component::Classes type;
        bool hasAddress;
    };

    // Validates every component entry in `components` - identity (Setup
    // parses per its class), duplicate name/GPIO address, and Blinds
    // membership integrity (referenced Relay/Button exists, isn't shared
    // between two Blinds groups) - without constructing anything. Shared by
    // InstallComponents() (boot-time, validates the whole file) and by
    // settings::AddComponent()/UpdateComponent() (validates a prospective
    // change before it's saved to config.json). `identities`/`owners` must
    // each have room for MaxConfiguredComponents entries.
    bool ValidateComponentsCatalog(JsonObjectConst components, Identity identities[], int16_t owners[], size_t& count, String& error) {
        count = 0;
        if(components.isNull() || components.size() > MaxConfiguredComponents) {
            error = "Components must be an object with at most " + String(MaxConfiguredComponents) + " entries";
            return false;
        }

        for(JsonPairConst entry : components) {
            int16_t configuredID = 0;
            if(!ParseComponentID(entry.key().c_str(), configuredID) || !entry.value().is<JsonObjectConst>()) {
                error = "invalid component key '" + String(entry.key().c_str()) + "'";
                return false;
            }
            JsonObjectConst object = entry.value().as<JsonObjectConst>();
            if(!HasComponentSections(object)) {
                error = "component #" + String(configuredID) + ": expected Setup and Properties sections";
                return false;
            }
            JsonObjectConst setup = ComponentSetup(object);
            if(!setup["Class"].is<const char*>()) {
                error = "component #" + String(configuredID) + ": Setup.Class is required";
                return false;
            }

            String componentClass = setup["Class"].as<const char*>();
            if(componentClass.equalsIgnoreCase("Relay")) {
                RelayConfig config;
                if(!ParseRelayConfig(object, configuredID, config)) { error = "component #" + String(configuredID) + ": invalid Relay configuration"; return false; }
                identities[count] = {config.name, config.id, config.address, component::Classes::Relay, true};
            } else if(componentClass.equalsIgnoreCase("Button")) {
                ButtonConfig config;
                if(!ParseButtonConfig(object, configuredID, config)) { error = "component #" + String(configuredID) + ": invalid Button configuration"; return false; }
                identities[count] = {config.name, config.id, config.address, component::Classes::Button, true};
            } else if(componentClass.equalsIgnoreCase("Thermometer")) {
                ThermometerConfig config;
                if(!ParseThermometerConfig(object, configuredID, config)) { error = "component #" + String(configuredID) + ": invalid Thermometer configuration"; return false; }
                identities[count] = {config.name, config.id, config.address, component::Classes::Thermometer, true};
            } else if(componentClass.equalsIgnoreCase("Blinds")) {
                BlindsConfig config;
                if(!ParseBlindsConfig(object, configuredID, config)) { error = "component #" + String(configuredID) + ": invalid Blinds configuration"; return false; }
                identities[count] = {config.name, config.id, 0, component::Classes::Blinds, false};
            } else {
                error = "component #" + String(configuredID) + ": unsupported class '" + componentClass + "'";
                return false;
            }

            for(size_t previous = 0; previous < count; previous++) {
                if(identities[previous].name.equalsIgnoreCase(identities[count].name) ||
                   (identities[previous].hasAddress && identities[count].hasAddress && identities[previous].address == identities[count].address)) {
                    error = "duplicate name or GPIO address for component #" + String(configuredID);
                    return false;
                }
            }

            owners[count] = -1;
            count++;
        }

        auto resolveMember = [&](int16_t selector, component::Classes expected) -> int16_t {
            for(size_t candidate = 0; candidate < count; candidate++) {
                if(identities[candidate].type == expected && identities[candidate].id == selector) return (int16_t)candidate;
            }
            return -1;
        };

        // Validate Blinds membership (relay/button references) after every
        // identity is known, so ordering in the file doesn't matter.
        for(JsonPairConst entry : components) {
            int16_t configuredID = 0;
            ParseComponentID(entry.key().c_str(), configuredID);
            JsonObjectConst object = entry.value().as<JsonObjectConst>();
            String componentClass = ComponentSetup(object)["Class"].as<const char*>();
            if(!componentClass.equalsIgnoreCase("Blinds")) continue;

            BlindsConfig config;
            if(!ParseBlindsConfig(object, configuredID, config)) { error = "component #" + String(configuredID) + ": invalid Blinds configuration"; return false; }
            int16_t blindsIndex = resolveMember(configuredID, component::Classes::Blinds);
            int16_t relayOpen = resolveMember(config.relayOpen, component::Classes::Relay);
            int16_t relayClose = resolveMember(config.relayClose, component::Classes::Relay);
            int16_t buttonOpen = config.buttonOpen == 0 ? -1 : resolveMember(config.buttonOpen, component::Classes::Button);
            int16_t buttonClose = config.buttonClose == 0 ? -1 : resolveMember(config.buttonClose, component::Classes::Button);

            if(relayOpen < 0 || relayClose < 0 || relayOpen == relayClose ||
               (config.buttonOpen != 0 && buttonOpen < 0) || (config.buttonClose != 0 && buttonClose < 0) ||
               (buttonOpen >= 0 && buttonOpen == buttonClose)) {
                error = "component #" + String(configuredID) + ": Blinds references an unresolved or conflicting member component";
                return false;
            }

            const int16_t members[] = {relayOpen, relayClose, buttonOpen, buttonClose};
            for(int16_t member : members) {
                if(member < 0) continue;
                if(owners[member] >= 0) {
                    error = "component is referenced by more than one Blinds group";
                    return false;
                }
                owners[member] = blindsIndex;
            }
        }

        return true;
    }
}

bool settings::InstallComponents(const String& ConfigFileName) {
    if(Components.IsStarted() || Components.Count() != 0) {
        Logger.Write("Components installation rejected: component runtime is already populated", logger::Error);
        return false;
    }

    File file = LittleFS.open(ConfigFileName.length() ? ConfigFileName : String(CONFIG_FILE_NAME), "r");
    if(!file) {
        Logger.Write("Components installation failed: unable to read " + ConfigFileName, logger::Error);
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if(err) {
        Logger.Write("Components installation failed: invalid JSON (" + String(err.c_str()) + ")", logger::Error);
        return false;
    }

    // Persisted runtime state (Relay.State, Blinds.Position) overrides the
    // seed Properties above, per component ID. Missing or invalid state is
    // not an error - it's the expected condition on a device's first boot,
    // or after state.json is lost - it just falls back to the config seed.
    JsonDocument stateDocument;
    {
        File stateFile = LittleFS.open(STATE_FILE_NAME, "r");
        if(stateFile) {
            if(deserializeJson(stateDocument, stateFile)) {
                Logger.Write("Persisted component state file is invalid JSON; using configured defaults", logger::Warning);
                stateDocument.clear();
            }
            stateFile.close();
        }
    }
    JsonObjectConst stateRoot = stateDocument.as<JsonObjectConst>();

    if((doc["ComponentSchemaVersion"] | 0) != ComponentSchemaVersion) {
        Logger.Write("Components installation failed: ComponentSchemaVersion must be " + String(ComponentSchemaVersion), logger::Error);
        return false;
    }

    JsonObjectConst components = doc["Components"].as<JsonObjectConst>();

    Identity identities[MaxConfiguredComponents];
    int16_t owners[MaxConfiguredComponents];
    size_t count = 0;
    String validationError;
    if(!ValidateComponentsCatalog(components, identities, owners, count, validationError)) {
        Logger.Write("Components installation failed: " + validationError, logger::Error);
        return false;
    }

    // Construct physical components first (Relay/Button/Thermometer), so
    // Blinds groups can reference them regardless of ordering in the file.
    std::unique_ptr<component> instances[MaxConfiguredComponents];
    size_t index = 0;
    for(JsonPairConst entry : components) {
        int16_t configuredID = 0;
        ParseComponentID(entry.key().c_str(), configuredID);
        JsonObjectConst object = entry.value().as<JsonObjectConst>();
        String componentClass = ComponentSetup(object)["Class"].as<const char*>();

        if(componentClass.equalsIgnoreCase("Relay")) {
            RelayConfig config;
            ParseRelayConfig(object, configuredID, config, ComponentState(stateRoot, configuredID));
            instances[index].reset(new (std::nothrow) relay(config.name, config.id, config.address, config.type, config.driveMode, owners[index] < 0 ? config.state : false, owners[index] < 0 ? config.enabled : true, config.pulseTimeMs));
        } else if(componentClass.equalsIgnoreCase("Button")) {
            ButtonConfig config;
            ParseButtonConfig(object, configuredID, config);
            instances[index].reset(new (std::nothrow) button(config.name, config.id, config.address, config.activeLevel, config.inputMode, config.debounceTimeMs, config.longClickTimeMs, config.multiClickTimeMs, owners[index] < 0 ? config.enabled : true));
        } else if(componentClass.equalsIgnoreCase("Thermometer")) {
            ThermometerConfig config;
            ParseThermometerConfig(object, configuredID, config);
            instances[index].reset(new (std::nothrow) thermometer(config.name, config.id, config.address, config.type, config.pollingIntervalMs, config.enabled));
        }

        if(!componentClass.equalsIgnoreCase("Blinds") && !instances[index]) {
            Logger.Write("Components installation failed: out of memory constructing component #" + String(configuredID), logger::Error);
            return false;
        }
        index++;
    }

    auto resolveMember = [&](int16_t selector, component::Classes expected) -> int16_t {
        for(size_t candidate = 0; candidate < count; candidate++) {
            if(identities[candidate].type == expected && identities[candidate].id == selector) return (int16_t)candidate;
        }
        return -1;
    };

    index = 0;
    for(JsonPairConst entry : components) {
        int16_t configuredID = 0;
        ParseComponentID(entry.key().c_str(), configuredID);
        JsonObjectConst object = entry.value().as<JsonObjectConst>();
        String componentClass = ComponentSetup(object)["Class"].as<const char*>();
        if(!componentClass.equalsIgnoreCase("Blinds")) { index++; continue; }

        BlindsConfig config;
        ParseBlindsConfig(object, configuredID, config, ComponentState(stateRoot, configuredID));
        int16_t relayOpenIndex = resolveMember(config.relayOpen, component::Classes::Relay);
        int16_t relayCloseIndex = resolveMember(config.relayClose, component::Classes::Relay);
        int16_t buttonOpenIndex = config.buttonOpen == 0 ? -1 : resolveMember(config.buttonOpen, component::Classes::Button);
        int16_t buttonCloseIndex = config.buttonClose == 0 ? -1 : resolveMember(config.buttonClose, component::Classes::Button);

        instances[index].reset(new (std::nothrow) blinds(
            config.name, config.id,
            static_cast<relay&>(*instances[relayOpenIndex]),
            static_cast<relay&>(*instances[relayCloseIndex]),
            buttonOpenIndex < 0 ? nullptr : static_cast<button*>(instances[buttonOpenIndex].get()),
            buttonCloseIndex < 0 ? nullptr : static_cast<button*>(instances[buttonCloseIndex].get()),
            config.position, config.stepTimeMs, config.buttonOpenEnabled, config.buttonCloseEnabled, config.invertButtons, config.enabled
        ));
        if(!instances[index]) {
            Logger.Write("Components installation failed: out of memory constructing component #" + String(configuredID), logger::Error);
            return false;
        }
        index++;
    }

    // Everything validated and constructed - register atomically.
    component* runtimeByIndex[MaxConfiguredComponents]{};
    for(index = 0; index < count; index++) {
        component* raw = instances[index].get();
        if(!Components.Register(std::move(instances[index]))) {
            Logger.Write("Components installation failed: unable to register component #" + String(identities[index].id), logger::Error);
            return false;
        }
        runtimeByIndex[index] = raw;
    }

    for(index = 0; index < count; index++) {
        if(owners[index] < 0) continue;
        if(!Components.AssignOwner(*runtimeByIndex[index], *runtimeByIndex[owners[index]])) {
            Logger.Write("Components installation failed: unable to assign ownership for component #" + String(identities[index].id), logger::Error);
            return false;
        }
    }

    Logger.Write("Installed " + String(count) + " component(s) from " + ConfigFileName);
    return true;
}

bool settings::SaveComponentsState(const String& StateFileName) {
    // Clear before taking the snapshot. Changes occurring while this saves
    // set the flags again and will be persisted by the next check.
    Components.ClearStateChanged();

    // Regenerated from scratch every time, keyed by component ID: a
    // component removed from the live set (via a config.json edit, pending
    // a reboot) simply stops appearing here on the next save.
    JsonDocument doc;
    for(size_t index = 0; index < Components.Count(); index++) {
        const component* item = Components.At(index);
        if(item == nullptr || !item->IsPublic() || !item->HasPersistentState()) continue;

        JsonObject entry = doc[String(item->ID())].to<JsonObject>();
        if(item->Class() == component::Classes::Relay) {
            entry["State"] = static_cast<const relay&>(*item).State();
        } else if(item->Class() == component::Classes::Blinds) {
            entry["Position"] = static_cast<const blinds&>(*item).Position();
        }
    }

    File file = LittleFS.open(StateFileName, "w");
    if(!file) return false;
    bool ok = serializeJson(doc, file) > 0;
    file.close();
    return ok;
}

bool settings::EnsureDefaultComponents(const String& ConfigFileName, const LegacyBlindsSeed& Seed, bool Force) {
    JsonDocument doc;
    if(LittleFS.exists(ConfigFileName)) {
        File file = LittleFS.open(ConfigFileName, "r");
        if(file) {
            deserializeJson(doc, file);
            file.close();
        }
    }

    bool alreadyValid = !Force && (doc["ComponentSchemaVersion"] | 0) == ComponentSchemaVersion && doc["Components"].is<JsonObjectConst>();
    if(alreadyValid) return true;

    doc["ComponentSchemaVersion"] = ComponentSchemaVersion;
    JsonObject components = doc["Components"].to<JsonObject>();

    // Reproduces Lite's fixed PCB wiring: two Blinds, matching the pins
    // main.cpp used to hardcode directly.
    auto addRelay = [&](int16_t id, const char* name, uint8_t address) {
        JsonObject item = components[String(id)].to<JsonObject>();
        JsonObject setup = item["Setup"].to<JsonObject>();
        setup["Name"] = name;
        setup["Class"] = "Relay";
        setup["Address"] = address;
        JsonObject properties = item["Properties"].to<JsonObject>();
        properties["Enabled"] = true;
        properties["State"] = false;
    };

    auto addButton = [&](int16_t id, const char* name, uint8_t address) {
        JsonObject item = components[String(id)].to<JsonObject>();
        JsonObject setup = item["Setup"].to<JsonObject>();
        setup["Name"] = name;
        setup["Class"] = "Button";
        setup["Address"] = address;
        JsonObject properties = item["Properties"].to<JsonObject>();
        properties["Enabled"] = true;
    };

    auto addBlinds = [&](int16_t id, const String& name, int16_t relayOpen, int16_t relayClose, int16_t buttonOpen, int16_t buttonClose,
                          uint16_t stepTimeMs, bool buttonOpenEnabled, bool buttonCloseEnabled, bool invertButtons) {
        JsonObject item = components[String(id)].to<JsonObject>();
        JsonObject setup = item["Setup"].to<JsonObject>();
        setup["Name"] = name;
        setup["Class"] = "Blinds";
        setup["RelayOpen"] = relayOpen;
        setup["RelayClose"] = relayClose;
        setup["ButtonOpen"] = buttonOpen;
        setup["ButtonClose"] = buttonClose;
        setup["StepTimeMs"] = stepTimeMs;
        JsonObject properties = item["Properties"].to<JsonObject>();
        properties["Enabled"] = true;
        properties["Position"] = 0;
        properties["ButtonOpenEnabled"] = buttonOpenEnabled;
        properties["ButtonCloseEnabled"] = buttonCloseEnabled;
        properties["InvertButtons"] = invertButtons;
    };

    addRelay(1, "LeftRelayOpen", 14);
    addRelay(2, "LeftRelayClose", 12);
    addButton(3, "LeftButtonOpen", 5);
    addButton(4, "LeftButtonClose", 4);
    addBlinds(5, Seed.LeftName, 1, 2, 3, 4, Seed.LeftStepTime, Seed.LeftButtonOpen, Seed.LeftButtonClose, Seed.LeftInvertButtons);

    addRelay(6, "RightRelayOpen", 13);
    addRelay(7, "RightRelayClose", 10);
    addButton(8, "RightButtonOpen", 0);
    addButton(9, "RightButtonClose", 2);
    addBlinds(10, Seed.RightName, 6, 7, 8, 9, Seed.RightStepTime, Seed.RightButtonOpen, Seed.RightButtonClose, Seed.RightInvertButtons);

    File file = LittleFS.open(ConfigFileName, "w");
    if(!file) return false;
    bool ok = serializeJsonPretty(doc, file) > 0;
    file.close();
    return ok;
}

namespace {
    bool ParseConfigBool(const String& value, bool& result) {
        if(value.equalsIgnoreCase("true") || value == "1") { result = true; return true; }
        if(value.equalsIgnoreCase("false") || value == "0") { result = false; return true; }
        return false;
    }
}

bool settings::SetComponentProperty(int16_t ID, const String& Property, const String& Value, String& Error) {
    component* target = Components.FindByID(ID);
    if(target == nullptr) { Error = "component not found"; return false; }

    // Live-only: reflected in state.json via the normal periodic save
    // (ComponentManager::PersistenceRequired()) rather than written back
    // into config.json here - same split Blinds.Position already used via
    // /api/blinds, now shared by the Dashboard's Relay toggle and Blinds
    // Open/Stop/Close controls (both now go through this single endpoint).
    if(target->Class() == component::Classes::Relay && Property.equalsIgnoreCase("State")) {
        bool boolValue = false;
        if(!ParseConfigBool(Value, boolValue)) { Error = "invalid value"; return false; }
        static_cast<relay&>(*target).SetState(boolValue);
        return true;
    }
    if(target->Class() == component::Classes::Blinds && Property.equalsIgnoreCase("State")) {
        blinds& target_blinds = static_cast<blinds&>(*target);
        if(Value.equalsIgnoreCase("open")) target_blinds.Open();
        else if(Value.equalsIgnoreCase("close")) target_blinds.Close();
        else if(Value.equalsIgnoreCase("stop")) target_blinds.Stop();
        else { Error = "invalid value"; return false; }
        return true;
    }

    bool boolValue = false;
    long numericValue = 0;

    if(Property.equalsIgnoreCase("Enabled")) {
        if(!ParseConfigBool(Value, boolValue)) { Error = "invalid value"; return false; }
        target->SetEnabled(boolValue);
    } else if(target->Class() == component::Classes::Blinds && Property.equalsIgnoreCase("StepTimeMs")) {
        numericValue = Value.toInt();
        if(numericValue < 1 || numericValue > UINT16_MAX) { Error = "invalid value"; return false; }
        static_cast<blinds&>(*target).StepTime((uint16_t)numericValue);
    } else if(target->Class() == component::Classes::Blinds && Property.equalsIgnoreCase("ButtonOpenEnabled")) {
        if(!ParseConfigBool(Value, boolValue)) { Error = "invalid value"; return false; }
        static_cast<blinds&>(*target).ButtonOpenEnabled(boolValue);
    } else if(target->Class() == component::Classes::Blinds && Property.equalsIgnoreCase("ButtonCloseEnabled")) {
        if(!ParseConfigBool(Value, boolValue)) { Error = "invalid value"; return false; }
        static_cast<blinds&>(*target).ButtonCloseEnabled(boolValue);
    } else if(target->Class() == component::Classes::Blinds && Property.equalsIgnoreCase("InvertButtons")) {
        if(!ParseConfigBool(Value, boolValue)) { Error = "invalid value"; return false; }
        static_cast<blinds&>(*target).InvertButtons(boolValue);
    } else {
        Error = "unsupported property";
        return false;
    }

    // Persist alongside the live change so it survives a reboot.
    JsonDocument doc;
    File file = LittleFS.open(CONFIG_FILE_NAME, "r");
    if(file) { deserializeJson(doc, file); file.close(); }

    JsonObject item = doc["Components"][String(ID)].as<JsonObject>();
    if(!item.isNull()) {
        if(Property.equalsIgnoreCase("Enabled")) item["Properties"]["Enabled"] = boolValue;
        else if(Property.equalsIgnoreCase("StepTimeMs")) item["Setup"]["StepTimeMs"] = numericValue;
        else if(Property.equalsIgnoreCase("ButtonOpenEnabled")) item["Properties"]["ButtonOpenEnabled"] = boolValue;
        else if(Property.equalsIgnoreCase("ButtonCloseEnabled")) item["Properties"]["ButtonCloseEnabled"] = boolValue;
        else if(Property.equalsIgnoreCase("InvertButtons")) item["Properties"]["InvertButtons"] = boolValue;

        File outFile = LittleFS.open(CONFIG_FILE_NAME, "w");
        if(outFile) {
            serializeJsonPretty(doc, outFile);
            outFile.close();
        }
    }

    return true;
}

// Creates a new config.json entry - only the identity fields validation
// depends on (Name, and either Address or RelayOpen/RelayClose) are set
// here; everything else follows via a separate UpdateComponent() call, same
// two-step split the original DeviceIQ web API uses. Like every other
// component mutation, this only edits config.json - it never touches the
// live ComponentManager, so a restart is required to apply it.
bool settings::AddComponent(const String& ClassName, const String& Name, int Address, int16_t RelayOpen, int16_t RelayClose, int16_t& NewID, String& Error) {
    if(!ClassName.equalsIgnoreCase("Relay") && !ClassName.equalsIgnoreCase("Button") &&
       !ClassName.equalsIgnoreCase("Thermometer") && !ClassName.equalsIgnoreCase("Blinds")) {
        Error = "unsupported component class";
        return false;
    }

    String name = Name;
    name.trim();
    if(name.isEmpty()) { Error = "name is required"; return false; }

    JsonDocument doc;
    File file = LittleFS.open(CONFIG_FILE_NAME, "r");
    if(!file) { Error = "unable to read configuration"; return false; }
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if(err) { Error = "invalid configuration file"; return false; }

    JsonObject components = doc["Components"].as<JsonObject>();
    if(components.isNull()) { Error = "invalid configuration file"; return false; }

    // Lowest unused ID, same approach as the original DeviceIQ's web API.
    int16_t newID = 0;
    for(int16_t candidate = 1; candidate <= INT16_MAX; candidate++) {
        if(components[String(candidate)].isNull()) { newID = candidate; break; }
    }
    if(newID == 0) { Error = "maximum component count reached"; return false; }

    JsonObject item = components[String(newID)].to<JsonObject>();
    JsonObject setup = item["Setup"].to<JsonObject>();
    setup["Name"] = name;
    setup["Class"] = ClassName;
    JsonObject properties = item["Properties"].to<JsonObject>();
    properties["Enabled"] = true;

    if(ClassName.equalsIgnoreCase("Blinds")) {
        setup["RelayOpen"] = RelayOpen;
        setup["RelayClose"] = RelayClose;
        properties["Position"] = 0;
    } else {
        setup["Address"] = Address;
        if(ClassName.equalsIgnoreCase("Relay")) properties["State"] = false;
    }

    Identity identities[MaxConfiguredComponents];
    int16_t owners[MaxConfiguredComponents];
    size_t count = 0;
    if(!ValidateComponentsCatalog(components, identities, owners, count, Error)) {
        components.remove(String(newID));
        return false;
    }

    File outFile = LittleFS.open(CONFIG_FILE_NAME, "w");
    if(!outFile) { Error = "unable to save configuration"; return false; }
    bool saved = serializeJsonPretty(doc, outFile) > 0;
    outFile.close();
    if(!saved) { Error = "unable to save configuration"; return false; }

    Logger.Write("Added component #" + String(newID) + " (" + ClassName + " '" + name + "') to " + String(CONFIG_FILE_NAME) + " - restart required to apply");
    NewID = newID;
    return true;
}

// Applies a set of Setup/Properties field changes to an existing config.json
// entry (Fields keys are lowercase, matching the web form's field names -
// see the /api/components/update handler in main.cpp). Unlike
// SetComponentProperty(), this never touches the live component - every
// field here (including ones SetComponentProperty can also apply live, like
// Enabled) only takes effect after a restart, same as AddComponent()/
// RemoveComponent().
bool settings::UpdateComponent(int16_t ID, JsonObjectConst Fields, String& Error) {
    JsonDocument doc;
    File file = LittleFS.open(CONFIG_FILE_NAME, "r");
    if(!file) { Error = "unable to read configuration"; return false; }
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if(err) { Error = "invalid configuration file"; return false; }

    JsonObject components = doc["Components"].as<JsonObject>();
    JsonObject item = components.isNull() ? JsonObject() : components[String(ID)].as<JsonObject>();
    if(item.isNull()) { Error = "component not found"; return false; }

    JsonObject setup = item["Setup"].as<JsonObject>();
    JsonObject properties = item["Properties"].as<JsonObject>();
    String componentClass = setup["Class"] | "";

    auto has = [&](const char* key) { return !Fields[key].isNull(); };
    auto text = [&](const char* key) { return Fields[key].as<String>(); };

    if(has("name")) {
        String name = text("name");
        name.trim();
        setup["Name"] = name;
    }
    if(has("enabled")) properties["Enabled"] = text("enabled").equalsIgnoreCase("true");

    if(componentClass.equalsIgnoreCase("Relay")) {
        if(has("address")) setup["Address"] = text("address").toInt();
        if(has("type")) setup["Type"] = text("type");
        if(has("drivemode")) setup["DriveMode"] = text("drivemode");
        if(has("pulsetimems")) setup["PulseTimeMs"] = (uint32_t)text("pulsetimems").toInt();
    } else if(componentClass.equalsIgnoreCase("Button")) {
        if(has("address")) setup["Address"] = text("address").toInt();
        if(has("activelevel")) setup["ActiveLevel"] = text("activelevel");
        if(has("inputmode")) setup["InputMode"] = text("inputmode");
        if(has("debouncetimems")) setup["DebounceTimeMs"] = (uint32_t)text("debouncetimems").toInt();
        if(has("longclicktimems")) setup["LongClickTimeMs"] = (uint32_t)text("longclicktimems").toInt();
        if(has("multiclicktimems")) setup["MultiClickTimeMs"] = (uint32_t)text("multiclicktimems").toInt();
    } else if(componentClass.equalsIgnoreCase("Thermometer")) {
        if(has("address")) setup["Address"] = text("address").toInt();
        if(has("type")) setup["Type"] = text("type");
        if(has("pollingintervalms")) setup["PollingIntervalMs"] = (uint32_t)text("pollingintervalms").toInt();
    } else if(componentClass.equalsIgnoreCase("Blinds")) {
        if(has("relayopen")) setup["RelayOpen"] = text("relayopen").toInt();
        if(has("relayclose")) setup["RelayClose"] = text("relayclose").toInt();
        if(has("buttonopen")) setup["ButtonOpen"] = text("buttonopen").toInt();
        if(has("buttonclose")) setup["ButtonClose"] = text("buttonclose").toInt();
        if(has("steptimems")) setup["StepTimeMs"] = (uint32_t)text("steptimems").toInt();
    }

    Identity identities[MaxConfiguredComponents];
    int16_t owners[MaxConfiguredComponents];
    size_t count = 0;
    if(!ValidateComponentsCatalog(components, identities, owners, count, Error)) return false;

    File outFile = LittleFS.open(CONFIG_FILE_NAME, "w");
    if(!outFile) { Error = "unable to save configuration"; return false; }
    bool saved = serializeJsonPretty(doc, outFile) > 0;
    outFile.close();
    if(!saved) { Error = "unable to save configuration"; return false; }

    Logger.Write("Updated component #" + String(ID) + " in " + String(CONFIG_FILE_NAME) + " - restart required to apply");
    return true;
}

// Deletes a config.json entry outright. Refuses to remove a component that's
// still a Blinds group's member (RelayOpen/RelayClose/ButtonOpen/
// ButtonClose) - same ownership rule InstallComponents()/
// ValidateComponentsCatalog() enforce at boot; the Blinds group has to be
// removed first. Like AddComponent()/UpdateComponent(), a restart is
// required to apply the removal.
bool settings::RemoveComponent(int16_t ID, String& Error) {
    JsonDocument doc;
    File file = LittleFS.open(CONFIG_FILE_NAME, "r");
    if(!file) { Error = "unable to read configuration"; return false; }
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if(err) { Error = "invalid configuration file"; return false; }

    JsonObject components = doc["Components"].as<JsonObject>();
    if(components.isNull() || components[String(ID)].isNull()) { Error = "component not found"; return false; }

    for(JsonPair entry : components) {
        JsonObjectConst object = entry.value().as<JsonObjectConst>();
        JsonObjectConst setup = object["Setup"].as<JsonObjectConst>();
        String componentClass = setup["Class"] | "";
        if(!componentClass.equalsIgnoreCase("Blinds")) continue;

        int relayOpen = setup["RelayOpen"] | 0;
        int relayClose = setup["RelayClose"] | 0;
        int buttonOpen = setup["ButtonOpen"] | 0;
        int buttonClose = setup["ButtonClose"] | 0;
        if(relayOpen == ID || relayClose == ID || buttonOpen == ID || buttonClose == ID) {
            String blindsName = setup["Name"] | "another component";
            Error = "component is used by Blinds '" + blindsName + "' - remove that first";
            return false;
        }
    }

    components.remove(String(ID));

    File outFile = LittleFS.open(CONFIG_FILE_NAME, "w");
    if(!outFile) { Error = "unable to save configuration"; return false; }
    bool saved = serializeJsonPretty(doc, outFile) > 0;
    outFile.close();
    if(!saved) { Error = "unable to save configuration"; return false; }

    Logger.Write("Removed component #" + String(ID) + " from " + String(CONFIG_FILE_NAME) + " - restart required to apply");
    return true;
}
