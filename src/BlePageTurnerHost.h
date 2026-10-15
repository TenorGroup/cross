#pragma once

#include <cstdint>

class GfxRenderer;
namespace bleturner {
struct Config;
enum class Action : uint8_t;
}

// This firmware's side of the page turner (lib/BlePageTurner): the Host the module asks for
// heap, cache release, page turns, the book's yield, file transfer, CPU speed and log lines.
// main.cpp adds the two things only it can do: run the catalog action a remote shortcut
// (ReaderMenu, SaveQuote) names, and restart into the open book.
void beginPageTurner(GfxRenderer& renderer, bleturner::Config& config, void (*runShortcut)(bleturner::Action shortcut),
                     void (*restartIntoBook)());

// Finish a Wi-Fi pull before making the reader and decide whether the X3 needs the shared heap restart.
bool preparePageTurnerAfterPull(bool enabled, bool usedNetwork, bool succeeded);
void markPageTurnerRestartSpent();
