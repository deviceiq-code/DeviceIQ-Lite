#include "Component.h"

component::component(String name, int16_t id, uint8_t address, bool enabled)
    : pName(std::move(name)), pID(id), pAddress(address), pEnabled(enabled) {}

bool component::SetEnabled(bool value) {
    if(pEnabled == value) return false;
    pEnabled = value;
    EnabledChanged(value);
    return true;
}

bool component::Configure() {
    if(pConfigured) return true;
    pConfigured = DoConfigure();
    return pConfigured;
}

bool component::Initialize() {
    if(pInitialized) return true;
    pInitialized = DoInitialize();
    return pInitialized;
}

void component::Control(uint32_t now) {
    if(Enabled()) DoControl(now);
}

void component::GetInfo(String& output) const {
    output += "Name        | " + Name() + "\r\n";
    output += "ID          | " + String(ID()) + "\r\n";
    output += "Class       | " + String(ClassName(Class())) + "\r\n";
    output += "Address     | " + String(Address()) + "\r\n";
    output += "Visibility  | " + String(IsPublic() ? "public" : "private") + "\r\n";
    if(Owner() != nullptr) output += "Owner       | " + Owner()->Name() + "\r\n";
    output += "Enabled     | " + String(Enabled() ? "true" : "false") + "\r\n";
}

const char* component::ClassName(Classes value) {
    switch(value) {
        case Classes::Relay: return "Relay";
        case Classes::Button: return "Button";
        case Classes::Thermometer: return "Thermometer";
        case Classes::Blinds: return "Blinds";
        default: return "Base";
    }
}
