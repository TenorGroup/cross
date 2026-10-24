#include "KOReaderSettingsActivity.h"
#include "activities/settings/X3SettingsLayout.h"

#include <GfxRenderer.h>
#include <HttpHeader.h>
#include <I18n.h>

#include <memory>
#include <string>
#include <utility>

#include "KOReaderAuthActivity.h"
#include "KOReaderAutoSync.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "MenuFavorites.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"

namespace fui = freeink::ui;

namespace {
using R = KOReaderSettingsActivity;

const StrId menuNames[R::MENU_ITEMS] = {
    StrId::STR_USERNAME,        StrId::STR_PASSWORD,        StrId::STR_SYNC_SERVER_URL,   StrId::STR_SERVER_TYPE,
    StrId::STR_CUSTOM_HEADER_1, StrId::STR_CUSTOM_HEADER_2, StrId::STR_DOCUMENT_MATCHING, StrId::STR_SEND_METADATA,
    StrId::STR_SYNC_BEHAVIOR,   StrId::STR_AUTO_SYNC,       StrId::STR_SYNC_CLIPPINGS,    StrId::STR_SIGN_UP,
    StrId::STR_AUTHENTICATE};

constexpr int SERVER_TYPE_ITEMS = 3;
constexpr StrId serverTypeNames[SERVER_TYPE_ITEMS] = {StrId::STR_CROSSPOINT_SERVER, StrId::STR_KOSYNC_SERVER,
                                                      StrId::STR_OTHER};
}  // namespace

KOReaderSettingsActivity::KOReaderSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("KOReaderSettings", renderer, mappedInput) {
  // Labels never change (unlike the values, which track live KOREADER_STORE
  // state), so they're set once here rather than every buildScreen() call.
  for (int i = 0; i < MENU_ITEMS; i++) {
    rowItems_[i].label = I18N.get(menuNames[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

int KOReaderSettingsActivity::listCount() const { return MENU_ITEMS; }

const char* KOReaderSettingsActivity::headerTitle() const { return tr(STR_KOREADER_SYNC); }

void KOReaderSettingsActivity::render(RenderLock&& lock) {
  if (optionPopup_.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

void KOReaderSettingsActivity::activateIndex(const int index) {
  if (optionPopup_.isActive()) return;
  // Activation opens a keyboard/sub-activity or repaints a new value; a
  // lingering flash would gray an unrelated row.
  app.clearTapFlash();
  if (rowChoiceStyle(index) == SettingsChoiceStyle::Popup) {
    static constexpr StrId matching[] = {StrId::STR_FILENAME, StrId::STR_BINARY};
    static constexpr StrId behavior[] = {StrId::STR_ASK_EVERY_TIME, StrId::STR_SMART_SYNC};
    if (index == R::ROW_SERVER_TYPE) {
      showSettingsChoices(optionPopup_, StrId::STR_SERVER_TYPE, serverTypeNames, SERVER_TYPE_ITEMS,
                          static_cast<int>(KOREADER_STORE.getServerType()), index, [this](const int selected) {
                            if (selected != static_cast<int>(KOREADER_STORE.getServerType())) {
                              KOREADER_STORE.setServerType(static_cast<KOReaderServerType>(selected));
                              KOREADER_STORE.saveToFile();
                            }
                            requestUpdate();
                          });
      return;
    }
    const bool match = index == R::ROW_MATCH_METHOD;
    const int selected = match ? (KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::BINARY)
                               : (KOREADER_STORE.getSyncBehavior() == KOReaderSyncBehavior::SMART);
    showSettingsChoices(optionPopup_, menuNames[index], match ? matching : behavior, settingsChoiceCount(index),
                        selected, index, [this, match](const int value) {
      if (match) KOREADER_STORE.setMatchMethod(value ? DocumentMatchMethod::BINARY : DocumentMatchMethod::FILENAME);
      else KOREADER_STORE.setSyncBehavior(value ? KOReaderSyncBehavior::SMART : KOReaderSyncBehavior::ASK_EVERY_TIME);
      KOREADER_STORE.saveToFile();
      requestUpdate();
    });
    return;
  }
  if (index == R::ROW_USERNAME) {
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_KOREADER_USERNAME),
                                                                   KOREADER_STORE.getUsername(), 64, InputType::Text),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               KOREADER_STORE.setCredentials(kb.text, KOREADER_STORE.getPassword());
                               KOREADER_STORE.saveToFile();
                             }
                           });
  } else if (index == R::ROW_PASSWORD) {
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_KOREADER_PASSWORD),
                                                                   KOREADER_STORE.getPassword(), 64,
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
                                                                   InputType::Password),
#else
                                                                   InputType::Text),
