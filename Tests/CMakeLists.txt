# Host-side tests for the STM32F411E-DISCO firmware.
#
#   cmake -S Tests -B Tests/build
#   cmake --build Tests/build --config Release
#   ctest --test-dir Tests/build -C Release --output-on-failure
#
# Targets:
#   fw_reference_tests  original C firmware (Tests/reference/main.c) + emulator
#   fw_cpp_tests        current firmware (Core/Src/main.c + C++ application) + emulator
#   unit_tests          C++ application classes against a fake board
cmake_minimum_required(VERSION 3.16)
project(stm32_firmware_host_tests C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 14)  # what the application code must compile with
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(FAKE_HAL_INC "${CMAKE_CURRENT_SOURCE_DIR}/emulator/include")
set(BSP_INC "${REPO_ROOT}/Drivers/BSP/STM32F411E-Discovery")

if(MSVC)
  add_compile_options(/utf-8 /W3 /D_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

find_package(Threads REQUIRED)

# Emulator + framework + system test cases (firmware independent).
set(EMULATOR_SOURCES
  emulator/emulator.cpp
  framework/test_framework.cpp
  cases/fuzz_scenario.cpp
  cases/test_system.cpp
  cases/test_led.cpp
  cases/test_button.cpp
  cases/test_adc.cpp
  cases/test_uart.cpp
  cases/main.cpp)

# The fake HAL directory must come first so that "stm32f4xx_hal.h" and
# "pdm2pcm.h" resolve to the emulator's versions while the real Core/Inc/main.h
# and the real BSP header are used unchanged.
set(FIRMWARE_INCLUDES "${FAKE_HAL_INC}" "${REPO_ROOT}/Core/Inc" "${BSP_INC}")

function(add_firmware_test_exe name variant is_reference main_c)
  add_executable(${name} ${EMULATOR_SOURCES} ${main_c} ${ARGN})
  target_include_directories(${name} PRIVATE ${FIRMWARE_INCLUDES} emulator framework cases)
  target_compile_definitions(${name} PRIVATE FW_VARIANT="${variant}" FW_IS_REFERENCE=${is_reference})
  # main() of the firmware runs on its own thread, the test owns the real main().
  set_source_files_properties(${main_c} TARGET_DIRECTORY ${name} PROPERTIES COMPILE_DEFINITIONS "main=fw_main")
  target_link_libraries(${name} PRIVATE Threads::Threads)
endfunction()

enable_testing()

add_firmware_test_exe(fw_reference_tests "reference C firmware" 1 "${CMAKE_CURRENT_SOURCE_DIR}/reference/main.c")
add_test(NAME emulator_reference COMMAND fw_reference_tests)

if(EXISTS "${REPO_ROOT}/Core/Inc/app.h")
  file(GLOB APP_SOURCES CONFIGURE_DEPENDS "${REPO_ROOT}/Core/Src/app/*.cpp")
  add_firmware_test_exe(fw_cpp_tests "C++ firmware" 0 "${REPO_ROOT}/Core/Src/main.c" ${APP_SOURCES})
  # The application code is held to a stricter warning level.
  if(MSVC)
    set_source_files_properties(${APP_SOURCES} TARGET_DIRECTORY fw_cpp_tests PROPERTIES COMPILE_OPTIONS "/W4;/WX")
  else()
    set_source_files_properties(${APP_SOURCES} TARGET_DIRECTORY fw_cpp_tests PROPERTIES COMPILE_OPTIONS "-Wpedantic;-Wconversion;-Werror")
  endif()
  add_test(NAME emulator_cpp COMMAND fw_cpp_tests)
  add_test(NAME differential_fuzz COMMAND fw_cpp_tests --diff $<TARGET_FILE:fw_reference_tests> 150)

  # Application classes only (no HAL, no emulator) against a fake board.
  file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "${REPO_ROOT}/Core/Src/app/*.cpp")
  list(FILTER CORE_SOURCES EXCLUDE REGEX "(app_bridge|board_stm32)\\.cpp$")
  add_executable(unit_tests ${CORE_SOURCES} framework/test_framework.cpp unit/fake_board.cpp unit/test_core.cpp unit/main.cpp)
  target_include_directories(unit_tests PRIVATE "${REPO_ROOT}/Core/Inc" framework unit)
  target_compile_definitions(unit_tests PRIVATE
    MAIN_C_PATH="${REPO_ROOT}/Core/Src/main.c"
    REFERENCE_MAIN_C_PATH="${CMAKE_CURRENT_SOURCE_DIR}/reference/main.c")
  add_test(NAME unit COMMAND unit_tests)
endif()
