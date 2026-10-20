#include "fuzz_scenario.hpp"
#include "test_framework.hpp"

int main(int argc, char** argv) {
  return tf::main(argc, argv, FW_VARIANT, FW_IS_REFERENCE != 0, &runFuzzScenario);
}
