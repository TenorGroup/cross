#include "ListOrderActivity.h"

#include <I18n.h>

#include "activities/RenderLock.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"

ListOrderActivity::ListOrderActivity(GfxRenderer& renderer, MappedInputManager& input, std::vector<std::string> labels,
                                     const int moving, std::function<void(const std::vector<int>&)> onDone)
    : UiListActivity("ListOrder", renderer, input), labels_(std::move(labels)), rows_(labels_.size()),
      moving_(moving), onDone_(std::move(onDone)) {
  for (int i = 0; i < static_cast<int>(labels_.size()); ++i) order_.push_back(i);
  if (moving_ < 0 || moving_ >= listCount()) moving_ = 0;
}

void ListOrderActivity::onEnter() {
  UiListActivity::onEnter();
  tenorchrome::noteFootActions(tr(STR_DONE));
  nav.selected = moving_;
  nav.followOnBuild = true;
}

const char* ListOrderActivity::headerTitle() const { return tr(STR_REORDER); }

void ListOrderActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(freeink::ui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight),
                                                        0, static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  for (int row = 0; row < listCount(); ++row) {
    rows_[row].label = labels_[order_[row]].c_str();
    rows_[row].actionValue = static_cast<int16_t>(row);
  }
  freeink::ui::ListProps props;
  props.items = rows_.data();
  props.count = static_cast<uint16_t>(rows_.size());
  props.action = ACTION_ROW;
  props.inputMask = freeink::ui::InputTouch;
  props.labelText = uiMenuLabelText(screen.theme());
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  // The one row with a cursor here: the item being moved, in the ring of the button readers' cursor (no black block).
  props.selectedIndex = static_cast<int16_t>(moving_);
  props.rowStyles = freeink::ui::defaultListRowStyles();
  tenorPillSelection(props.rowStyles);
  props.rowRadius = TENOR_PILL_RADIUS;
  // and, as that cursor does in a framed list, 16 px inside the frame's ring, its words the pill's padding in.
  props.rowInset = static_cast<int16_t>(tenorchrome::frameSpan(renderer).x + 16);
  props.sidePadding = TENOR_PILL_SIDE_PADDING;
  screen.list(props);
}

void ListOrderActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount() || index == moving_) return;
  {
    RenderLock lock(*this);
    const int item = order_[moving_];
    order_.erase(order_.begin() + moving_);
    order_.insert(order_.begin() + index, item);
    moving_ = index;
  }
  requestUpdate();
}

bool ListOrderActivity::handleCustomInput() {
  int x = 0, y = 0;
  if (!mappedInput.wasScreenTapped(x, y) || tenorchrome::footActionAt(x, y) != 0) return false;
  onDone_(order_);
  finish();
  return true;
}
