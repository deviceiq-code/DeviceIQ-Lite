#include "ComponentManager.h"

bool ComponentManager::Register(std::unique_ptr<component> instance) {
    if(pStarted || !instance || pCount >= MAX_COMPONENTS) return false;

    // Blinds groups don't carry a real GPIO address (Address() reads 0,
    // same bit pattern as an actual GPIO0 component), so they're excluded
    // by class here rather than by address - comparing address 0 between
    // two Blinds, or between a Blinds and a real GPIO0 component, would
    // otherwise be a false conflict.
    for(size_t index = 0; index < pCount; index++) {
        if(pComponents[index]->ID() == instance->ID() || pComponents[index]->Name() == instance->Name()) return false;
        if(instance->Class() != component::Classes::Blinds && pComponents[index]->Class() != component::Classes::Blinds &&
           pComponents[index]->Address() == instance->Address()) return false;
    }

    pComponents[pCount++] = std::move(instance);
    return true;
}

bool ComponentManager::AssignOwner(component& member, component& owner) {
    if(pStarted || &member == &owner || !IsRegistered(&member) || !IsRegistered(&owner) ||
       member.Owner() != nullptr || owner.Owner() != nullptr) return false;
    member.SetOwner(&owner);
    return true;
}

bool ComponentManager::Start() {
    if(pStarted) return true;
    pStartError.clear();

    for(size_t index = 0; index < pCount; index++) {
        if(!pComponents[index]->Configure()) {
            pStartError = "component #" + String(pComponents[index]->ID()) + " " + pComponents[index]->Name() + " failed during resource configuration";
            return false;
        }
    }

    for(size_t index = 0; index < pCount; index++) {
        if(!pComponents[index]->Initialize()) {
            pStartError = "component #" + String(pComponents[index]->ID()) + " " + pComponents[index]->Name() + " failed during hardware initialization";
            return false;
        }
    }

    pStarted = true;
    return true;
}

component* ComponentManager::FindByName(const String& name) const {
    for(size_t index = 0; index < pCount; index++) {
        if(pComponents[index]->Name().equalsIgnoreCase(name)) return pComponents[index].get();
    }
    return nullptr;
}

component* ComponentManager::FindByID(int16_t id) const {
    for(size_t index = 0; index < pCount; index++) {
        if(pComponents[index]->ID() == id) return pComponents[index].get();
    }
    return nullptr;
}

component* ComponentManager::At(size_t index) const {
    return index < pCount ? pComponents[index].get() : nullptr;
}

bool ComponentManager::PersistenceRequired() const {
    for(size_t index = 0; index < pCount; index++) {
        const component* item = pComponents[index].get();
        if(item->IsPublic() && item->HasPersistentState() && item->StateChanged()) return true;
    }
    return false;
}

void ComponentManager::ClearStateChanged() {
    for(size_t index = 0; index < pCount; index++) pComponents[index]->ClearStateChanged();
}

void ComponentManager::Control(uint32_t now) {
    for(size_t index = 0; index < pCount; index++) pComponents[index]->Control(now);
}

void ComponentManager::RemoveAll() {
    for(size_t index = 0; index < pCount; index++) pComponents[index].reset();
    pCount = 0;
    pStarted = false;
    pStartError.clear();
}

bool ComponentManager::IsRegistered(const component* candidate) const {
    for(size_t index = 0; index < pCount; index++) {
        if(pComponents[index].get() == candidate) return true;
    }
    return false;
}

ComponentManager Components;
