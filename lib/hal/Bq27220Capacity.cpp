#include "Bq27220Capacity.h"

// Section numbers are those of the BQ27220 Technical Reference Manual, SLUUBD4A (Nov 2022).
namespace {
constexpr uint8_t CONTROL = 0x00;                 // Control(), low byte then high byte (2.2)
constexpr uint8_t FULL_CHARGE_CAPACITY = 0x12;    // FullChargeCapacity() (2.10)
constexpr uint8_t OPERATION_STATUS = 0x3A;        // OperationStatus() (2.27)
constexpr uint8_t OPERATION_STATUS_HIGH = 0x3B;   // its high byte, read alone in 6.1 steps 4 and 15
constexpr uint8_t CFGUPDATE_BIT = 1 << 2;         // bit 2 of that byte: CONFIG UPDATE mode
constexpr uint8_t DESIGN_CAPACITY = 0x3C;         // DesignCapacity() (2.28)
constexpr uint8_t MAC_CONTROL = 0x3E;             // ManufacturerAccessControl(), 0x3E and 0x3F
constexpr uint8_t MAC_DATA = 0x40;                // MACData() (2.29)
constexpr uint8_t MAC_DATA_SUM = 0x60;            // MACDataSum() (2.30)
constexpr uint8_t MAC_DATA_LEN_MIN = 2 + 2 + 2;   // address, one two-byte parameter, sum and length
constexpr uint8_t MAC_DATA_LEN_MAX = 2 + 32 + 2;  // a full 32-byte block: 0x24 in 6.1 step 13

constexpr uint16_t UNSEAL_KEY_1 = 0x0414;  // 6.1 step 1
constexpr uint16_t UNSEAL_KEY_2 = 0x3672;
constexpr uint16_t FULL_ACCESS_KEY = 0xFFFF;         // 6.1 step 2, sent twice
// Between two key words. Sent back to back the X3's gauge ignored them; 1.5 s apart each took
// (measured 26/09/2026).
constexpr uint32_t KEY_GAP_MS = 1500;
constexpr uint32_t SELECT_SETTLE_MS = 10;
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

// A Learned Full Charge Capacity more than a quarter above the cell was learned against TI's 3000
// mAh default: a learning cycle moves it at most 256 mAh down (1.1.3), so it stays far above a
// small cell, and FullChargeCapacity() copies it at every reinit (1.1.10). One learned on the cell
// itself is kept.
bool learnedTooHigh(const uint16_t learned, const uint16_t target) { return learned > target + target / 4; }
}  // namespace

uint8_t Bq27220Capacity::replaceChecksum(const uint8_t oldSum, const uint8_t oldMsb, const uint8_t oldLsb,
                                         const uint8_t newMsb, const uint8_t newLsb) {
  const uint8_t rest = static_cast<uint8_t>(255 - oldSum - oldMsb - oldLsb);
  return static_cast<uint8_t>(255 - static_cast<uint8_t>(rest + newMsb + newLsb));
}

bool Bq27220Capacity::control(Bus& bus, const uint16_t subcommand) {
  // Both bytes in one write. The X3's gauge ignores the single-byte method of 6.1 step 1: sent a byte
  // at a time, SEALED, the unseal keys and the full access keys left OperationStatus() unchanged;
  // sent as one word each took (measured on an X3, 26/09/2026).
  const uint8_t bytes[] = {static_cast<uint8_t>(subcommand & 0xFF), static_cast<uint8_t>(subcommand >> 8)};
  return bus.write(CONTROL, bytes, 2);
}