#endif
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), kb.text);
                               KOREADER_STORE.saveToFile();
                             }
                           });
  } else if (index == R::ROW_SERVER_URL) {
    // Prefill with https:// if empty to save typing.
    const std::string currentUrl = KOREADER_STORE.getServerUrl();
    const std::string prefillUrl = currentUrl.empty() ? "https://" : currentUrl;
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SYNC_SERVER_URL),
                                                                   prefillUrl, 128, InputType::Url),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               const std::string urlToSave =
                                   (kb.text == "https://" || kb.text == "http://") ? "" : kb.text;
                               KOREADER_STORE.setServerUrl(urlToSave);
                               KOREADER_STORE.saveToFile();
                             }
                           });
  } else if (index == R::ROW_CUSTOM_HEADER_1 || index == R::ROW_CUSTOM_HEADER_2) {
    // Single "Name: Value" line, parsed on save.
    const size_t slot = static_cast<size_t>(index == R::ROW_CUSTOM_HEADER_1 ? 0 : 1);
    const StrId label = index == R::ROW_CUSTOM_HEADER_1 ? StrId::STR_CUSTOM_HEADER_1 : StrId::STR_CUSTOM_HEADER_2;
    const std::string prefill = formatHttpHeaderLine(KOREADER_STORE.getCustomHeaders()[slot]);
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, I18N.get(label), prefill, 160, InputType::Text),
        [this, slot](const ActivityResult& result) {
          if (!result.isCancelled) {
            const auto& kb = std::get<KeyboardResult>(result.data);
            HttpHeader header;
            parseHttpHeaderLine(kb.text, header);
            KOREADER_STORE.setCustomHeader(slot, header.name, header.value);
            KOREADER_STORE.saveToFile();
          }
        });
  } else if (index == R::ROW_SEND_METADATA) {
    KOREADER_STORE.setSendMetadata(!KOREADER_STORE.getSendMetadata());
    KOREADER_STORE.saveToFile();
    requestUpdate();
  } else if (index == R::ROW_AUTO_SYNC) {
    // Off by default; switching it off drops the pending automatic sync.
    KOREADER_STORE.setAutoSync(!KOREADER_STORE.getAutoSync());
    if (!KOREADER_STORE.getAutoSync()) koautosync::switchedOff();
    KOREADER_STORE.saveToFile();
    requestUpdate();
  } else if (index == R::ROW_SYNC_CLIPPINGS) {
    KOREADER_STORE.setSyncClippings(!KOREADER_STORE.getSyncClippings());
    KOREADER_STORE.saveToFile();
    requestUpdate();
  } else if (index == R::ROW_SIGN_UP) {
    // Create a new account on the sync server with the entered credentials.
    if (!KOREADER_STORE.hasCredentials()) return;
    startActivityForResult(
        std::make_unique<KOReaderAuthActivity>(renderer, mappedInput, KOReaderAuthActivity::Mode::SIGN_UP),
        [](const ActivityResult&) {});
  } else if (index == R::ROW_AUTHENTICATE) {
    // Can't authenticate without credentials.
    if (!KOREADER_STORE.hasCredentials()) return;
    startActivityForResult(std::make_unique<KOReaderAuthActivity>(renderer, mappedInput), [](const ActivityResult&) {});
  }
}

void KOReaderSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  if (x3settings::beginScreen(screen, renderer)) {
    tabBandDrawn = true;
  } else {
    screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                  static_cast<int16_t>(metrics.buttonHintsHeight), 0});
    screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  }

  // rowItems_'s labels/actionValue were set once in the constructor; only the
  // live value text needs refreshing here, by assigning into the existing
  // rowValues_ strings (no array growth) rather than building a new
  // items/values vector on every render.
  for (int i = 0; i < MENU_ITEMS; i++) {
    if (i == R::ROW_USERNAME) {
      const auto username = KOREADER_STORE.getUsername();
      rowValues_[i] = username.empty() ? tr(STR_NOT_SET) : username;
    } else if (i == R::ROW_PASSWORD) {
      rowValues_[i] = KOREADER_STORE.getPassword().empty() ? tr(STR_NOT_SET) : "******";
    } else if (i == R::ROW_SERVER_URL) {
      rowValues_[i] = KOREADER_STORE.getServerUrl();
      if (rowValues_[i].empty()) {
        // Show which server the default actually is, scheme stripped for space
        std::string defaultUrl = KOREADER_STORE.getBaseUrl();
        const auto schemeEnd = defaultUrl.find("://");
        if (schemeEnd != std::string::npos) defaultUrl.erase(0, schemeEnd + 3);
        rowValues_[i] = std::string(tr(STR_DEFAULT_VALUE)) + ": " + defaultUrl;
      }
    } else if (i == R::ROW_SERVER_TYPE) {
      const auto type = KOREADER_STORE.getServerType();
      rowValues_[i] = type == KOReaderServerType::CROSSPOINT ? tr(STR_CROSSPOINT_SERVER)
                      : type == KOReaderServerType::KOSYNC   ? tr(STR_KOSYNC_SERVER)
                                                             : tr(STR_OTHER);
    } else if (i == R::ROW_CUSTOM_HEADER_1 || i == R::ROW_CUSTOM_HEADER_2) {
      const size_t slot = static_cast<size_t>(i == R::ROW_CUSTOM_HEADER_1 ? 0 : 1);
      rowValues_[i] = formatHttpHeaderLine(KOREADER_STORE.getCustomHeaders()[slot], true);
      if (rowValues_[i].empty()) rowValues_[i] = tr(STR_NOT_SET);
    } else if (i == R::ROW_MATCH_METHOD) {
      rowValues_[i] =
          KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME ? tr(STR_FILENAME) : tr(STR_BINARY);
    } else if (i == R::ROW_SEND_METADATA) {
      rowValues_[i] = KOREADER_STORE.getSendMetadata() ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    } else if (i == R::ROW_SYNC_BEHAVIOR) {
      rowValues_[i] =
          KOREADER_STORE.getSyncBehavior() == KOReaderSyncBehavior::SMART ? tr(STR_SMART_SYNC) : tr(STR_ASK_EVERY_TIME);
    } else if (i == R::ROW_AUTO_SYNC) {
      rowValues_[i] = KOREADER_STORE.getAutoSync() ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    } else if (i == R::ROW_SYNC_CLIPPINGS) {
      rowValues_[i] = KOREADER_STORE.getSyncClippings() ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    } else {
      rowValues_[i] = KOREADER_STORE.hasCredentials() ? "" : std::string("[") + tr(STR_SET_CREDENTIALS_FIRST) + "]";
    }
    // Account actions have a full-width label. Their prerequisite belongs
    // below it, so the longer Vietnamese hint cannot squeeze Log in/Sign up.
    const bool accountAction = i >= R::ROW_SIGN_UP;
    rowItems_[i].value = accountAction || rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
    rowItems_[i].subtitle = accountAction && !rowValues_[i].empty() ? rowValues_[i].c_str() : nullptr;
    rowItems_[i].toggle = rowChoiceStyle(i) == SettingsChoiceStyle::Inline;
    rowItems_[i].toggleChecked = i == R::ROW_SEND_METADATA   ? KOREADER_STORE.getSendMetadata()
                                 : i == R::ROW_AUTO_SYNC      ? KOREADER_STORE.getAutoSync()
                                 : i == R::ROW_SYNC_CLIPPINGS ? KOREADER_STORE.getSyncClippings()
                                                              : false;
  }

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(MENU_ITEMS);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  props.labelText = uiMenuLabelText(screen.theme());
  props.labelText.maxLines = 2;  // a label that does not fit goes to a second line, as on the X4 Pro
  props.balanceWrappedLabelWithValue = !tenorchrome::x3ScreenLayoutEnabled();
  props.subtitleText = screen.theme().smallText;
  syncListViewport(screen, props);
  screen.list(props);
}

std::string KOReaderSettingsActivity::favoriteKey(int row) const { return menufavorites::keyFor("kosync", 0, row); }
