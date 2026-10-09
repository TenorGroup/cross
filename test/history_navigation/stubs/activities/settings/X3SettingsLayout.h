#pragma once
namespace tenorchrome {
inline bool x3ScreenLayoutEnabled() { return false; }
}
struct StatsIconBoundary {};
inline constexpr StatsIconBoundary icon_tenor_home_stats_40{};
namespace x3settings {
template <typename... Arguments> bool beginScreen(Arguments&&...) { return false; }
template <typename... Arguments> void drawPath(Arguments&&...) {}
}
