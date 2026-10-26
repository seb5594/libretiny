#pragma once

#include <lib/core/CHIPError.h>
#include <lib/support/Span.h>

namespace esphome::libretiny_matter_protocol {

// Codec test helpers for one untimed On/Off invocation. These operate on the
// Interaction Model payload, not on an authenticated Matter session.
struct InvokeInfo {
  uint16_t endpoint;
  uint32_t cluster;
  uint32_t command;
  uint8_t revision;
};

CHIP_ERROR encode_invoke(uint16_t endpoint, uint32_t command, chip::MutableByteSpan &output);
CHIP_ERROR inspect_invoke(chip::ByteSpan input, InvokeInfo &output);

struct TestResult {
  unsigned passed{0};
  unsigned failed{0};
  const char *first_failure{nullptr};
};

TestResult run_protocol_tests();

}  // namespace esphome::libretiny_matter_protocol
