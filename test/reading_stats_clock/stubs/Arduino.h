#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

using String = std::string;

unsigned long millis();
void delay(unsigned long ms);
void configTzTime(const char* timezone, const char* first, const char* second);
