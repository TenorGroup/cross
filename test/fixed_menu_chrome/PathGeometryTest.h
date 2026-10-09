void check(bool passed, const char* message);
int pathGeometryCases = 0;
void checkX3PathGeometry() {
  const char* titles[] = {
      "Hiển thị", "Ngủ", "Trình đọc", "Điều khiển", "Cử chỉ", "Hệ thống", "Thiết bị", "Bàn phím", "Khác",
      "Tên thiết bị", "Mật khẩu Wi-Fi", "Địa chỉ máy chủ OPDS", "Gửi file", "Mạng", "Mạng Wi-Fi",
      "Bộ lật Bluetooth", "Mã QR", "Dấu trang", "Chọn chương", "Trích dẫn", "Sửa trích dẫn",
      "Thói quen đọc", "Lịch sử đọc", "Gán lại nút mặt trước", "Cài đặt văn bản", "Thanh trạng thái chung",
      "Tùy chỉnh thanh trạng thái", "Ngôn ngữ", "Thông tin", "Ngày giờ", "Múi giờ", "Xóa cache mọi sách",
      "Tải font", "Máy chủ OPDS", "Cài đặt OPDS", "Cài đặt KOReader", "Đồng bộ KOReader", "jag",
      "Xác thực KOReader", "Calibre không dây", "Cập nhật firmware từ thẻ SD", "Cập nhật",
      "Thông tin cập nhật", "Số liệu sách", "Số liệu đọc", "Nút Home", "Chọn giá trị", "Cử chỉ nút Home"
  };
  for (const auto* title : titles) {
    GfxRenderer target;
    target.screenHeight = 792;
    const auto bar = tenorchrome::x3ScreenLayout(target).barRect;
    const int height = bar.height - 12;
    const int top = bar.y + 6;
    const std::vector<tenorchrome::X3PathNode> nodes = {
        {tenorchrome::X3PathIcon::Settings, ""}, {tenorchrome::X3PathIcon::Device, "Thiết bị"},
        {tenorchrome::X3PathIcon::Keyboard, title}};
    const auto pills = tenorchrome::layoutX3Navigation(target, UI_12_FONT_ID, nodes, bar.width - 32, height);
    tenorchrome::drawX3NavigationPath(target, nodes, false);
    check(pills.size() == 3 && pills[0].width == height && pills[1].width == height,
          "root and nearest parent share circular height");
    check(!target.paintedText.empty() && target.paintedText.back() == pills.back().text,
          "shared painter uses the layout current title");
    check(target.textFontIds.back() == UI_12_FONT_ID, "every child path uses the same font");
    const auto* drawn = target.paintedText.back().c_str();
    const int inkTop = target.getTextInkTop(UI_12_FONT_ID, drawn, EpdFontFamily::REGULAR);
    const int inkBottom = target.getTextInkBottom(UI_12_FONT_ID, drawn, EpdFontFamily::REGULAR);
    const auto bounds = target.getTextInkBounds(UI_12_FONT_ID, drawn, EpdFontFamily::REGULAR);
    const int delta = 2 * target.ys.back() + bounds[1] + bounds[3] - 2 * top - height;
    check(delta >= -1 && delta <= 0, "path text ink center is within 0.5 px");
    int pixelTop = 792, pixelBottom = -1;
    const int textLeft = 16 + 2 * (height + tenorchrome::X3_PATH_GAP) + tenorchrome::X3_PATH_PADDING;
    for (int y = target.ys.back() + inkTop; y < target.ys.back() + inkBottom; ++y)
      for (int x = textLeft; x < textLeft + target.getTextWidth(UI_12_FONT_ID, drawn); ++x)
        if (target.pixels[y * 528 + x]) {
          pixelTop = std::min(pixelTop, y);
          pixelBottom = std::max(pixelBottom, y);
        }
    const int pixelDelta = pixelTop + pixelBottom - (2 * top + height - 1);
    check(pixelBottom >= pixelTop && pixelDelta >= -1 && pixelDelta <= 0,
          "actual body-font bitmap ink has a consistent upward bias within 0.5 px");
    int pixelLeft = 528, pixelRight = -1;
    const int pillLeft = 16 + 2 * (height + tenorchrome::X3_PATH_GAP);
    for (int y = target.ys.back() + inkTop; y < target.ys.back() + inkBottom; ++y)
      for (int x = pillLeft + 10; x < pillLeft + pills.back().width - 10; ++x)
        if (target.pixels[y * 528 + x]) {
          pixelLeft = std::min(pixelLeft, x);
          pixelRight = std::max(pixelRight, x);
        }
    const int horizontalDelta = pixelLeft + pixelRight - (2 * pillLeft + pills.back().width - 1);
    if (horizontalDelta < -1 || horizontalDelta > 0) {
      const auto inkX = target.getTextInkBounds(UI_12_FONT_ID, drawn, EpdFontFamily::REGULAR);
      std::fprintf(stderr, "label=%s pixels=%d,%d ink=%d,%d delta=%d origin=%d width=%d left=%d\n",
                   drawn, pixelLeft, pixelRight, inkX[0], inkX[2], horizontalDelta, target.textXs.back(), pills.back().width, pillLeft);
    }
    check(pixelRight >= pixelLeft && horizontalDelta >= -1 && horizontalDelta <= 0,
          "actual body-font bitmap ink is horizontally centred in the pill");
    check(pills.back().width == target.getTextWidth(UI_12_FONT_ID, drawn) + 28,
          "current pill hugs the displayed glyph width");
    check(pills[0].width + pills[1].width + pills[2].width + 16 <= bar.width - 32,
          "path and common gaps stay inside the bar");
    check(target.placedBars.size() == 1 && target.placedBars[0][1] == bar.y &&
              target.placedBars[0][3] == bar.height, "path uses the exact shared barRect");
    ++pathGeometryCases;
  }
}
