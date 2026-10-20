# Host-side tests (no microcontroller needed)

The firmware is tested on a PC by compiling it **unchanged** (`Core/Src/main.c`, the
real `Core/Inc/main.h` and BSP header, the C++ application in `Core/Src/app/`) against
a fake HAL and linking it with an emulator of the STM32F411E-DISCO.

```
cmake -S Tests -B Tests/build
cmake --build Tests/build --config Release
ctest --test-dir Tests/build -C Release --output-on-failure
```

Any C++14 compiler works (developed with MSVC 2022, plain C/C++ + `std::thread`).

## Layers

| ctest name | What runs | Purpose |
|---|---|---|
| `emulator_reference` | system tests on the **original C firmware** (`reference/main.c`, a frozen copy of the hardware-proven `main.c`) | validates the emulator and the test expectations against code known to work on the board |
| `emulator_cpp` | the same system tests on the **current firmware** (C `main.c` + C++ application) | the rewrite meets the requirements |
| `differential_fuzz` | 150 random 20–60 s scenarios run on both firmwares; the traces (every UART byte and LED edge, ns timestamps) must be identical | the rewrite behaves exactly like the original, including corner cases nobody wrote a test for |
| `unit` | the C++ classes against a fake `app::board`, plus a check that `main.c` differs from the original only inside `USER CODE` sections | fast, precise checks of the building blocks |

Each emulator test runs in its own process (the firmware has global state and never
returns from `main()`); the executable re-invokes itself with `--run <test>`. Other
modes: `--list`, `--trace <seed>`, `--diff <other.exe> <count> [first-seed]`.

## The emulator (`emulator/`)

`emulator/include/stm32f4xx_hal.h` declares the HAL subset the firmware uses, with the
real numeric constants. `emulator.cpp` implements those functions and models:

* **RCC**: clocks derived from the firmware's own `RCC_OscInit`/`ClkInit` structures
  (8 MHz HSE → 96 MHz SYSCLK, APB1 24 MHz, timers 48 MHz).
* **SysTick** 1 ms → `HAL_GetTick()`.
* **TIM2/TIM3**: prescaler, ARR, CNT, update interrupt, TRGO. Firmware writes to
  `ARR`/`CNT` act on the model.
* **ADC1 + DMA**: TIM2 TRGO starts a scan (rank 1 temperature sensor, rank 2 VREFINT).
  Results are written circularly to the DMA buffer after the real conversion time
  ((480+12) cycles @ 24 MHz). Analog inputs and calibration words are set by each test.
* **EXTI0 / PA0** user button on both edges, with arbitrary bounce patterns.
* **USART2**: frame time from BRR (24 MHz/208 → 95.3 µs per 8E1 frame). A blocking
  `HAL_UART_Transmit` consumes CPU time. Reception works like
  `HAL_UART_IRQHandler`, including overrun (ORE), which ends the reception.
* **NVIC**: every IRQ in this project has priority 0, so ISRs never nest. Events that
  happen while an ISR is running (e.g. blocked in a UART transmit) only become pending.
  A second event on an already-pending source is lost, which is how SysTick ticks get
  lost on the real chip.

`HAL_*_MspInit` (stm32f4xx_hal_msp.c) is not compiled. Its effects (NVIC enables,
circular half-word DMA) are modelled directly.

## System test cases (`cases/`)

| Requirement | Test |
|---|---|
| init | `boot_configures_clocks_timers_and_leds` |
| 1.A–E | `led_cycles_4s_on_6s_off` (64 s, every phase 4000/6000 ms ± 5) |
| 3.A | `led_changes_and_durations_are_reported`, `serial_port_is_115200_8E1`, `all_output_lines_are_crlf_terminated`, `press_duration_is_reported_on_release` |
| 2.A | `first_press_toggles_led_and_reports_button`, `release_chatter_is_debounced`, `press_chatter_is_not_filtered_characterization` |
| 2.B | `response_to_actuation_within_50ms` (measured ≈ 1.8 ms) |
| 2.C | `actuation_pattern_is_press_press_release_release` (3 full cycles) |
| 2.D | `manual_toggle_restarts_the_on_off_timing` |
| 4.A | `adc_is_sampled_at_75hz` (74.6 Hz) |
| 4.B | `values_are_the_average_of_the_last_10_samples` (compares with the exact samples the ISR saw) |
| 4.C | `temperature_and_vcc_integer_conversion`, `temperature_below_30C_is_converted_correctly`¹, `query_before_first_sample_does_not_crash`¹ |
| 4.D | `long_press_reports_temp_and_vcc_once`, `short_press_does_not_report_adc`, `long_press_threshold_is_more_than_500ms` (sweep 494–510 ms) |
| 5.A | `query_reports_vcc_temp_and_led_durations` |
| 5.B–D | `set_on_duration_applies_from_next_on_phase`, `set_off_duration_applies_from_next_off_phase`, `duration_limits_500_to_9999` |
| 5.E | `invalid_input_reports_error`, `overlong_input_characterization` |
| 5 | `cr_lf_and_crlf_terminate_commands`, `single_byte_during_answer_is_not_lost`, `back_to_back_commands_overrun_stops_reception_characterization` |

¹ Rewrite only. These cover two deliberate fixes and are skipped for the reference build.

## Behaviour of the original that the rewrite keeps (characterisation tests)

* LED periods are stored as TIM3 reload values. `?` reports `LED 3999L` / `LED 5999D`,
  and `3500L` gives a 3501 ms phase.
* The first OFF phase after reset lasts 4 s (TIM3's initial period), not 6 s.
* On a press that toggles the LED, the LED lines come before `BUTTON ON`.
* The reply repeats the parsed number, not the received text (`0500L` → `LED 500L OK`).
* The RX buffer holds 6 characters. `1234567L` → `ERROR`, while `123456L` gets no answer.
* `?` followed by a NUL byte is accepted as a query.
* Only release→press is debounced. A press edge that bounces after it was handled
  counts as a short press.
* Commands are answered inside the RX interrupt with a blocking transmit. If 2 bytes
  arrive meanwhile (back-to-back commands, or typing during an LED-change printout),
  USART2 overruns, the HAL aborts the reception and nothing re-arms it: the board
  stops listening until reset.
* All output is sent from ISRs with priority equal to SysTick, so `HAL_GetTick()` loses
  about 1–3 ms per printout. Reported times are therefore slightly short (e.g. `LED ON TIME 3997`).

## Deliberate differences from the original

* Temperature is computed in signed arithmetic. The original subtracted `TS_CAL1` in
  unsigned arithmetic, so below 30 °C it printed huge values.
* Division by a zero VREFINT average (within ~13 ms after reset) yields 0, as Cortex-M
  `UDIV` does, instead of relying on it.
