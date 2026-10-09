#pragma once

#include "activities/reader/ReaderMenuLayout.h"
#include "tenorHomeTabIcons.h"
#include "tenorReaderTabIcons.h"
#include "tenorTextTabIcons.h"

namespace readermenu {

inline const freeink::Icon& x3TabIcon(const Tab tab, const bool selected) {
  switch (tab) {
    case Tab::POSITION: return selected ? icon_tenor_reader_position_bold_40 : icon_tenor_reader_position_40;
    case Tab::READING: return selected ? icon_tenor_reader_reading_bold_40 : icon_tenor_reader_reading_40;
    case Tab::TOOLS: return selected ? icon_tenor_reader_tools_bold_40 : icon_tenor_reader_tools_40;
    case Tab::TEXT: return selected ? icon_tenor_text_font_bold_40 : icon_tenor_text_font_40;
    case Tab::FAVORITES: break;
  }
  return selected ? icon_tenor_home_favorites_bold_40 : icon_tenor_home_favorites_40;
}

}  // namespace readermenu
