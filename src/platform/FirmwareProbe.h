#pragma once

#include <Arduino.h>

// X4 Pro probe build only (TENOR_PRESS_PROBE): read-only views of the chip's eFuses, flash and
// otadata, an install from the card driven over the cable, and a test of the bootloader's
// rollback. Nothing here writes an eFuse.
namespace fwprobe {

// Early in setup(): crashes this boot when CMD:ROLLBACK_TEST armed it.
void onBoot();
// One serial command (without "CMD:"); false when it is none of these:
//   EFUSE                lock-related eFuse fields and the MAC
//   OTA_STATE            partition table, otadata entries, app slots, rollback verdict
//   FLASH_DUMP <path>    the whole flash to the card (temp file, then rename), with its CRC32
//   SD_FLASH <path>      install a .bin from the card into the next OTA slot, then restart
//   ROLLBACK_TEST <n>    put the running image on trial again and crash its next n boots (1-3)
bool command(const String& cmd);

}  // namespace fwprobe
