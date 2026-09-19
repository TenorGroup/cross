#pragma once
namespace BoardConfig {
enum class InputStyle { XteinkAdcLadder, DigitalButtons, DigitalConfirmBackHold, DigitalConfirmPowerHold, DigitalTwoButton };
struct Board { InputStyle inputStyle = InputStyle::XteinkAdcLadder; };
extern Board ACTIVE;
}
