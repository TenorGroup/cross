#pragma once
// Never reached: the keyboard fixture is tenor/cross (shells/Shell.h).
namespace ugly {
enum class Size { S22, S30 };
enum class Circle { Word };
enum class Mark { Left, Right };
struct Box {
  int x0, y0, x1, y1;
};
template <class... A> int width(A&&...) { return 0; }
template <class... A> int text(A&&...) { return 0; }
template <class... A> void underline(A&&...) {}
template <class... A> void mark(A&&...) {}
template <class... A> void circle(A&&...) {}
inline int ascent(Size) { return 0; }
}  // namespace ugly
