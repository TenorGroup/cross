#pragma once
struct UITheme {
  struct Metrics {int topPadding=0,headerHeight=0,buttonHintsHeight=0;} metrics;
  const Metrics& getMetrics() const {return metrics;}
  static UITheme& getInstance(){static UITheme t;return t;}
};
