#include <HalTiltSensor.h>

HalTiltSensor halTiltSensor;

void HalTiltSensor::update(uint8_t, uint8_t, bool) {}

bool HalTiltSensor::wasTiltedForward() { return false; }

bool HalTiltSensor::wasTiltedBack() { return false; }

void HalTiltSensor::configureVerticalGesture(uint8_t, bool) {}

bool HalTiltSensor::wasTiltedUp() { return false; }

bool HalTiltSensor::wasTiltedDown() { return false; }
