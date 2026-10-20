// STM32 implementation of app::board (see board.hpp).
#pragma once

#include "main.h"

namespace app {
namespace board {

// Handles used by the board functions; call before anything else.
void attach(TIM_HandleTypeDef* ledTimer, UART_HandleTypeDef* uart);

}  // namespace board
}  // namespace app