Bq27220Capacity::Param Bq27220Capacity::writeParam(Bus& bus, const uint16_t address, const bool required) {
  const uint8_t addressLow = address & 0xFF;
  const uint8_t addressHigh = address >> 8;
  // Steps 5 and 6: the parameter's address into ManufacturerAccessControl().
  const uint8_t addressBytes[] = {addressLow, addressHigh};
  detail = 1;
  if (!bus.write(MAC_CONTROL, addressBytes, 2)) return Param::Failed;
  // The gauge needs a moment to bring the block into MACData(): read at once, the X3's gauge still
  // returned the previous block (measured 26/09/2026).
  bus.pause(SELECT_SETTLE_MS);
  // 2.29: reading from ManufacturerAccessControl() first confirms the block MACData() now holds.
  uint8_t selected[2];
  detail = 2;
  if (!bus.read(MAC_CONTROL, selected, 2) || selected[0] != addressLow || selected[1] != addressHigh) {
    detailValue = static_cast<uint16_t>(selected[0] | (selected[1] << 8));
    return Param::Failed;
  }
  // Steps 7 to 9, the old value first: on the X3's gauge, reading MACDataSum() and MACDataLen()
  // moves MACData() on to the next 32 bytes (measured 26/09/2026), so the block is selected again
  // before the new value goes in.
  uint8_t oldValue[2] = {}, sumAndLengthOld[2] = {};
  detail = 3;
  if (!bus.read(MAC_DATA, oldValue, 2) || !bus.read(MAC_DATA_SUM, sumAndLengthOld, 2)) return Param::Failed;
  const uint8_t oldMsb = oldValue[0], oldLsb = oldValue[1];
  const uint8_t oldSum = sumAndLengthOld[0], length = sumAndLengthOld[1];
  detail = 4;
  detailValue = static_cast<uint16_t>((length << 8) | oldSum);
  if (length < MAC_DATA_LEN_MIN || length > MAC_DATA_LEN_MAX) return Param::Failed;
  detail = 5;
  detailValue = static_cast<uint16_t>((oldMsb << 8) | oldLsb);
  const uint16_t old = static_cast<uint16_t>((oldMsb << 8) | oldLsb);
  if (address == DM_FULL_CHARGE_CAPACITY ? !learnedTooHigh(old, target) : old != TI_DEFAULT_MAH) {
    return required && old != target ? Param::Failed : Param::Skipped;
  }
  const uint8_t newMsb = target >> 8;
  const uint8_t newLsb = target & 0xFF;
  // Steps 10 to 13: new value, new checksum, then the length, which moves the block into RAM.
  wroteData = true;
  if (!bus.write(MAC_CONTROL, addressBytes, 2)) return Param::Failed;
  bus.pause(SELECT_SETTLE_MS);
  // Checksum and length go together as one word (2.31), the value in one write like the rest.
  const uint8_t value[] = {newMsb, newLsb};
  const uint8_t sumAndLength[] = {replaceChecksum(oldSum, oldMsb, oldLsb, newMsb, newLsb), length};
  detail = 6;
  if (!bus.write(MAC_DATA, value, 2) || !bus.write(MAC_DATA_SUM, sumAndLength, 2)) return Param::Failed;
  detail = 0;
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
          ((dcRead == TI_DEFAULT_MAH || dcRead == target) && !readWord(bus, OPERATION_STATUS, status))) {
        // Nothing was sent to the gauge: nothing to undo.
        failStage = stage;
        outcome = Result::Failed;
        stage = Stage::Done;
        return;
      }
      checkStatus = status;
      const bool inConfig = (status >> 8) & CFGUPDATE_BIT;
      if (dcRead == target && (inConfig || security(status) != SEC_SEALED)) {
        // The target is in, but a load cut short (sleep, or a refused exit or SEALED) left the gauge
        // in CONFIG UPDATE, where it stops gauging, or unsealed. DesignCapacity() reading the target
        // means a block was written, so the exit reinitializes, as at the end of a load (step 14).
        resealing = true;
        if (!inConfig) return seal(bus);
        stage = Stage::WaitExit;
        if (!control(bus, EXIT_CFG_UPDATE_REINIT)) {
          failed = true;
          failStage = stage;
          return seal(bus);
        }
        return wait(nowMs, Stage::WaitExit);
      }
      uint16_t fcc = 0;
      if (dcRead == target && !readWord(bus, FULL_CHARGE_CAPACITY, fcc)) {
        failStage = stage;
        outcome = Result::Failed;
        stage = Stage::Done;
        return;
      }
      if (dcRead == target ? !learnedTooHigh(fcc, target) : dcRead != TI_DEFAULT_MAH) {
        // Already loaded since the gauge last lost power, or a battery someone else configured.
        outcome = Result::NotNeeded;
        stage = Stage::Done;
        return;
      }
      // Steps 1 and 2: unseal if sealed, then FULL ACCESS, which the gauge does not boot in. One key
      // word a tick, KEY_GAP_MS apart: the X3's gauge ignored keys sent back to back.
      keyCount = 0;
      if (security(status) == SEC_SEALED) {
        keys[keyCount++] = UNSEAL_KEY_1;
        keys[keyCount++] = UNSEAL_KEY_2;
      }
      if (security(status) != SEC_FULL_ACCESS) {
        keys[keyCount++] = FULL_ACCESS_KEY;
        keys[keyCount++] = FULL_ACCESS_KEY;
      }
      keysSent = 0;
      stage = Stage::Access;
      nextAt = nowMs;
      return;
    }
    case Stage::Access: {
      if (!due(nowMs, nextAt)) return;
      if (keysSent < keyCount) {
        if (!control(bus, keys[keysSent++])) return giveUp(bus, nowMs);
        nextAt = nowMs + KEY_GAP_MS;
        return;
      }
      uint16_t status = 0;
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
      // fails, it still reads 3000 and the next start loads both again. A learned value up to a
      // quarter above the target is kept; Design Capacity is skipped when it already holds it.
      if (writeParam(bus, DM_FULL_CHARGE_CAPACITY, false) == Param::Failed ||
          writeParam(bus, DM_DESIGN_CAPACITY, true) == Param::Failed) {
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
  outcome = failed || dcRead != target ? Result::Failed : resealing ? Result::Resealed : Result::Loaded;
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
