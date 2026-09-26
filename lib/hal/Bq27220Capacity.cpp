#include "Bq27220Capacity.h"

// Section numbers are those of the BQ27220 Technical Reference Manual, SLUUBD4A (Nov 2022).
namespace {
constexpr uint8_t CONTROL = 0x00;                 // Control(), low byte then high byte (2.2)
constexpr uint8_t OPERATION_STATUS = 0x3A;        // OperationStatus() (2.27)
constexpr uint8_t OPERATION_STATUS_HIGH = 0x3B;   // its high byte, read alone in 6.1 steps 4 and 15
constexpr uint8_t CFGUPDATE_BIT = 1 << 2;         // bit 2 of that byte: CONFIG UPDATE mode
constexpr uint8_t DESIGN_CAPACITY = 0x3C;         // DesignCapacity() (2.28)
constexpr uint8_t MAC_CONTROL = 0x3E;             // ManufacturerAccessControl(), 0x3E and 0x3F
constexpr uint8_t MAC_DATA = 0x40;                // MACData() (2.29)
constexpr uint8_t MAC_DATA_SUM = 0x60;            // MACDataSum() (2.30)
constexpr uint8_t MAC_DATA_LEN = 0x61;            // MACDataLen() (2.31)
constexpr uint8_t MAC_DATA_LEN_MIN = 2 + 2 + 2;   // address, one two-byte parameter, sum and length
constexpr uint8_t MAC_DATA_LEN_MAX = 2 + 32 + 2;  // a full 32-byte block: 0x24 in 6.1 step 13

constexpr uint16_t UNSEAL_KEY_1 = 0x0414;  // 6.1 step 1
constexpr uint16_t UNSEAL_KEY_2 = 0x3672;
constexpr uint16_t FULL_ACCESS_KEY = 0xFFFF;         // 6.1 step 2, sent twice
constexpr uint16_t SEALED = 0x0030;                  // 2.2.15
constexpr uint16_t ENTER_CFG_UPDATE = 0x0090;        // 2.2.21
constexpr uint16_t EXIT_CFG_UPDATE_REINIT = 0x0091;  // 2.2.22
constexpr uint16_t EXIT_CFG_UPDATE = 0x0092;         // 2.2.23

// CEDV Profile 1 in Data Memory (table 3-2), two bytes, most significant first (6.1 step 9).
constexpr uint16_t DM_FULL_CHARGE_CAPACITY = 0x929D;  // Learned Full Charge Capacity (4.9.36)
constexpr uint16_t DM_DESIGN_CAPACITY = 0x929F;       // 6.1 step 5

// SEC[1:0], OperationStatus() bits 2:1 (2.27).
constexpr uint8_t SEC_SEALED = 0b11;
constexpr uint8_t SEC_FULL_ACCESS = 0b01;

// CONFIG UPDATE is read no sooner than 2 s after the command (2.2.21 note), then twice a second
// at most, since the gauge wants no more than two standard commands a second (5.3). 6.1 expects
// up to 1 s for either change; past 5 s the load gives up.
constexpr uint32_t FLAG_FIRST_READ_MS = 2000;
constexpr uint32_t FLAG_POLL_MS = 500;
constexpr uint32_t FLAG_GIVE_UP_MS = 5000;

bool readWord(Bq27220Capacity::Bus& bus, const uint8_t reg, uint16_t& value) {
  uint8_t bytes[2];
  if (!bus.read(reg, bytes, 2)) return false;
  value = static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
  return true;
}

uint8_t security(const uint16_t operationStatus) { return (operationStatus >> 1) & 0b11; }

bool due(const uint32_t nowMs, const uint32_t atMs) { return static_cast<int32_t>(nowMs - atMs) >= 0; }
}  // namespace

uint8_t Bq27220Capacity::replaceChecksum(const uint8_t oldSum, const uint8_t oldMsb, const uint8_t oldLsb,
                                         const uint8_t newMsb, const uint8_t newLsb) {
  const uint8_t rest = static_cast<uint8_t>(255 - oldSum - oldMsb - oldLsb);
  return static_cast<uint8_t>(255 - static_cast<uint8_t>(rest + newMsb + newLsb));
}

