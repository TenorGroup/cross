#include "UglyIconInk.h"

#include <InlineSymbols.h>
#include <cstring>

#include "activities/Activity.h"
#include "shells/Shell.h"
#include "components/icons/listIcons.h"
#include "components/icons/tenorRowIcons.h"
#include "components/icons/tenorTransferIcons.h"
#include "components/icons/tenorHomeTabIcons.h"
#include "components/icons/tenorReaderTabIcons.h"
#include "components/icons/tenorStatusIcons.h"
#include "components/icons/headerIcons.h"
#include "components/icons/customListIcons.h"
#include "components/icons/readerToolbarIcons.h"
#include "components/icons/readerToolbarTabIcons.h"

namespace ugly {

bool iconAsset(const GfxRenderer& renderer, const uint8_t* bits, icons::Rect box, uint32_t seed,
               int nativeWidth, int nativeHeight, bool rowInk) {
  struct Asset { const uint8_t* bits; icons::Id id; int width, height; };
  static constexpr Asset assets[] = {
    {icon_reader_contents_24.bits, icons::Id::Contents, icon_reader_contents_24.w, icon_reader_contents_24.h},
    {icon_reader_text_24.bits, icons::Id::Font, icon_reader_text_24.w, icon_reader_text_24.h},
    {icon_reader_more_24.bits, icons::Id::More, icon_reader_more_24.w, icon_reader_more_24.h},
    {icon_reader_back_24.bits, icons::Id::Back, icon_reader_back_24.w, icon_reader_back_24.h},
    {icon_reader_next_24.bits, icons::Id::Right, icon_reader_next_24.w, icon_reader_next_24.h},
    {icon_reader_tick_24.bits, icons::Id::Tick, icon_reader_tick_24.w, icon_reader_tick_24.h},
    {icon_reader_align_justify_24.bits, icons::Id::AlignJustify, icon_reader_align_justify_24.w, icon_reader_align_justify_24.h},
    {icon_reader_align_left_24.bits, icons::Id::AlignLeft, icon_reader_align_left_24.w, icon_reader_align_left_24.h},
    {icon_reader_align_center_24.bits, icons::Id::AlignCenter, icon_reader_align_center_24.w, icon_reader_align_center_24.h},
    {icon_reader_align_right_24.bits, icons::Id::AlignRight, icon_reader_align_right_24.w, icon_reader_align_right_24.h},
    {icon_reader_align_book_24.bits, icons::Id::AlignBook, icon_reader_align_book_24.w, icon_reader_align_book_24.h},
    {icon_reader_spacing_1_24.bits, icons::Id::Spacing1, icon_reader_spacing_1_24.w, icon_reader_spacing_1_24.h},
    {icon_reader_spacing_2_24.bits, icons::Id::Spacing2, icon_reader_spacing_2_24.w, icon_reader_spacing_2_24.h},
    {icon_reader_spacing_3_24.bits, icons::Id::Spacing3, icon_reader_spacing_3_24.w, icon_reader_spacing_3_24.h},
    {icon_reader_spacing_4_24.bits, icons::Id::Spacing4, icon_reader_spacing_4_24.w, icon_reader_spacing_4_24.h},
    {icon_reader_spacing_5_24.bits, icons::Id::Spacing5, icon_reader_spacing_5_24.w, icon_reader_spacing_5_24.h},
    {icon_reader_dropcap_off_24.bits, icons::Id::DropOff, icon_reader_dropcap_off_24.w, icon_reader_dropcap_off_24.h},
    {icon_reader_dropcap_default_24.bits, icons::Id::DropDefault, icon_reader_dropcap_default_24.w, icon_reader_dropcap_default_24.h},
    {icon_reader_dropcap_large_24.bits, icons::Id::DropLarge, icon_reader_dropcap_large_24.w, icon_reader_dropcap_large_24.h},
    {icon_reader_tab_contents_40.bits, icons::Id::Contents, icon_reader_tab_contents_40.w, icon_reader_tab_contents_40.h},
    {icon_reader_tab_text_40.bits, icons::Id::Font, icon_reader_tab_text_40.w, icon_reader_tab_text_40.h},
    {icon_reader_tab_more_40.bits, icons::Id::More, icon_reader_tab_more_40.w, icon_reader_tab_more_40.h},
    {icon_reader_tab_contents_bold_40.bits, icons::Id::Contents, icon_reader_tab_contents_bold_40.w, icon_reader_tab_contents_bold_40.h},
    {icon_reader_tab_text_bold_40.bits, icons::Id::Font, icon_reader_tab_text_bold_40.w, icon_reader_tab_text_bold_40.h},
    {icon_reader_tab_more_bold_40.bits, icons::Id::More, icon_reader_tab_more_bold_40.w, icon_reader_tab_more_bold_40.h},
    {icon_sun_filled_24_bits, icons::Id::Sun, 24, 24},
    {icon_sun_filled_32_bits, icons::Id::Sun, 32, 32},
    {icon_moon_filled_24_bits, icons::Id::Moon, 24, 24},
    {icon_moon_filled_32_bits, icons::Id::Moon, 32, 32},
    {icon_folder_24.bits, icons::Id::Folder, icon_folder_24.w, icon_folder_24.h},
    {icon_file_text_24.bits, icons::Id::Text, icon_file_text_24.w, icon_file_text_24.h},
    {icon_image_24.bits, icons::Id::Image, icon_image_24.w, icon_image_24.h},
    {icon_book_24.bits, icons::Id::Book, icon_book_24.w, icon_book_24.h},
    {icon_file_24.bits, icons::Id::File, icon_file_24.w, icon_file_24.h},
    {icon_wifi_24.bits, icons::Id::Wifi, icon_wifi_24.w, icon_wifi_24.h},
    {icon_library_24.bits, icons::Id::Library, icon_library_24.w, icon_library_24.h},
    {icon_radio_tower_24.bits, icons::Id::Hotspot, icon_radio_tower_24.w, icon_radio_tower_24.h},
    {icon_sun_24.bits, icons::Id::Sun, icon_sun_24.w, icon_sun_24.h},
    {icon_sun_32.bits, icons::Id::Sun, icon_sun_32.w, icon_sun_32.h},
    {icon_moon_24.bits, icons::Id::Moon, icon_moon_24.w, icon_moon_24.h},
    {icon_moon_32.bits, icons::Id::Moon, icon_moon_32.w, icon_moon_32.h},
    {icon_upload_32.bits, icons::Id::Up, icon_upload_32.w, icon_upload_32.h},
    {icon_download_32.bits, icons::Id::Down, icon_download_32.w, icon_download_32.h},
    {icon_bookmark_24.bits, icons::Id::Bookmark, icon_bookmark_24.w, icon_bookmark_24.h},
    {icon_usb_24.bits, icons::Id::Usb, icon_usb_24.w, icon_usb_24.h},
    {icon_usb_32.bits, icons::Id::Usb, icon_usb_32.w, icon_usb_32.h},
    {icon_blocks_24.bits, icons::Id::Blocks, icon_blocks_24.w, icon_blocks_24.h},
    {icon_blocks_32.bits, icons::Id::Blocks, icon_blocks_32.w, icon_blocks_32.h},
    {icon_refresh_cw_24.bits, icons::Id::Refresh, icon_refresh_cw_24.w, icon_refresh_cw_24.h},
    {icon_refresh_cw_32.bits, icons::Id::Refresh, icon_refresh_cw_32.w, icon_refresh_cw_32.h},
    {icon_folder_32.bits, icons::Id::Folder, icon_folder_32.w, icon_folder_32.h},
    {icon_file_text_32.bits, icons::Id::Text, icon_file_text_32.w, icon_file_text_32.h},
    {icon_image_32.bits, icons::Id::Image, icon_image_32.w, icon_image_32.h},
    {icon_book_32.bits, icons::Id::Book, icon_book_32.w, icon_book_32.h},
    {icon_file_32.bits, icons::Id::File, icon_file_32.w, icon_file_32.h},
    {icon_wifi_32.bits, icons::Id::Wifi, icon_wifi_32.w, icon_wifi_32.h},
    {icon_library_32.bits, icons::Id::Library, icon_library_32.w, icon_library_32.h},
    {icon_radio_tower_32.bits, icons::Id::Hotspot, icon_radio_tower_32.w, icon_radio_tower_32.h},
    {icon_bookmark_32.bits, icons::Id::Bookmark, icon_bookmark_32.w, icon_bookmark_32.h},
    {icon_download_24.bits, icons::Id::Down, icon_download_24.w, icon_download_24.h},
    {icon_upload_24.bits, icons::Id::Up, icon_upload_24.w, icon_upload_24.h},
    {icon_tenor_home_recent_40.bits, icons::Id::Book, icon_tenor_home_recent_40.w, icon_tenor_home_recent_40.h},
    {icon_tenor_home_folder_40.bits, icons::Id::Folder, icon_tenor_home_folder_40.w, icon_tenor_home_folder_40.h},
    {icon_tenor_home_stats_40.bits, icons::Id::Stats, icon_tenor_home_stats_40.w, icon_tenor_home_stats_40.h},
    {icon_tenor_home_settings_40.bits, icons::Id::Gear, icon_tenor_home_settings_40.w, icon_tenor_home_settings_40.h},
    {icon_tenor_home_favorites_40.bits, icons::Id::Heart, icon_tenor_home_favorites_40.w, icon_tenor_home_favorites_40.h},
    {icon_tenor_home_recent_bold_40.bits, icons::Id::Book, icon_tenor_home_recent_bold_40.w, icon_tenor_home_recent_bold_40.h},
    {icon_tenor_home_folder_bold_40.bits, icons::Id::Folder, icon_tenor_home_folder_bold_40.w, icon_tenor_home_folder_bold_40.h},
    {icon_tenor_home_stats_bold_40.bits, icons::Id::Stats, icon_tenor_home_stats_bold_40.w, icon_tenor_home_stats_bold_40.h},
    {icon_tenor_home_settings_bold_40.bits, icons::Id::Gear, icon_tenor_home_settings_bold_40.w, icon_tenor_home_settings_bold_40.h},
    {icon_tenor_home_favorites_bold_40.bits, icons::Id::Heart, icon_tenor_home_favorites_bold_40.w, icon_tenor_home_favorites_bold_40.h},
    {icon_tenor_reader_position_40.bits, icons::Id::Bookmark, icon_tenor_reader_position_40.w, icon_tenor_reader_position_40.h},
    {icon_tenor_reader_reading_40.bits, icons::Id::Book, icon_tenor_reader_reading_40.w, icon_tenor_reader_reading_40.h},
    {icon_tenor_reader_tools_40.bits, icons::Id::Gear, icon_tenor_reader_tools_40.w, icon_tenor_reader_tools_40.h},
    {icon_tenor_reader_position_bold_40.bits, icons::Id::Bookmark, icon_tenor_reader_position_bold_40.w, icon_tenor_reader_position_bold_40.h},
    {icon_tenor_reader_reading_bold_40.bits, icons::Id::Book, icon_tenor_reader_reading_bold_40.w, icon_tenor_reader_reading_bold_40.h},
    {icon_tenor_reader_tools_bold_40.bits, icons::Id::Gear, icon_tenor_reader_tools_bold_40.w, icon_tenor_reader_tools_bold_40.h},
    {icon_status_wifi_18.bits, icons::Id::Wifi, icon_status_wifi_18.w, icon_status_wifi_18.h},
    {icon_status_bluetooth_18.bits, icons::Id::Bluetooth, icon_status_bluetooth_18.w, icon_status_bluetooth_18.h},
    {icon_status_bluetooth_lost_18.bits, icons::Id::BluetoothLost, icon_status_bluetooth_lost_18.w, icon_status_bluetooth_lost_18.h},
    {icon_header_back_32.bits, icons::Id::Back, icon_header_back_32.w, icon_header_back_32.h},
  };
  for (const auto& asset : assets) {
    if (bits != asset.bits && !(nativeWidth == asset.width && nativeHeight == asset.height &&
        std::memcmp(bits, asset.bits, ((nativeWidth + 7) / 8) * nativeHeight) == 0)) continue;
    if (rowInk) rowIcon(renderer, asset.id, box, seed);
    else icon(renderer, asset.id, box, seed);
    return true;
  }
  return false;
}

bool iconSymbol(const GfxRenderer& renderer, int shape, int x, int y, int size, bool black) {
  if (!shell::isUgly()) return false;
  constexpr icons::Id ids[] = {icons::Id::Tick, icons::Id::Back, icons::Id::Up, icons::Id::Down,
    icons::Id::Left, icons::Id::Right, icons::Id::Star, icons::Id::Heart, icons::Id::Erase};
  if (shape < 0 || shape >= static_cast<int>(inlineSymbols::Shape::None)) return false;
  const int extent = std::max(10, size + 4);
  icon(renderer, ids[shape], {x - extent / 2, y - extent / 2, extent, extent},
       iconFrameSeed(), black);
  return true;
}

}
