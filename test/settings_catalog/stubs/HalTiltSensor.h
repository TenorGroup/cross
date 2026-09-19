#pragma once
class HalTiltSensor { public: bool available=false; bool isAvailable() const { return available; } };
extern HalTiltSensor halTiltSensor;
