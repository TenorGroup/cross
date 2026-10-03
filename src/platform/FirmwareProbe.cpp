#include "FirmwareProbe.h"

#if defined(TENOR_PRESS_PROBE) && FREEINK_DEVICE_X4PRO && !defined(SIMULATOR)

#include <HalStorage.h>
#include <Logging.h>
#include <esp_app_desc.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <esp_flash.h>
#include <esp_mac.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_rom_crc.h>
#include <esp_system.h>
#include <spi_flash_mmap.h>

#include "FileTransferState.h"
#include "RollbackProbe.h"
#include "network/FirmwareFlasher.h"
#include "network/OtaBootSwitch.h"

// Survives the panic reset, lost with power: a test can never outlive a power cut.
RTC_NOINIT_ATTR static rollback_probe::CrashCounter crashCounter;

namespace fwprobe {
namespace {

const char* stateName(const uint32_t state) {
  switch (state) {
    case ESP_OTA_IMG_NEW:
      return "NEW";
    case ESP_OTA_IMG_PENDING_VERIFY:
      return "PENDING_VERIFY";
    case ESP_OTA_IMG_VALID:
      return "VALID";
    case ESP_OTA_IMG_INVALID:
      return "INVALID";
    case ESP_OTA_IMG_ABORTED:
      return "ABORTED";
    case ESP_OTA_IMG_UNDEFINED:
      return "UNDEFINED";
    default:
      return "?";
  }
}

void printEfuse() {
  struct Field {
    const char* name;
    const esp_efuse_desc_t** desc;
  };
  static const Field FIELDS[] = {
      {"DIS_USB_SERIAL_JTAG", ESP_EFUSE_DIS_USB_SERIAL_JTAG},
      {"DIS_USB_JTAG", ESP_EFUSE_DIS_USB_JTAG},
      {"DIS_PAD_JTAG", ESP_EFUSE_DIS_PAD_JTAG},
      {"DIS_USB_OTG", ESP_EFUSE_DIS_USB_OTG},
      {"DIS_DOWNLOAD_MODE", ESP_EFUSE_DIS_DOWNLOAD_MODE},
      {"ENABLE_SECURITY_DOWNLOAD", ESP_EFUSE_ENABLE_SECURITY_DOWNLOAD},
      {"DIS_USB_OTG_DOWNLOAD_MODE", ESP_EFUSE_DIS_USB_OTG_DOWNLOAD_MODE},
      {"DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE", ESP_EFUSE_DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE},
      {"DIS_USB_SERIAL_JTAG_ROM_PRINT", ESP_EFUSE_DIS_USB_SERIAL_JTAG_ROM_PRINT},
      {"DIS_DIRECT_BOOT", ESP_EFUSE_DIS_DIRECT_BOOT},
      {"USB_PHY_SEL", ESP_EFUSE_USB_PHY_SEL},
      {"SECURE_BOOT_EN", ESP_EFUSE_SECURE_BOOT_EN},
      {"SPI_BOOT_CRYPT_CNT", ESP_EFUSE_SPI_BOOT_CRYPT_CNT},
  };
  for (const auto& f : FIELDS) {
    uint32_t value = 0;
    const esp_err_t err = esp_efuse_read_field_blob(f.desc, &value, esp_efuse_get_field_size(f.desc));
    logSerial.printf("EFUSE:%s=%lu%s\n", f.name, static_cast<unsigned long>(value),
                     err == ESP_OK ? "" : ",read_failed");
  }
  uint8_t mac[6] = {};
  esp_efuse_mac_get_default(mac);
  logSerial.printf("EFUSE:MAC=%02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// Reads both otadata entries and names what the newest one says about the bootloader.
const char* rollbackVerdict(const bool print) {
  rollback_probe::OtaEntry entries[2] = {{UINT32_MAX, UINT32_MAX, false}, {UINT32_MAX, UINT32_MAX, false}};
  const esp_partition_t* otadata =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  for (int i = 0; otadata && i < 2; ++i) {
    ota_boot::SelectEntry raw{};
    if (esp_partition_read(otadata, i * SPI_FLASH_SEC_SIZE, &raw, sizeof(raw)) != ESP_OK) continue;
    entries[i] = {raw.ota_seq, raw.ota_state, raw.crc == ota_boot::computeSeqCrc(raw.ota_seq)};
    if (print) {
      logSerial.printf("OTADATA:entry=%d,seq=%lu,slot=ota_%lu,state=%s,crc=%s\n", i,
                       static_cast<unsigned long>(raw.ota_seq), static_cast<unsigned long>((raw.ota_seq - 1u) % 2u),
                       stateName(raw.ota_state), entries[i].crcOk ? "ok" : "bad");
    }
  }
  const int newest = rollback_probe::newestEntry(entries);
  const char* verdict = newest < 0 ? "no_otadata" : rollback_probe::verdict(entries[newest].state);
  if (print) logSerial.printf("ROLLBACK:%s,newest_entry=%d\n", verdict, newest);
  return verdict;
}

void printOtaState() {
  logSerial.printf("BOOT:reset_reason=%d\n", static_cast<int>(esp_reset_reason()));
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  for (; it != nullptr; it = esp_partition_next(it)) {
    const esp_partition_t* p = esp_partition_get(it);
    logSerial.printf("PART:%s,type=%u,subtype=0x%02x,addr=0x%06lx,size=0x%06lx,encrypted=%d\n", p->label,
                     static_cast<unsigned>(p->type), static_cast<unsigned>(p->subtype),
                     static_cast<unsigned long>(p->address), static_cast<unsigned long>(p->size), p->encrypted);
    if (p->type != ESP_PARTITION_TYPE_APP) continue;
    esp_ota_img_states_t state;
    const bool hasState = esp_ota_get_state_partition(p, &state) == ESP_OK;
    esp_app_desc_t desc{};
    if (esp_ota_get_partition_description(p, &desc) == ESP_OK) {
      logSerial.printf("APP:%s,state=%s,version=%s,project=%s,built=%s %s,idf=%s,elf=%02x%02x%02x%02x\n", p->label,
                       hasState ? stateName(state) : "none", desc.version, desc.project_name, desc.date, desc.time,
                       desc.idf_ver, desc.app_elf_sha256[0], desc.app_elf_sha256[1], desc.app_elf_sha256[2],
                       desc.app_elf_sha256[3]);
    } else {
      logSerial.printf("APP:%s,state=%s,no_image\n", p->label, hasState ? stateName(state) : "none");
    }
  }
  esp_partition_iterator_release(it);

  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* boot = esp_ota_get_boot_partition();
  const esp_app_desc_t* self = esp_app_get_description();
  logSerial.printf("RUNNING:%s,boot=%s,version=%s,built=%s %s\n", running ? running->label : "?",
                   boot ? boot->label : "?", self->version, self->date, self->time);

  rollbackVerdict(true);
}

void flashDump(const String& path) {
  printOtaState();
  uint32_t size = 0;
  if (path.isEmpty() || esp_flash_get_size(nullptr, &size) != ESP_OK || size == 0) {
    logSerial.printf("FLASH_DUMP_FAIL:size_or_path\n");
    return;
  }
  const String tmp = path + ".tmp";
  HalFile file;
  if (!Storage.openFileForWrite("DUMP", tmp, file)) {
    logSerial.printf("FLASH_DUMP_FAIL:open %s\n", tmp.c_str());
    return;
  }
  static uint8_t buf[4096];
  const unsigned long started = millis();
  uint32_t crc = 0;  // zlib's CRC32, chained (esp_rom_crc32_le inverts in and out)
  uint32_t done = 0;
  bool ok = true;
  logSerial.printf("FLASH_DUMP_START:%lu\n", static_cast<unsigned long>(size));
  while (ok && done < size) {
    ok = esp_flash_read(nullptr, buf, done, sizeof(buf)) == ESP_OK && file.write(buf, sizeof(buf)) == sizeof(buf);
    crc = esp_rom_crc32_le(crc, buf, sizeof(buf));
    done += sizeof(buf);
    if (done % (1024 * 1024) == 0) logSerial.printf("FLASH_DUMP:%lu/%lu\n", done >> 20, size >> 20);
  }
  ok = file.close() && ok;
  if (ok) {
    if (Storage.exists(path.c_str())) Storage.remove(path.c_str());
    ok = Storage.rename(tmp.c_str(), path.c_str());
  } else {
    Storage.remove(tmp.c_str());
  }
  logSerial.printf("FLASH_DUMP_END:ok=%d,bytes=%lu,crc32=%08lx,ms=%lu,path=%s\n", ok, static_cast<unsigned long>(done),
                   static_cast<unsigned long>(crc), millis() - started, path.c_str());
}

void sdFlash(const String& path) {
  if (!filetransfer::acquire()) {
    filetransfer::release();
    logSerial.printf("SD_FLASH_RESULT:BLE_BUSY\n");
    return;
  }
  logSerial.printf("SD_FLASH_START:%s\n", path.c_str());
  static uint32_t tenthShown;
  tenthShown = 0;
  const auto result = firmware_flash::flashFromSdPath(
      path.c_str(),
      [](size_t written, size_t total, void*) {
        const uint32_t tenth = total ? static_cast<uint32_t>(written * 10 / total) : 0;
        if (tenth == tenthShown) return;
        tenthShown = tenth;
        logSerial.printf("SD_FLASH:%lu/%lu\n", static_cast<unsigned long>(written), static_cast<unsigned long>(total));
      },
      nullptr);
  logSerial.printf("SD_FLASH_RESULT:%s\n", firmware_flash::resultName(result));
  if (result != firmware_flash::Result::OK) {
    filetransfer::release();
    return;
  }
  printOtaState();  // the new entry reads NEW until the bootloader takes it up
  logSerial.printf("SD_FLASH:restarting\n");
  delay(500);
  ESP.restart();
}

void rollbackTest(const long crashes) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  rollback_probe::arm(crashCounter, crashes < 0 ? 0 : static_cast<uint32_t>(crashes));
  // A new otadata entry for the running slot: the bootloader trials this same image again, and a
  // rollback falls back to the entry it booted from, which is this image too.
  if (!running || !ota_boot::switchTo(running)) {
    crashCounter = {};
    logSerial.printf("ROLLBACK_TEST:FAIL otadata\n");
    return;
  }
  logSerial.printf("ROLLBACK_TEST:armed crashes=%lu slot=%s, restarting\n",
                   static_cast<unsigned long>(crashCounter.left), running->label);
  printOtaState();
  delay(500);
  ESP.restart();
}

}  // namespace

void onBoot() {
  if (!rollback_probe::takeCrash(crashCounter)) {
    // Read before setup() accepts a trial image: the boot after a test or an install says here
    // what the bootloader did (also in CMD:LOGDUMP, the cable attaches after this).
    LOG_INF("PROBE", "ROLLBACK:%s", rollbackVerdict(false));
    return;
  }
  LOG_INF("PROBE", "ROLLBACK_TEST crash boot, %lu more", static_cast<unsigned long>(crashCounter.left));
  esp_system_abort("ROLLBACK_TEST crash boot");
}

bool command(const String& cmd) {
  if (cmd == "EFUSE") {
    printEfuse();
  } else if (cmd == "OTA_STATE") {
    printOtaState();
  } else if (cmd.startsWith("FLASH_DUMP ")) {
    String path = cmd.substring(11);
    path.trim();
    flashDump(path);
  } else if (cmd.startsWith("SD_FLASH ")) {
    String path = cmd.substring(9);
    path.trim();
    sdFlash(path);
  } else if (cmd.startsWith("ROLLBACK_TEST")) {
    rollbackTest(cmd.length() > 14 ? cmd.substring(14).toInt() : 1);
  } else {
    return false;
  }
  return true;
}

}  // namespace fwprobe

#endif
