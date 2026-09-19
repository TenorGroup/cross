#pragma once
namespace BoardConfig {
enum class DisplayController { Test };
struct ViewableInsets { int top = 0; int right = 0; int bottom = 0; int left = 0; };
struct Profile { ViewableInsets viewableInsets; };
inline Profile ACTIVE;
}
