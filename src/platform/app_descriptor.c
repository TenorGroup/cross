#ifndef SIMULATOR

#include "esp_app_desc.h"
#include "sdkconfig.h"

// The cached Arduino framework carries a weak descriptor from its own build.
// Compile the application descriptor here so it follows this firmware's version.
// esptool fills app_elf_sha256 after linking, as with the IDF descriptor.
const __attribute__((used, section(".rodata_desc"), aligned(4))) esp_app_desc_t esp_app_desc = {
    .magic_word = ESP_APP_DESC_MAGIC_WORD,
    .version = CROSSPOINT_VERSION,
    .project_name = "tenor-cross",
#ifdef CONFIG_APP_COMPILE_TIME_DATE
    .time = __TIME__,
    .date = __DATE__,
#endif
    .idf_ver = IDF_VER,
#ifdef CONFIG_BOOTLOADER_APP_SECURE_VERSION
    .secure_version = CONFIG_BOOTLOADER_APP_SECURE_VERSION,
#endif
    .min_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MIN_FULL,
    .max_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MAX_FULL,
    .mmu_page_size = 31 - __builtin_clz(CONFIG_MMU_PAGE_SIZE),
};

#endif
