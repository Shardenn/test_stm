#include "app/application.hpp"

#include "app/board.hpp"
#include "app/serial.hpp"

namespace app {

void Application::onSerialByte(uint8_t byte) { execute(parser_.feed(static_cast<char>(byte))); }

void Application::execute(const Command& command) {
  switch (command.type) {
    case Command::Type::None:
      break;
    case Command::Type::Query: {
      const Measurement m = telemetry_.measure(board::adcCalibration());
      sendLine(Line("VCC ") << m.vccMv);
      sendLine(Line("TEMP ") << m.temperatureC);
      sendLine(Line("LED ") << led_.period(LedState::On) << 'L');
      sendLine(Line("LED ") << led_.period(LedState::Off) << 'D');
      break;
    }
    case Command::Type::SetOnPeriod:
      led_.setPeriod(LedState::On, command.value);
      sendLine(Line("LED ") << command.value << "L OK");
      break;
    case Command::Type::SetOffPeriod:
      led_.setPeriod(LedState::Off, command.value);
      sendLine(Line("LED ") << command.value << "D OK");
      break;
    case Command::Type::Invalid:
      sendLine("ERROR");
      break;
  }
}

}  // namespace app