bool Bq27220Capacity::control(Bus& bus, const uint16_t subcommand) {
  // The single-byte method of 6.1 step 1: above 100 kHz the gauge takes one-byte writes only (5.3).
  return bus.write(CONTROL, subcommand & 0xFF) && bus.write(CONTROL + 1, subcommand >> 8);
}

Bq27220Capacity::Param Bq27220Capacity::writeParam(Bus& bus, const uint16_t address, const bool required) {
  const uint8_t addressLow = address & 0xFF;
  const uint8_t addressHigh = address >> 8;
  // Steps 5 and 6: the parameter's address into ManufacturerAccessControl().
  if (!bus.write(MAC_CONTROL, addressLow) || !bus.write(MAC_CONTROL + 1, addressHigh)) return Param::Failed;
  // 2.29: reading from ManufacturerAccessControl() first confirms the block MACData() now holds.
  uint8_t selected[2];
  if (!bus.read(MAC_CONTROL, selected, 2) || selected[0] != addressLow || selected[1] != addressHigh) {
    return Param::Failed;
  }
  // Steps 7 to 9: old checksum, block length, old value.
  uint8_t oldSum = 0, length = 0, oldMsb = 0, oldLsb = 0;
  if (!bus.read(MAC_DATA_SUM, &oldSum, 1) || !bus.read(MAC_DATA_LEN, &length, 1) || !bus.read(MAC_DATA, &oldMsb, 1) ||
      !bus.read(MAC_DATA + 1, &oldLsb, 1)) {
    return Param::Failed;
  }
  if (length < MAC_DATA_LEN_MIN || length > MAC_DATA_LEN_MAX) return Param::Failed;
  if (((oldMsb << 8) | oldLsb) != TI_DEFAULT_MAH) return required ? Param::Failed : Param::Skipped;
  const uint8_t newMsb = target >> 8;
  const uint8_t newLsb = target & 0xFF;
  // Steps 10 to 13: new value, new checksum, then the length, which moves the block into RAM.
  wroteData = true;
  if (!bus.write(MAC_DATA, newMsb) || !bus.write(MAC_DATA + 1, newLsb) ||
      !bus.write(MAC_DATA_SUM, replaceChecksum(oldSum, oldMsb, oldLsb, newMsb, newLsb)) ||
      !bus.write(MAC_DATA_LEN, length)) {
    return Param::Failed;
  }
  return Param::Written;
}

void Bq27220Capacity::wait(const uint32_t nowMs, const Stage next) {
  stage = next;
  sentAt = nowMs;
  nextAt = nowMs + FLAG_FIRST_READ_MS;
}

