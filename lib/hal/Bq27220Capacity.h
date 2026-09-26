#pragma once

#include <cstdint>

// Loads the fitted battery's capacity into a BQ27220 fuel gauge that still holds TI's 3000 mAh
// default, following the Data Memory update of the BQ27220 Technical Reference Manual (SLUUBD4A,
// section 6.1). The gauge keeps that memory in RAM only (TRM 3.1, 8.1.4): a power-on reset, such
// as a battery run flat, brings 3000 mAh back, so the check runs at every start.
//
// Nothing here blocks: tick() makes the bus transactions that are due and returns, and the waits
// the gauge needs are deadlines against the clock passed in. Every path that unlocked the gauge
// seals it again (TRM 2.2.15), and every error or timeout gives up for this start: nothing is
// tried a second time.
class Bq27220Capacity {
 public:
  // The gauge's I2C transactions. The caller keeps them on the one task that owns the gauge.
  struct Bus {
    virtual bool write(uint8_t reg, uint8_t value) = 0;               // one-byte write (TRM 5.3)
    virtual bool read(uint8_t reg, uint8_t* out, uint8_t count) = 0;  // incremental read (TRM 5.1)

   protected:
    ~Bus() = default;
  };

  enum class Result : uint8_t { Pending, NotNeeded, Loaded, Failed };
  // Where the load is, or where it gave up.
  enum class Stage : uint8_t { Check, Access, WaitEnter, Block, WaitExit, Seal, Done };

  static constexpr uint16_t TI_DEFAULT_MAH = 3000;  // Design Capacity default (TRM table 3-2)

  // targetMah 0 leaves the gauge untouched: no bus traffic at all.
  explicit Bq27220Capacity(uint16_t targetMah = 0) : target(targetMah) {}

  void tick(Bus& bus, uint32_t nowMs);
  // Before deep sleep: leaves CONFIG UPDATE and seals at once, without waiting on the gauge.
  void abandon(Bus& bus);

  Result result() const { return outcome; }
  Stage failedAt() const { return failStage; }
  bool running() const { return stage != Stage::Check && stage != Stage::Done; }
  // DesignCapacity() read after sealing (or the one that needed no load), 0 when unread.
  uint16_t designCapacity() const { return dcRead; }

  // TRM 6.1 step 11: the new MACDataSum() from the old one, replacing one two-byte parameter.
  static uint8_t replaceChecksum(uint8_t oldSum, uint8_t oldMsb, uint8_t oldLsb, uint8_t newMsb, uint8_t newLsb);

 private:
  enum class Param : uint8_t { Written, Skipped, Failed };

  bool control(Bus& bus, uint16_t subcommand);
  Param writeParam(Bus& bus, uint16_t address, bool required);
  void wait(uint32_t nowMs, Stage next);
  void giveUp(Bus& bus, uint32_t nowMs);
  void seal(Bus& bus);

  uint16_t target;
  Stage stage = Stage::Check;
  Stage failStage = Stage::Check;
  Result outcome = Result::Pending;
  bool failed = false;
  bool inConfigUpdate = false;  // ENTER_CFG_UPDATE sent, no exit sent yet
  bool wroteData = false;       // a MACData() write was started
  uint16_t dcRead = 0;
  uint32_t sentAt = 0;
  uint32_t nextAt = 0;
};
