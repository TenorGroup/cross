#include "HalGaugeCapacity.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <Logging.h>
#include <Wire.h>

#include "HalPowerManager.h"

HalGaugeCapacity halGaugeCapacity;  // Singleton instance

namespace {
// The gauge on Wire, one transaction a call, each followed by the bus-free time the BQ27220 asks
// for between packets (TRM SLUUBD4A 5.3: at least 66 us).
class WireGauge final : public Bq27220Capacity::Bus {
  const uint8_t addr;

 public:
  explicit WireGauge(const uint8_t address) : addr(address) {}

  bool write(const uint8_t reg, const uint8_t* data, const uint8_t count) override {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(data, count);
    const bool ok = Wire.endTransmission() == 0;
    delayMicroseconds(66);
    return ok;
  }

  void pause(const uint32_t ms) override { delay(ms); }
  bool read(const uint8_t reg, uint8_t* out, const uint8_t count) override {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    bool ok = Wire.endTransmission(false) == 0 && Wire.requestFrom(addr, count, uint8_t{1}) == count;
    for (uint8_t i = 0; ok && i < count; ++i) out[i] = static_cast<uint8_t>(Wire.read());
    delayMicroseconds(66);
    return ok;
  }
};

const char* stageName(const Bq27220Capacity::Stage stage) {
  switch (stage) {
    case Bq27220Capacity::Stage::Check:
      return "failed:check";
    case Bq27220Capacity::Stage::Access:
      return "failed:access";
    case Bq27220Capacity::Stage::WaitEnter:
      return "failed:enter";
    case Bq27220Capacity::Stage::Block:
      return "failed:block";
    case Bq27220Capacity::Stage::WaitExit:
      return "failed:exit";
    case Bq27220Capacity::Stage::Seal:
      return "failed:seal";
    case Bq27220Capacity::Stage::Done:
      break;
  }
  return "failed";
}
}  // namespace

void HalGaugeCapacity::tick() {
  if (!started) {
    const auto& gauge = BoardConfig::ACTIVE.batteryGauge;
    const bool bq27220 = gauge.gaugeAddr != 0 && gauge.gaugeType == BoardConfig::GaugeType::Bq27220;
    load = Bq27220Capacity(bq27220 ? gauge.designCapacityMah : 0);
    started = true;
  }
  if (load.result() != Bq27220Capacity::Result::Pending || !powerManager.mayReadGauge()) return;
  WireGauge bus(BoardConfig::ACTIVE.batteryGauge.gaugeAddr);
  load.tick(bus, millis());
  if (load.result() != Bq27220Capacity::Result::Pending && BoardConfig::ACTIVE.batteryGauge.designCapacityMah != 0) {
    LOG_INF("BAT", "Gauge capacity %s dc=%u detail=%u value=0x%04x", status(),
            static_cast<unsigned>(load.designCapacity()), load.detail, load.detailValue);
  }
}

void HalGaugeCapacity::abandon() {
  if (!load.running() || !powerManager.mayReadGauge()) return;
  WireGauge bus(BoardConfig::ACTIVE.batteryGauge.gaugeAddr);
  load.abandon(bus);
  LOG_INF("BAT", "Gauge capacity %s dc=%u (sleep)", status(), static_cast<unsigned>(load.designCapacity()));
}

const char* HalGaugeCapacity::status() const {
  if (!started) return "none";
  switch (load.result()) {
    case Bq27220Capacity::Result::Pending:
      return "pending";
    case Bq27220Capacity::Result::NotNeeded:
      return "not-needed";
    case Bq27220Capacity::Result::Loaded:
      return "loaded";
    case Bq27220Capacity::Result::Failed:
      break;
  }
  return stageName(load.failedAt());
}
