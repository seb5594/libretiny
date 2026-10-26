#include <array>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "protocol_codec.h"

using namespace esphome::libretiny_matter_protocol;

int main(int argc, char **argv) {
  if (argc == 1) {
    const auto result = run_protocol_tests();
    std::cout << "{\"passed\":" << result.passed << ",\"failed\":" << result.failed << ",\"first_failure\":\""
              << (result.first_failure == nullptr ? "" : result.first_failure) << "\"}\n";
    return result.failed == 0 ? 0 : 1;
  }
  if (std::string(argv[1]) == "fuzz") {
    std::mt19937 random(0x7231);
    std::array<uint8_t, 513> data{};
    for (unsigned i = 0; i < 20000; i++) {
      const size_t length = random() % data.size();
      for (size_t j = 0; j < length; j++) data[j] = random();
      InvokeInfo info{};
      const auto error = inspect_invoke(chip::ByteSpan(data.data(), length), info);
      (void)error;  // Both successful and rejected parses must remain memory-safe.
    }
    std::cout << "20000 deterministic malformed inputs completed\n";
    return 0;
  }
  const std::string hex = argv[1];
  if (hex.size() % 2 != 0)
    return 2;
  std::vector<uint8_t> data;
  for (size_t i = 0; i < hex.size(); i += 2) {
    const auto pair = hex.substr(i, 2);
    char *end;
    const auto byte = std::strtoul(pair.c_str(), &end, 16);
    if (end != pair.c_str() + 2)
      return 2;
    data.push_back(byte);
  }
  InvokeInfo info{};
  auto error = inspect_invoke(chip::ByteSpan(data.data(), data.size()), info);
  std::cout << "{\"error\":" << error.AsInteger() << ",\"endpoint\":" << info.endpoint
            << ",\"cluster\":" << info.cluster << ",\"command\":" << info.command
            << ",\"revision\":" << static_cast<unsigned>(info.revision) << "}\n";
  return error == CHIP_NO_ERROR ? 0 : 1;
}
