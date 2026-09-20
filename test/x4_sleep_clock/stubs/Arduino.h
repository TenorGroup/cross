#pragma once
#include <cstdint>
#include <initializer_list>
constexpr int INPUT = 0, OUTPUT = 1, INPUT_PULLUP = 2, INPUT_PULLDOWN = 3;
constexpr int LOW = 0, HIGH = 1;
void pinMode(int, int);
void digitalWrite(int, int);
int digitalRead(int);
unsigned long millis();
void delay(unsigned long);
int getCpuFrequencyMhz();
bool setCpuFrequencyMhz(int);
struct SerialStub { void end(); };
extern SerialStub logSerial;
