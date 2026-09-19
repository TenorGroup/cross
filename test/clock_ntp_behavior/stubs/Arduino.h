#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
using String = std::string;
unsigned long millis();
void delay(unsigned long);
void configTzTime(const char*, const char*, const char*);