void Bq27220Capacity::tick(Bus& bus, const uint32_t nowMs) {
  switch (stage) {
    case Stage::Check: {
      if (target == 0) {
        outcome = Result::NotNeeded;
        stage = Stage::Done;
        return;
      }
      uint16_t status = 0;
      if (!readWord(bus, DESIGN_CAPACITY, dcRead) ||
          (dcRead == TI_DEFAULT_MAH && !readWord(bus, OPERATION_STATUS, status))) {
        // Nothing was sent to the gauge: nothing to undo.
        failStage = stage;
        outcome = Result::Failed;
        stage = Stage::Done;
        return;
      }
      if (dcRead != TI_DEFAULT_MAH) {
        // Already loaded since the gauge last lost power, or a battery someone else configured.
        outcome = Result::NotNeeded;
        stage = Stage::Done;
        return;
      }
      // Steps 1 and 2: unseal if sealed, then FULL ACCESS, which the gauge does not boot in.
      stage = Stage::Access;
      if (security(status) == SEC_SEALED && !(control(bus, UNSEAL_KEY_1) && control(bus, UNSEAL_KEY_2))) {
        return giveUp(bus, nowMs);
      }
      if (security(status) != SEC_FULL_ACCESS && !(control(bus, FULL_ACCESS_KEY) && control(bus, FULL_ACCESS_KEY))) {
        return giveUp(bus, nowMs);
      }
      if (!readWord(bus, OPERATION_STATUS, status) || security(status) != SEC_FULL_ACCESS) return giveUp(bus, nowMs);
      // Step 3.
      inConfigUpdate = true;
      if (!control(bus, ENTER_CFG_UPDATE)) return giveUp(bus, nowMs);
      return wait(nowMs, Stage::WaitEnter);
    }
    case Stage::WaitEnter: {
      if (!due(nowMs, nextAt)) return;
      // Step 4.
      uint8_t high = 0;
      if (!bus.read(OPERATION_STATUS_HIGH, &high, 1)) return giveUp(bus, nowMs);
      if (!(high & CFGUPDATE_BIT)) {
        if (due(nowMs, sentAt + FLAG_GIVE_UP_MS)) return giveUp(bus, nowMs);
        nextAt = nowMs + FLAG_POLL_MS;
        return;
      }
      stage = Stage::Block;
      // After the reinit FullChargeCapacity() is a copy of Learned Full Charge Capacity, which
      // should start at the Design Capacity (1.1.10). Written first: if Design Capacity then
      // fails, it still reads 3000 and the next start loads both again. A learned value other
      // than the default is kept.
      if (writeParam(bus, DM_FULL_CHARGE_CAPACITY, false) == Param::Failed ||
          writeParam(bus, DM_DESIGN_CAPACITY, true) != Param::Written) {
        return giveUp(bus, nowMs);
      }
      // Step 14: the reinit recomputes FullChargeCapacity() and RemainingCapacity() (1.1.10).
      inConfigUpdate = false;
      if (!control(bus, EXIT_CFG_UPDATE_REINIT)) {
        failed = true;
        failStage = stage;
        return seal(bus);
      }
      return wait(nowMs, Stage::WaitExit);
    }
    case Stage::WaitExit: {
      if (!due(nowMs, nextAt)) return;
      // Step 15.
      uint8_t high = 0;
      const bool read = bus.read(OPERATION_STATUS_HIGH, &high, 1);
      if (read && (high & CFGUPDATE_BIT) && !due(nowMs, sentAt + FLAG_GIVE_UP_MS)) {
        nextAt = nowMs + FLAG_POLL_MS;
        return;
      }
      if (!read || (high & CFGUPDATE_BIT)) {
        if (!failed) failStage = stage;
        failed = true;
      }
      return seal(bus);
    }
    case Stage::Access:
    case Stage::Block:
    case Stage::Seal:
    case Stage::Done:
      return;
  }
}

void Bq27220Capacity::giveUp(Bus& bus, const uint32_t nowMs) {
  failed = true;
  failStage = stage;
  if (inConfigUpdate) {
    inConfigUpdate = false;
    // Out of CONFIG UPDATE: with a reinit when a block write had started, so the gauge restarts from
    // whatever its RAM now holds; without one when nothing was written.
    if (control(bus, wroteData ? EXIT_CFG_UPDATE_REINIT : EXIT_CFG_UPDATE)) return wait(nowMs, Stage::WaitExit);
  }
  seal(bus);
}

void Bq27220Capacity::seal(Bus& bus) {
  // Step 16, whatever the gauge's access mode was: end equipment keeps it SEALED (2.2.15).
  stage = Stage::Seal;
  if (!control(bus, SEALED)) {
    if (!failed) failStage = stage;
    failed = true;
  }
  if (!readWord(bus, DESIGN_CAPACITY, dcRead)) dcRead = 0;
  outcome = !failed && dcRead == target ? Result::Loaded : Result::Failed;
  if (outcome == Result::Failed && !failed) failStage = stage;
  stage = Stage::Done;
}

void Bq27220Capacity::abandon(Bus& bus) {
  if (!running()) return;
  failed = true;
  failStage = stage;
  if (inConfigUpdate) {
    inConfigUpdate = false;
    control(bus, wroteData ? EXIT_CFG_UPDATE_REINIT : EXIT_CFG_UPDATE);
  }
  seal(bus);
}
