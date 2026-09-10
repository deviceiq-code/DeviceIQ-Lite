#include "State.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

void devicestate::BlindPosition(BlindSlot Slot, uint8_t Value) {
    if(mBlindPosition[(uint8_t)Slot] == Value) return;
    mBlindPosition[(uint8_t)Slot] = Value;
    mDirty = true;
}

void devicestate::Control() {
    if(!mDirty) return;
    if((uint32_t)(millis() - mLastSaveMs) < STATE_SAVE_INTERVAL_MS) return;

    Save();
}

bool devicestate::Load(const String& StateFileName) {
    mBlindPosition[0] = 0;
    mBlindPosition[1] = 0;

    if(!LittleFS.exists(StateFileName)) return false;

    File file = LittleFS.open(StateFileName, "r");
    if(!file) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if(err) return false;

    JsonObjectConst blinds = doc["Blinds"];
    mBlindPosition[(uint8_t)BlindSlot::Left] = blinds["Left Position"] | 0;
    mBlindPosition[(uint8_t)BlindSlot::Right] = blinds["Right Position"] | 0;

    return true;
}

bool devicestate::Save(const String& StateFileName) {
    JsonDocument doc;

    JsonObject blinds = doc["Blinds"].to<JsonObject>();
    blinds["Left Position"] = mBlindPosition[(uint8_t)BlindSlot::Left];
    blinds["Right Position"] = mBlindPosition[(uint8_t)BlindSlot::Right];

    File file = LittleFS.open(StateFileName, "w");
    if(!file) return false;

    bool ok = serializeJson(doc, file) > 0;
    file.close();

    mLastSaveMs = millis();
    if(ok) mDirty = false;
    return ok;
}

devicestate DeviceState;
