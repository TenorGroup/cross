#include "KOReaderAutoSync.h"

#include <Arduino.h>
#include <BlePageTurner.h>
#include <HalClock.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <HalMemory.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>
#include <WiFi.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <memory>
#include <optional>
#include <vector>

#include "KOReaderAutoSyncPolicy.h"
#include "KOReaderPullResume.h"
#include "BlePageTurnerHost.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderDocumentId.h"
#include "KOReaderSyncClient.h"
#include "KOReaderSyncDebt.h"
#include "KOReaderSyncMark.h"
#include "KOReaderWifiHint.h"
#include "ProgressComparison.h"
#include "ProgressMapper.h"
#include "WifiCredentialStore.h"
#include "activities/ActivityManager.h"
#include "CrossPointState.h"
#include "CrossPointSettings.h"
#include "platform/ColdLogProbe.h"
#include "activities/RenderLock.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "activities/reader/EpubReaderUtils.h"

namespace koautosync {
namespace {

using namespace kosyncauto;

constexpr char DEBT_FILE[] = "/.crosspoint/kosync-no.json";
constexpr char HINT_FILE[] = "/.crosspoint/kosync-wifi.json";
constexpr size_t DEBT_FILE_MAX_BYTES = 8192;  // 4 books of long paths stay far under this
constexpr char MARK_FILE[] = "/.crosspoint/kosync-last.json";
constexpr size_t MARK_FILE_MAX_BYTES = 2048;  // 8 marks of about 70 bytes

SyncDebt pendingNote;
bool hasPendingNote = false;
SyncMark pendingMark;
std::string pendingMarkPath;
bool hasPendingMark = false;
bool markAfterFramePending = false;
PullChange pullChange;
bool hasPullChange = false;
// The pull owed to the first epub opened from Home (PullDebt); RTC memory keeps it across the quiet restart.
RTC_NOINIT_ATTR uint32_t pullDebtWord;
PullDebt pullDebt() { return pullDebtFromStored(pullDebtWord); }
void setPullDebt(const PullDebt debt) { pullDebtWord = static_cast<uint32_t>(debt); }
#if FREEINK_DEVICE_X3
RTC_NOINIT_ATTR PullResume keptPull;
#else
PullResume keptPull;
#endif
bool pullRestartPending = false;
bool restoredFrame = false;
bool usedPullNetwork = false;

#if FREEINK_DEVICE_X3
// The wake pull runs before the reader's first frame. Keep a failed fast-join run in RTC so the next
// round can skip the stale AP hint without adding an SD write to that pre-frame path.
struct HintFailureMemo {  // plain aggregate like PullResume: no constructor runs over RTC memory at boot
  uint32_t magic;
  uint32_t key;
  uint8_t failures;
  uint8_t reset;
};
constexpr uint32_t HINT_FAILURE_MAGIC = 0x4B48464Du;
RTC_NOINIT_ATTR HintFailureMemo hintFailureMemo;
WifiHint pendingHint;
bool hasPendingHint = false;
#endif

std::string documentHash(const std::string& path);

#ifdef TENOR_PRESS_PROBE
RTC_NOINIT_ATTR uint32_t probeMinSleepMagic;
RTC_NOINIT_ATTR uint32_t probeMinSleepSeconds;
constexpr uint32_t PROBE_MIN_SLEEP_MAGIC = 0x4B41534D;
bool probeHeapPending = false;

void probeHeap(const char* stage) {
  LOG_INF("KAS", "HEAP_INFO stage=%s free=%u largest=%u min=%u", stage, static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()));
}
#endif

// The 30 minute rule; a measuring build can lower it so a timed wake of a few seconds counts.
uint32_t minSleepSeconds() {
#ifdef TENOR_PRESS_PROBE
  if (probeMinSleepMagic == PROBE_MIN_SLEEP_MAGIC && probeMinSleepSeconds > 0) return probeMinSleepSeconds;
#endif
  return PULL_MIN_SLEEP_SECONDS;
}

int32_t msLeft(const uint32_t deadline) { return static_cast<int32_t>(deadline - millis()); }

// false: the card could not be read right now, so nothing may be written over the file.
bool loadDebts(SyncDebtList& list) {
  JsonDocument doc;
  switch (PersistableStoreBase::readDocFromFileStatus(DEBT_FILE, doc, DEBT_FILE_MAX_BYTES)) {
    case PersistableStoreBase::ReadResult::Ready:
      list.fromJson(doc);
      return true;
    case PersistableStoreBase::ReadResult::Invalid:
      return true;  // missing or damaged and no recovery copy: nothing is owed
    case PersistableStoreBase::ReadResult::Unavailable:
      return false;
  }
  return false;
}

bool saveDebts(const SyncDebtList& list) {
  if (list.empty()) {
    Storage.remove(DEBT_FILE);
    return true;
  }
  JsonDocument doc;
  list.toJson(doc);
  return PersistableStoreBase::writeDocToFile(DEBT_FILE, doc);
}

// false: the card could not be read right now, so nothing may be written over the file.
bool loadMarks(SyncMarkList& list) {
  JsonDocument doc;
  switch (PersistableStoreBase::readDocFromFileStatus(MARK_FILE, doc, MARK_FILE_MAX_BYTES)) {
    case PersistableStoreBase::ReadResult::Ready:
      list.fromJson(doc);
      return true;
    case PersistableStoreBase::ReadResult::Invalid:
      return true;  // missing or damaged: no book has a mark, the decisions fall back to the plain comparison
    case PersistableStoreBase::ReadResult::Unavailable:
      return false;
  }
  return false;
}

bool saveMarks(const SyncMarkList& list) {
  JsonDocument doc;
  list.toJson(doc);
  return PersistableStoreBase::writeDocToFile(MARK_FILE, doc);
}

// Writes the mark the wake pull left in memory, after the book's first frame or at
// sleep. A card that cannot be read keeps it for the next try.
void flushMark() {
  if (!hasPendingMark) return;
  if (pendingMark.document.empty()) {
    const std::string hash = documentHash(pendingMarkPath);
    if (hash.size() != 32) {
      LOG_ERR("KAS", "Could not queue an invalid document mark");
      return;
    }
    pendingMark.document = hash;
    pendingMarkPath.clear();
  }
  if (pendingMark.document.size() != 32) {
    LOG_ERR("KAS", "Could not queue an invalid document mark");
    return;
  }
  SyncMarkList list;
  if (!loadMarks(list)) return;
  hasPendingMark = false;
  pendingMarkPath.clear();
  list.put(std::move(pendingMark));
  if (!saveMarks(list)) LOG_ERR("KAS", "Could not write %s", MARK_FILE);
}

// BLE and Wi-Fi do not share the radio on the X3. True when the page turner's radio is down.
bool radioIsFree() {
  auto up = [] {
    const auto ble = bleturner::status();
    return ble.running || ble.starting || ble.stopping;
  };
#ifdef TENOR_PRESS_PROBE
  const auto before = bleturner::status();
  restartlog::trace(restartlog::TraceStage::RadioBefore,
                    (before.running ? 1 : 0) | (before.starting ? 2 : 0) | (before.stopping ? 4 : 0),
                    ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
  if (up()) bleturner::beforeSleep(1000);
#ifdef TENOR_PRESS_PROBE
  const auto after = bleturner::status();
  restartlog::trace(restartlog::TraceStage::RadioAfter,
                    (after.running ? 1 : 0) | (after.starting ? 2 : 0) | (after.stopping ? 4 : 0),
                    ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
  return !up();
}

// THE gate for both directions. The inputs are read in order and only while the earlier ones pass, so
// with the switch off nothing is read from the card and no radio state is touched.
// `onHome`: the pull for a book opened from Home asks before the restart, where the heap is the fragmented
// one and the page turner may be on; the radio and the heap are asked again by the pull on the fresh start.
Eligibility gate(const bool onHome = false) {
  const bool enabled = KOREADER_STORE.getAutoSync();
  const bool account = enabled && KOREADER_STORE.hasCredentials();
  bool network = false;
  if (account) {
    WIFI_STORE.loadFromFile();
    network = WIFI_STORE.getCredentialCount() > 0;
  }
  const bool radio = network && (onHome || radioIsFree());
  HalMemory::HeapStats heap{MIN_FREE_FOR_TLS, 0, 0, MIN_BLOCK_FOR_TLS};
  if (radio && !onHome) heap = HalMemory::getDefaultHeap();
  return eligibility(enabled, account, network, heap.freeBytes, heap.largestBlockBytes, radio);
}

const char* why(const Eligibility e) {
  switch (e) {
    case Eligibility::Ready: return "ready";
    case Eligibility::Disabled: return "off";
    case Eligibility::NoAccount: return "no account";
    case Eligibility::NoNetwork: return "no saved network";
    case Eligibility::RadioBusy: return "radio busy";
    case Eligibility::LowHeap: return "low heap";
  }
  return "?";
}

#if FREEINK_DEVICE_X3
uint32_t hintKey(const WifiHint& hint) {
  uint32_t key = 2166136261u;
  for (const char c : hint.ssid) key = (key ^ static_cast<uint8_t>(c)) * 16777619u;
  key = (key ^ hint.channel) * 16777619u;
  for (const uint8_t byte : hint.bssid) key = (key ^ byte) * 16777619u;
  return key;
}
#endif

WifiHint loadHint() {
  WifiHint hint;
  JsonDocument doc;
  if (PersistableStoreBase::readDocFromFileStatus(HINT_FILE, doc, 512) == PersistableStoreBase::ReadResult::Ready) {
    hint.fromJson(doc);  // empty on a damaged file
  }
#if FREEINK_DEVICE_X3
  if (hint.wellFormed() && hintFailureMemo.magic == HINT_FAILURE_MAGIC && hintFailureMemo.key == hintKey(hint)) {
    hint.fastJoinFailures = hintFailureMemo.reset ? 0 : std::max(hint.fastJoinFailures, hintFailureMemo.failures);
  }
#endif
  return hint;
}

#if FREEINK_DEVICE_X3
void rememberHintFailure(const WifiHint& hint) {
  hintFailureMemo = {HINT_FAILURE_MAGIC, hintKey(hint), hint.fastJoinFailures, 0};
  pendingHint = hint;
  hasPendingHint = true;
}

void flushHint() {
  if (!hasPendingHint) return;
  JsonDocument doc;
  pendingHint.toJson(doc);
  if (!PersistableStoreBase::writeDocToFile(HINT_FILE, doc)) return;
  hasPendingHint = false;
  hintFailureMemo = {};
}
#else
void dropHint() { Storage.remove(HINT_FILE); }
#endif

// Remembers where the network that just connected is, when that differs from what is stored.
void rememberHint(const std::string& ssid, const WifiHint& stored) {
  WifiHint now;
  now.ssid = ssid;
  now.channel = static_cast<uint8_t>(WiFi.channel());
  if (const uint8_t* bssid = WiFi.BSSID()) memcpy(now.bssid, bssid, sizeof(now.bssid));
  if (!now.wellFormed()) return;
#if FREEINK_DEVICE_X3
  const bool sameHint = stored.wellFormed() && hintKey(stored) == hintKey(now);
  if (sameHint) {
    // A scan fallback connected to the same AP. It did not prove that the fast hint works, so keep
    // the failure run and let the next round scan directly again.
    return;
  }
  if (stored.wellFormed()) {
    // The old file can still be on the card if a restart happens before the first frame. Keep that
    // stale AP disabled in RTC until the new hint is flushed after the first frame.
    hintFailureMemo = {HINT_FAILURE_MAGIC, hintKey(stored), 2, 0};
  } else {
    hintFailureMemo = {HINT_FAILURE_MAGIC, hintKey(now), 0, 1};
  }
  pendingHint = now;
  hasPendingHint = true;
#else
  if (now == stored) return;
  JsonDocument doc;
  now.toJson(doc);
  PersistableStoreBase::writeDocToFile(HINT_FILE, doc);
#endif
}

bool waitConnected(const uint32_t until) {
  while (msLeft(until) > 0) {
    const wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) return true;
    if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) return false;
    delay(50);
  }
  return WiFi.status() == WL_CONNECTED;
}

// Joins a saved network without any screen: the last one that worked first, then the others, each
// until the deadline. A remembered channel and access point (WifiHint) go first; after one failed fast
// attempt, the next round scans directly. false when none connects in time.
bool joinSavedNetwork(const uint32_t deadline) {
  const uint32_t began = millis();
  std::vector<WifiCredential> order;
  const std::string last = WIFI_STORE.getLastConnectedSsid();
  if (!last.empty()) {
    if (const auto cred = WIFI_STORE.findCredential(last)) order.push_back(*cred);
  }
  for (size_t i = 0; i < WIFI_STORE.getCredentialCount(); ++i) {
    const auto cred = WIFI_STORE.getCredentialAt(i);
    if (cred && cred->ssid != last) order.push_back(*cred);
  }
  WifiHint hint = loadHint();

  WiFi.persistent(false);
  powerManager.setPowerSaving(false);
  WiFi.mode(WIFI_STA);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
#ifdef TENOR_PRESS_PROBE
  restartlog::trace(restartlog::TraceStage::JoinStart, static_cast<int32_t>(order.size()), ESP.getFreeHeap(),
                    ESP.getMaxAllocHeap());
#endif
  for (const auto& cred : order) {
    if (msLeft(deadline) <= 0) break;
    const char* password = cred.password.empty() ? nullptr : cred.password.c_str();
#ifndef SIMULATOR  // the simulator's Wi-Fi has no channels
    if (hint.usableFor(cred.ssid)
#if FREEINK_DEVICE_X3
        && shouldTryFastJoin(hint.fastJoinFailures)
#endif
    ) {
      WiFi.disconnect(false, false);
      WiFi.begin(cred.ssid.c_str(), password, hint.channel, hint.bssid, true);
      const uint32_t fastUntil = std::min<uint32_t>(deadline, millis() + FAST_JOIN_MS);
      const bool fastConnected = waitConnected(fastUntil);
#ifdef TENOR_PRESS_PROBE
      restartlog::trace(restartlog::TraceStage::WifiHint,
                        fastConnected ? 1 : (msLeft(fastUntil) <= 0 ? -2 : static_cast<int32_t>(WiFi.status())),
                        ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
      if (fastConnected) {
        WiFi.setSleep(false);
#if FREEINK_DEVICE_X3
        hint.fastJoinFailures = 0;
        pendingHint = hint;
        hasPendingHint = true;
        hintFailureMemo = {HINT_FAILURE_MAGIC, hintKey(hint), 0, 1};
#endif
        LOG_INF("KAS", "join fast in %lu ms", static_cast<unsigned long>(millis() - began));
        return true;
      }
#if FREEINK_DEVICE_X3
      hint.fastJoinFailures = recordFastJoinFailure(hint.fastJoinFailures);
      rememberHintFailure(hint);
      LOG_INF("KAS", "remembered access point failed after %lu ms, failures=%u, scanning",
              static_cast<unsigned long>(millis() - began), static_cast<unsigned>(hint.fastJoinFailures));
#else
      LOG_INF("KAS", "remembered access point failed after %lu ms, dropped", static_cast<unsigned long>(millis() - began));
      dropHint();
#endif
    }
#endif
    if (msLeft(deadline) <= 0) break;
    WiFi.disconnect(false, false);
    if (password) {
      WiFi.begin(cred.ssid.c_str(), password);
    } else {
      WiFi.begin(cred.ssid.c_str());
    }
    const bool scanConnected = waitConnected(deadline);
#ifdef TENOR_PRESS_PROBE
    restartlog::trace(restartlog::TraceStage::WifiScan,
                      scanConnected ? 1 : (msLeft(deadline) <= 0 ? -2 : static_cast<int32_t>(WiFi.status())),
                      ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
    if (scanConnected) {
      WiFi.setSleep(false);
#ifdef TENOR_PRESS_PROBE
      restartlog::trace(restartlog::TraceStage::WifiConnected, static_cast<int32_t>(WiFi.status()), ESP.getFreeHeap(),
                        ESP.getMaxAllocHeap());
#endif
      rememberHint(cred.ssid, hint);
      LOG_INF("KAS", "join scan in %lu ms", static_cast<unsigned long>(millis() - began));
      return true;
    }
  }
  return false;
}

// restCpu: let the clock drop to the low-power one now that the radio is off. The sleep push says yes (the
// sleep after it holds its own Lock). The wake pull says no: it runs in setup() with no Lock held, and the
// Epub load, the mapping and the book open after it would run at 10 MHz; the main loop lowers the clock
// by itself after the idle time.
void leaveWifi(const bool restCpu) {
  KOReaderSyncClient::requestTimeoutMs = 0;
  KOReaderSyncClient::requestDeadlineMs = 0;
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
  }
  if (restCpu) powerManager.setPowerSaving(true);
}

// Same dispatch as the manual sync (KOReaderSyncActivity).
std::string documentHash(const std::string& path) {
  return KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME
             ? KOReaderDocumentId::calculateFromFilename(path)
             : KOReaderDocumentId::calculate(path);
}

// One request, bounded by what is left of the round.
void limitRequest(const uint32_t started) { KOReaderSyncClient::requestTimeoutMs = requestTimeoutMs(millis() - started); }

Fetch fetchRemote(const std::string& hash, KOReaderProgress& remote, const uint32_t started) {
  limitRequest(started);
  const auto result = KOReaderSyncClient::getProgress(hash, remote);
  LOG_INF("KAS", "GET result=%d http=%d", static_cast<int>(result), KOReaderSyncClient::lastHttpCode);
  if (result == KOReaderSyncClient::OK) return Fetch::Found;
  return result == KOReaderSyncClient::NOT_FOUND ? Fetch::NotFound : Fetch::Failed;
}

std::shared_ptr<Epub> loadEpubForMapping(const std::string& path) {
  auto epub = std::make_shared<Epub>(path, "/.crosspoint");
  epub->setupCacheDir();
  // Metadata only, no CSS, no rebuild if the cache is missing (as the manual sync does).
  if (!epub->load(false, true)) return nullptr;
  return epub;
}

enum class PushOutcome : uint8_t { Done, Keep, OutOfTime };

PushOutcome pushOne(const SyncDebt& debt, GfxRenderer& renderer, const uint32_t started, SyncMarkList& marks,
                    bool& marksChanged) {
  if (!canStartRequest(millis() - started)) return PushOutcome::OutOfTime;
  // "Gone" needs the card to answer (it holds this very folder); a read that fails keeps the debt.
  const bool cardAnswers = Storage.ready() && Storage.exists("/.crosspoint");
  const bool bookThere = cardAnswers && Storage.exists(debt.path.c_str());
  const std::string hash = bookThere ? documentHash(debt.path) : std::string();
  switch (checkBook(cardAnswers, bookThere, !hash.empty())) {
    case BookCheck::Gone: return PushOutcome::Done;  // the book is gone: nothing to push
    case BookCheck::Unreadable: return PushOutcome::Keep;
    case BookCheck::Ready: break;
  }

  KOReaderProgress remote{};
  const Fetch fetch = fetchRemote(hash, remote, started);
  // Percentages only: nothing is opened yet. Local ahead, or no server copy, pushes.
  const ProgressComparison comparison =
      fetch == Fetch::Found ? compareProgress(CrossPointPosition{}, debt.percentage, CrossPointPosition{},
                                              remote.percentage)
                            : ProgressComparison::Unknown;
  // A server record this machine left itself, with the place moved since (even backwards): push it.
  const uint32_t place = placeKey(debt.spineIndex, debt.pageNumber);
  const bool ours = fetch == Fetch::Found && marks.serverIsOurs(hash, remote.progress, remote.percentage);
  const PushAction action = pushAction(fetch, comparison, ours, marks.placeChanged(hash, place));
  LOG_INF("KAS", "push decision fetch=%d cmp=%d ours=%d action=%d local=%.4f remote=%.4f", static_cast<int>(fetch),
          static_cast<int>(comparison), ours, static_cast<int>(action), static_cast<double>(debt.percentage),
          static_cast<double>(remote.percentage));
  if (action == PushAction::KeepDebt) return PushOutcome::Keep;
  if (action == PushAction::DropDebt) return PushOutcome::Done;
  if (!canStartRequest(millis() - started)) return PushOutcome::OutOfTime;

  CrossPointPosition position = {debt.spineIndex, debt.pageNumber, debt.totalPages};
  position.hasResolvedSpineIndex = true;
  position.hasMappedPage = true;
  position.visibleTextOffset = debt.visibleTextOffset;
  position.hasVisibleTextOffset = debt.hasVisibleTextOffset;
  position.paragraphIndex = debt.paragraphIndex;
  position.hasParagraphIndex = debt.hasParagraphIndex;

  KOReaderProgress upload;
  {
    auto epub = loadEpubForMapping(debt.path);
    if (!epub) return PushOutcome::Keep;
    SavedProgressPosition saved;
    {
      RenderLock lock;
      GfxRenderer::FrameBufferLoan loan(renderer);
      saved = ProgressMapper::toSavedProgress(epub, position);
    }
    upload = ProgressMapper::toUploadRecord(hash, saved, position, debt.path, epub.get());
  }  // the Epub is released before the TLS handshake

  limitRequest(started);
  const auto result = KOReaderSyncClient::updateProgress(upload);
  LOG_INF("KAS", "PUT result=%d http=%d", static_cast<int>(result), KOReaderSyncClient::lastHttpCode);
  if (result != KOReaderSyncClient::OK) return PushOutcome::Keep;
  marks.put({hash, recordToken(upload.progress, upload.percentage), place});
  marksChanged = true;
  return PushOutcome::Done;
}

}  // namespace

bool wantsNote() { return KOREADER_STORE.getAutoSync() && KOREADER_STORE.hasCredentials(); }

void noteLeftBook(const std::string& path, const CrossPointPosition& position, const float percentage,
                  const bool sleeping) {
  if (path.empty() || !std::isfinite(percentage)) return;
  if (position.spineIndex < 0 || position.spineIndex > UINT16_MAX || position.pageNumber < 0 ||
      position.pageNumber > UINT16_MAX || position.totalPages < 0 || position.totalPages > UINT16_MAX) {
    return;
  }
  SyncDebt debt;
  debt.path = path;
  debt.spineIndex = static_cast<uint16_t>(position.spineIndex);
  debt.pageNumber = static_cast<uint16_t>(position.pageNumber);
  debt.totalPages = static_cast<uint16_t>(position.totalPages);
  debt.visibleTextOffset = position.visibleTextOffset;
  debt.hasVisibleTextOffset = position.hasVisibleTextOffset;
  debt.paragraphIndex = position.paragraphIndex;
  debt.hasParagraphIndex = position.hasParagraphIndex;
  debt.percentage = std::clamp(percentage, 0.0f, 1.0f);
  const time_t now = time(nullptr);
  debt.leftAt = now >= 1735689600 ? static_cast<uint32_t>(now) : 0;  // 0 while the clock is unset
  pendingNote = std::move(debt);
  hasPendingNote = true;
  if (sleeping) {
    flushNote();  // the device powers down next: write now
  } else {
    activityManager.deferWrite(&flushNote);  // after the next screen's first frame
  }
}

void switchedOff() {
  hasPendingNote = false;
  hasPendingMark = false;
  markAfterFramePending = false;
#if FREEINK_DEVICE_X3
  hasPendingHint = false;
  pendingHint = {};
  hintFailureMemo = {};
#endif
  pendingMark = {};
  pendingMarkPath.clear();
  Storage.remove(DEBT_FILE);
}

void flushNote() {
  if (!hasPendingNote) return;
  SyncDebtList list;
  if (!loadDebts(list)) return;  // card busy: the note stays in memory for the next try
  hasPendingNote = false;
  list.put(std::move(pendingNote));
  if (!saveDebts(list)) LOG_ERR("KAS", "Could not write %s", DEBT_FILE);
}

void pushBeforeSleep(GfxRenderer& renderer) {
  flushNote();
  flushMark();
#if FREEINK_DEVICE_X3
  // A pull can leave a new or failed hint pending while its debt list is empty. Persist it on the
  // sleep path before any eligibility or empty-list return, after the first frame has already been shown.
  flushHint();
#endif
  const Eligibility eligible = gate();
  if (eligible != Eligibility::Ready) {
    if (eligible != Eligibility::Disabled) LOG_INF("KAS", "sleep push skipped: %s", why(eligible));
    return;
  }
  SyncDebtList list;
  if (!loadDebts(list) || list.empty()) return;
  SyncMarkList marks;
  const bool marksReadable = loadMarks(marks);  // unreadable: decide by the plain comparison, write nothing back
  bool marksChanged = false;

  const uint32_t started = millis();
  LOG_INF("KAS", "sleep push begin books=%u", static_cast<unsigned>(list.size()));
  // The join leaves room for one request after it.
  const bool joined = joinSavedNetwork(started + ROUND_BUDGET_MS - REQUEST_MS);
  LOG_INF("KAS", "join ok=%d at=%lu ms heap=%u largest=%u", joined, static_cast<unsigned long>(millis() - started),
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  bool changed = false;
  if (joined) {
    // Newest book first: the one read last is the one most likely to be picked up elsewhere.
    for (size_t i = list.size(); i-- > 0;) {
      const SyncDebt debt = list.at(i);
      const PushOutcome outcome = pushOne(debt, renderer, started, marks, marksChanged);
      if (outcome == PushOutcome::OutOfTime) break;
      if (outcome == PushOutcome::Done) changed = list.remove(debt.path) || changed;
    }
  }
  leaveWifi(true);
#if FREEINK_DEVICE_X3
  flushHint();  // this round may have replaced the hint; no frame is waiting on the card here
#endif
  if (changed && !saveDebts(list)) LOG_ERR("KAS", "Could not write %s", DEBT_FILE);
  if (marksChanged && marksReadable && !saveMarks(marks)) LOG_ERR("KAS", "Could not write %s", MARK_FILE);
  LOG_INF("KAS", "sleep push done total=%lu ms left=%u", static_cast<unsigned long>(millis() - started),
          static_cast<unsigned>(list.size()));
}

uint32_t sleepStamp() {
  if (!KOREADER_STORE.getAutoSync() || !halClock.hasValidTime()) return 0;
  const time_t now = time(nullptr);
  return now >= 1735689600 ? static_cast<uint32_t>(now) : 0;
}

namespace {

PullResult pullNow(GfxRenderer& renderer, const std::string& path) {
#ifdef TENOR_PRESS_PROBE
  restartlog::trace(restartlog::TraceStage::Hash, 0, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
  const std::string hash = documentHash(path);
#ifdef TENOR_PRESS_PROBE
  restartlog::trace(restartlog::TraceStage::Hash, hash.empty() ? 1 : 2, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
  if (hash.empty()) return PullResult::Failed;
  const uint32_t networkAt = millis();
  usedPullNetwork = true;
  KOReaderSyncClient::requestDeadlineMs = networkAt + PULL_NETWORK_MS;
  if (!joinSavedNetwork(networkAt + PULL_JOIN_MS)) {
    leaveWifi(false);
#ifdef TENOR_PRESS_PROBE
    restartlog::mark(restartlog::Event::PullNetwork, millis() - networkAt, static_cast<uint32_t>(Fetch::Failed));
#endif
    return PullResult::Failed;
  }
  KOReaderProgress remote{};
  KOReaderSyncClient::requestTimeoutMs = pullRequestMs(millis() - networkAt);
  const auto reply = KOReaderSyncClient::requestTimeoutMs ? KOReaderSyncClient::getProgress(hash, remote)
                                                        : KOReaderSyncClient::NETWORK_ERROR;
  const uint32_t networkMs = millis() - networkAt;
  const Fetch fetch = pullNetworkExpired(networkMs) || reply != KOReaderSyncClient::OK
                          ? (reply == KOReaderSyncClient::NOT_FOUND && !pullNetworkExpired(networkMs) ? Fetch::NotFound
                                                                                                    : Fetch::Failed)
                          : Fetch::Found;
#ifdef TENOR_PRESS_PROBE
  LOG_INF("KAS", "network ms=%lu result=%d http=%d", static_cast<unsigned long>(networkMs), static_cast<int>(reply),
          KOReaderSyncClient::lastHttpCode);
  restartlog::mark(restartlog::Event::PullNetwork, networkMs, static_cast<uint32_t>(fetch));
#endif
  leaveWifi(false);  // the radio is off before the book opens (BLE needs it on the X3)
  if (fetch != Fetch::Found) return fetch == Fetch::Failed ? PullResult::Failed : PullResult::Kept;

  auto epub = loadEpubForMapping(path);
  if (!epub) {
#ifdef TENOR_PRESS_PROBE
    restartlog::trace(restartlog::TraceStage::PullMap, 1, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
    return PullResult::Failed;
  }
#ifdef TENOR_PRESS_PROBE
  restartlog::trace(restartlog::TraceStage::PullMap, 2, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
  HalFile file;
  uint8_t data[10] = {};
  size_t got = 0;
  if (ProgressFile::openForRead("KAS", epub->getCachePath(), file)) {
    const int n = file.read(data, sizeof(data));
    got = n > 0 ? static_cast<size_t>(n) : 0;
  }
  const auto place = parseProgressFile(data, got);
  if (!place) {
#ifdef TENOR_PRESS_PROBE
    restartlog::trace(restartlog::TraceStage::PullMap, 3, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
    return PullResult::Kept;  // never read in this layout: nothing to compare with
  }

  CrossPointPosition local = {place->spineIndex, place->pageNumber, place->totalPages};
  local.hasResolvedSpineIndex = true;
  local.hasMappedPage = true;
  local.visibleTextOffset = place->visibleTextOffset;
  local.hasVisibleTextOffset = place->hasVisibleTextOffset;
  const int pageCount = std::max(place->totalPages, 1);
  const float intra = pageCount > 1 ? static_cast<float>(place->pageNumber) / static_cast<float>(pageCount - 1) : 0.0f;
  const float localPercentage = epub->calculateProgress(place->spineIndex, intra);

  CrossPointPosition mapped;
  {
    renderer.waitRefreshComplete();  // the "Syncing" popup's refresh reads the framebuffer to its end
    GfxRenderer::FrameBufferLoan loan(renderer);
    mapped = ProgressMapper::fromRemoteProgress(epub, remote, renderer, local.spineIndex, local.totalPages);
  }
  // The loan left the framebuffer white: the glass and the controller no longer match it, so the first
  // paint of the book drives every pixel.
  renderer.redriveNextRefresh();
  const ProgressComparison comparison = compareProgress(local, localPercentage, mapped, remote.percentage);
  // Only another machine's record moves the book: the one this machine left there is older than its own reading.
  SyncMarkList marks;
  loadMarks(marks);
  const bool ours = marks.serverIsOurs(hash, remote.progress, remote.percentage);
  const PullAction action = pullAction(fetch, comparison, ours);
  LOG_INF("KAS", "wake pull decision cmp=%d ours=%d action=%d local=%.4f remote=%.4f", static_cast<int>(comparison),
          ours, static_cast<int>(action), static_cast<double>(localPercentage), static_cast<double>(remote.percentage));
  if (action != PullAction::Apply) {
#ifdef TENOR_PRESS_PROBE
    restartlog::trace(restartlog::TraceStage::PullMap, 4, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
    return PullResult::Kept;
  }

  std::optional<uint32_t> offset;
  if (mapped.hasVisibleTextOffset) offset = mapped.visibleTextOffset;
  if (!EpubReaderUtils::saveProgress(*epub, mapped.spineIndex, mapped.pageNumber, 0, offset)) {
#ifdef TENOR_PRESS_PROBE
    restartlog::trace(restartlog::TraceStage::PullSave, 1, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
    return PullResult::Failed;
  }
#ifdef TENOR_PRESS_PROBE
  restartlog::trace(restartlog::TraceStage::PullSave, 2, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
  pullChange = {path, local.spineIndex, local.pageNumber, mapped.spineIndex, mapped.pageNumber,
                local.visibleTextOffset, local.hasVisibleTextOffset};
  hasPullChange = true;
  // The record taken is this machine's last synced one; written after the book's first frame.
  pendingMark = {hash, recordToken(remote.progress, remote.percentage), placeKey(mapped.spineIndex, mapped.pageNumber)};
  pendingMarkPath.clear();
  hasPendingMark = true;
  markAfterFramePending = true;
  return PullResult::Applied;
}

// The part of a pull both entries share: the gate, the notice, the round and the measuring lines.
PullResult pullGated(GfxRenderer& renderer, const std::string& path, const bool showNotice, const char* what,
                     const bool useStoredPath = false) {
#ifdef TENOR_PRESS_PROBE
  restartlog::traceReset();
#endif
  const Eligibility eligible = gate();
  if (eligible != Eligibility::Ready) {
    if (eligible != Eligibility::Disabled) LOG_INF("KAS", "%s skipped: %s", what, why(eligible));
    return PullResult::Skipped;
  }
  RenderLock lock;
  const uint32_t started = millis();
  if (showNotice) SleepActivity::showSyncing(renderer);
  usedPullNetwork = false;
  const PullResult result = pullNow(renderer, path);
  LOG_INF("KAS", "%s result=%d total=%lu ms", what, static_cast<int>(result),
          static_cast<unsigned long>(millis() - started));
#ifdef TENOR_PRESS_PROBE
  probeHeap("after_pull");  // the book is not open yet
  probeHeapPending = true;
  restartlog::mark(restartlog::Event::PullStep, millis() - started, static_cast<uint32_t>(result));
  restartlog::dump();
#endif
  PullResume savedPull{};
  const bool pathFitsRestart = keepPullPath(savedPull, path, PULL_DONE_MAGIC, useStoredPath);
  const bool restart = pathFitsRestart && preparePageTurnerAfterPull(
      SETTINGS.ble.enabled, usedPullNetwork, result == PullResult::Applied || result == PullResult::Kept);
  if (restart) {
    keptPull = savedPull;
    keptPull.hasChange = hasPullChange;
    if (hasPullChange) {
      keptPull.fromSpine = pullChange.fromSpineIndex;
      keptPull.fromPage = pullChange.fromPage;
      keptPull.toSpine = pullChange.toSpineIndex;
      keptPull.toPage = pullChange.toPage;
      keptPull.fromOffset = pullChange.fromOffset;
      keptPull.hasFromOffset = pullChange.hasFromOffset;
    }
    keptPull.hasMark = hasPendingMark && pendingMark.document.size() == 32;
    if (keptPull.hasMark) {
      keptPull.token = pendingMark.token;
      keptPull.place = pendingMark.place;
    }
    pullRestartPending = true;
  }
  return result;
}

// Has the machine slept long enough for a pull? Same rule for the wake pull and for the pull owed to Home.
bool sleptLong() {
  const time_t now = time(nullptr);
  return sleptLongEnough(APP_STATE.sleepStartedAt, now > 0 ? static_cast<uint32_t>(now) : 0, halClock.hasValidTime(),
                         minSleepSeconds());
}

}  // namespace

PullResult pullOnWake(GfxRenderer& renderer, const std::string& path, const bool showNotice) {
  if (!isEpubPath(path)) return PullResult::Skipped;  // a text or XTC book has no place to map onto
  if (!sleptLong()) {
    if (KOREADER_STORE.getAutoSync()) LOG_INF("KAS", "wake pull skipped: short sleep or no trusted clock");
    return PullResult::Skipped;
  }
  return pullGated(renderer, path, showNotice, "wake pull", path == APP_STATE.openEpubPath);
}

void noteStart(const bool quietRestart, const bool wokeOnHome) {
  const uint32_t keptMagic = takePullMagic(keptPull, quietRestart);
  if (keptMagic) {
    if (!keptPull.useStoredPath) APP_STATE.openEpubPath = keptPull.path;
    setPullDebt(keptMagic == PULL_ARMED_MAGIC ? PullDebt::Armed : PullDebt::None);
    restoredFrame = true;
    if (keptMagic == PULL_DONE_MAGIC) {
      hasPullChange = keptPull.hasChange;
      if (hasPullChange) {
        pullChange = {APP_STATE.openEpubPath, keptPull.fromSpine, keptPull.fromPage, keptPull.toSpine, keptPull.toPage,
                      keptPull.fromOffset, keptPull.hasFromOffset != 0};
      }
      hasPendingMark = keptPull.hasMark;
      markAfterFramePending = hasPendingMark;
      if (hasPendingMark) {
        // Hashing can read the card; wait until the restored book's first frame.
        pendingMark = {std::string(), keptPull.token, keptPull.place};
        pendingMarkPath = APP_STATE.openEpubPath;
      } else {
        pendingMark = {};
        pendingMarkPath.clear();
      }
#ifdef TENOR_PRESS_PROBE
      probeHeapPending = true;
#endif
    }
  }
  pullRestartPending = false;
  const bool wanted = KOREADER_STORE.getAutoSync() && KOREADER_STORE.hasCredentials();
  const bool slept = wokeOnHome && sleptLong();
  const PullDebt before = pullDebt();
  const PullDebt after = debtAfterStart(before, quietRestart, wokeOnHome, slept, wanted);
  setPullDebt(after);
  if (after == PullDebt::Owed && before != PullDebt::Owed) {
    LOG_INF("KAS", "open pull owed: the first book opened from Home pulls");
  } else if (wokeOnHome && wanted && !slept) {
    LOG_INF("KAS", "open pull not owed: short sleep or no trusted clock");
  }
}

bool beforeOpen(GfxRenderer& renderer, const std::string& path) {
  if (pullRestartPending) return true;
  if (restoredFrame) {
    renderer.redriveNextRefresh();
    restoredFrame = false;
  }
  const PullDebt debt = pullDebt();
  if (debt == PullDebt::None) return false;  // the usual open: nothing is read, nothing is asked
  const bool epub = isEpubPath(path);
  const bool wanted = epub && gate(true) == Eligibility::Ready;
#if FREEINK_DEVICE_X3
  // shortcut: Home restart paths fit 175 bytes; use a larger retained store when RTC has room.
  if (wanted && debt == PullDebt::Owed && path.size() >= sizeof(keptPull.path)) {
    LOG_INF("KAS", "open pull skipped: path exceeds restart buffer");
    setPullDebt(PullDebt::None);
    return false;
  }
#ifdef TENOR_PRESS_PROBE
  if (wanted) probeHeap("home_before_pull");
#endif
  // Home keeps the page turner's BLE allocation alive while it joins Wi-Fi. A clean
  // restart gives the pull a fresh radio and heap before the book's first frame.
  const bool restartNeeded = wanted && SETTINGS.ble.enabled;
#else
  const bool restartNeeded = false;
#endif
  const OpenPlan plan = planOpen(debt, epub, wanted, restartNeeded);
  setPullDebt(plan.debtAfter);
  if (epub && !wanted) LOG_INF("KAS", "open pull dropped: switch, account or saved network missing");
  switch (plan.step) {
    case OpenStep::Open:
      return false;
    case OpenStep::PullThenOpen:
      pullGated(renderer, path, debt == PullDebt::Owed, "open pull");
      return pullRestartPending;
    case OpenStep::RestartForPull:
      if (keepPullPath(keptPull, path, PULL_ARMED_MAGIC)) return true;
      setPullDebt(PullDebt::None);
      return false;
  }
  return false;
}

void cancelPullRestart() {
  keptPull = {};
  pullRestartPending = false;
  setPullDebt(PullDebt::Owed);
}

bool pullIsArmed() { return pullDebt() == PullDebt::Armed; }
bool pullRestartReady() { return pullRestartPending; }

#ifdef TENOR_PRESS_PROBE
void probeSetMinSleep(const uint32_t seconds) {
  probeMinSleepSeconds = seconds;
  probeMinSleepMagic = seconds > 0 ? PROBE_MIN_SLEEP_MAGIC : 0;
}
#endif

void afterFirstFrame() {
#ifdef TENOR_PRESS_PROBE
  if (probeHeapPending) {
    probeHeapPending = false;
    probeHeap("first_frame");
  }
#endif
#if FREEINK_DEVICE_X3
  if ((!hasPendingMark || !markAfterFramePending) && !hasPendingHint) return;
#else
  if (!hasPendingMark || !markAfterFramePending) return;
#endif
  RenderLock lock(RenderLock::TryTake{});
  if (!lock.acquired()) return;
  if (hasPendingMark && markAfterFramePending) {
    markAfterFramePending = false;
    flushMark();  // A failed read waits for the existing sleep retry.
  }
#if FREEINK_DEVICE_X3
  flushHint();
#endif
}

const PullChange* progressJustPulled() { return hasPullChange ? &pullChange : nullptr; }

void acknowledgeProgressPulled() { hasPullChange = false; }

}  // namespace koautosync
