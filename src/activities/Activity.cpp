#include "Activity.h"

#include <algorithm>

#include "ActivityManager.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "fontIds.h"

void Activity::onEnter() { LOG_DBG("ACT", "Entering activity: %s", name.c_str()); }

void Activity::onExit() { LOG_DBG("ACT", "Exiting activity: %s", name.c_str()); }

void Activity::requestUpdate(bool immediate) { activityManager.requestUpdate(immediate); }

void Activity::requestUpdateAndWait() { activityManager.requestUpdateAndWait(); }

void Activity::onGoHome(HomeMenuItem item) { activityManager.goHome(item); }

void Activity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void Activity::startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler) {
  if (!activity) {
    LOG_ERR("ACT", "Cannot push an unallocated activity");
    return;
  }
  this->resultHandler = std::move(resultHandler);
  activityManager.pushActivity(std::move(activity));
}

void Activity::setResult(ActivityResult&& result) { this->result = std::move(result); }

void Activity::finish() { activityManager.popActivity(); }

void Activity::drawNavigationHeader(const char* title) {
  if (tenorchrome::enabled()) {
    tenorchrome::drawHeader(renderer, title, navigationPrefix.c_str());
    return;
  }
  const auto& m = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, m.topPadding, renderer.getScreenWidth(), m.headerHeight}, title);
}
