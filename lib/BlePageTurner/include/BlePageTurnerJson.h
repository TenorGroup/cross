#pragma once

#include <ArduinoJson.h>

#include "BlePageTurner.h"

// The page turner's settings in the host's settings.json, under the keys blePageTurnerEnabled,
// blePeerAddr, blePeerName, blePrevKeyUsage, bleNextKeyUsage and bleRemotes (an array of at
// most 4 {addr, binds[<= 8]}). Files written before these keys load unchanged.
namespace bleturner {

void writeJson(const Config& config, JsonDocument& doc);
// Returns false when the file has no blePageTurnerEnabled key (written before the page
// turner existed): the host saves once so the keys are there from then on.
bool readJson(Config& config, JsonVariantConst doc);

}  // namespace bleturner
