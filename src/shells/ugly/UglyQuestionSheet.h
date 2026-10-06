#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class GfxRenderer;
class MappedInputManager;

namespace ugly {

// View context, subject and callbacks are borrowed until invalidate(). A Row's
// question and formatted option labels are used only within their current call.
class QuestionSheet {
 public:
  enum class Kind : uint8_t { Toggle, Choice, Ruler, Paper, Action, ReadOnly };
  struct Row {
    uint32_t id = 0;
    const char* question = "";
    Kind kind = Kind::ReadOnly;
    int selected = 0;
    int count = 0;
  };
  struct View {
    void* context = nullptr;
    int count = 0;
    const char* subject = "";
    uint32_t date = 0;  // yyyymmdd, prepared outside paint; zero leaves ID blank
    int code = 0;
    Row (*row)(void*, int) = nullptr;
    void (*label)(void*, int row, int option, char* out, size_t size) = nullptr;
  };
  enum class Key : uint8_t { PreviousQuestion, NextQuestion, PreviousOption, NextOption,
                             PreviousSheet, NextSheet, Confirm, Back, Home };
  enum class IntentKind : uint8_t { None, Preview, Commit, Activate, Back };
  struct Intent {
    IntentKind kind = IntentKind::None;
    uint32_t id = 0;
    int row = -1;
    int candidate = -1;
    bool repaint = false;
  };

  // Call under the host's render lock when catalog, language or geometry changes.
  // No borrowed Row/label pointer survives this call or an input/paint call.
  void bind(const GfxRenderer& renderer, View view, bool touch);
  void invalidate();
  Intent input(Key key);
  Intent tap(int x, int y);
  int questionAt(int x, int y) const;  // visible row, -1 on inert areas or paper
  Intent turnSheet(int direction);
  // Only after the host applied the returned Commit successfully. The view then
  // supplies the new committed selection. Failure leaves its previous mark intact.
  void didCommit(int row, int previousIndex);
  void setQuip(int row, std::string line);  // prepare/inflate outside paint
  void paint(const GfxRenderer& renderer, const MappedInputManager& input) const;

  int question() const { return current_; }
  int candidate() const { return candidate_; }
  int sheet() const { return page_; }
  int sheetCount() const { return pages_; }
  bool paperOpen() const { return paper_; }
  int paperFirst() const { return paperFirst_; }

 private:
  struct Block {
    uint32_t id;
    int16_t top, height, questionHeight, page, columns, previous;
    Kind kind;
  };
  View view_{};
  std::vector<Block> blocks_;  // resized only by bind, never by paint/input
  std::string quip_;
  int current_ = 0, candidate_ = 0, page_ = 0, pages_ = 0, quipRow_ = -1;
  int width_ = 528, height_ = 792, left_ = 32, right_ = 400, margin_ = 406;
  int questionStep_ = 38, answerStep_ = 44, gap_ = 10;
  int firstTop_ = 112, laterTop_ = 52, bottom_ = 716;
  bool touch_ = false, paper_ = false, answering_ = false;
  mutable bool ready_ = false;  // touch geometry becomes visible after paint
  int paperTop_ = 0, paperBottom_ = 0, paperFirst_ = 0, paperLast_ = -1, paperAnchor_ = 0, paperAnchorTop_ = 0;
  Row rowAt(int index) const;
  Kind kindAt(int index) const;
  void labelAt(int row, int option, char* out, size_t size) const;
  Intent intent(IntentKind kind, bool repaint) const;
  Intent focus(int row);
  void openPaper();
  int answerTop(int row) const;
  int optionAt(int row, int x, int y) const;
};

}  // namespace ugly
