#include <lib/core/TLVReader.h>
#include <lib/core/TLVWriter.h>

#include <array>
#include <cstring>
#include <limits>

#include "protocol_codec.h"

namespace esphome::libretiny_matter_protocol {

namespace {

// Matter InvokeRequest: untimed, endpoint 1, OnOff (0x0006), On (0x01),
// empty command fields, InteractionModelRevision 12 of the pinned upstream.
constexpr uint8_t ON_REQUEST[] = {0x15, 0x28, 0x00, 0x28, 0x01, 0x36, 0x02, 0x15, 0x37, 0x00,
                                  0x24, 0x00, 0x01, 0x24, 0x01, 0x06, 0x24, 0x02, 0x01, 0x18,
                                  0x35, 0x01, 0x18, 0x18, 0x18, 0x24, 0xff, 0x0c, 0x18};

void check(TestResult &result, bool passed, const char *name) {
  if (passed) {
    result.passed++;
  } else {
    result.failed++;
    if (result.first_failure == nullptr)
      result.first_failure = name;
  }
}

}  // namespace

TestResult run_protocol_tests() {
  TestResult result;
  std::array<uint8_t, 96> buffer{};
  for (uint32_t command = 0; command <= 2; command++) {
    chip::MutableByteSpan output(buffer.data(), buffer.size());
    auto error = encode_invoke(1, command, output);
    uint8_t expected[sizeof(ON_REQUEST)];
    memcpy(expected, ON_REQUEST, sizeof(expected));
    expected[18] = command;
    check(result,
          error == CHIP_NO_ERROR && output.size() == sizeof(expected) &&
              memcmp(output.data(), expected, sizeof(expected)) == 0,
          "invoke-wire-vector");
    InvokeInfo info{};
    error = inspect_invoke(chip::ByteSpan(expected), info);
    check(result,
          error == CHIP_NO_ERROR && info.endpoint == 1 && info.cluster == 6 && info.command == command &&
              info.revision == 12,
          "invoke-upstream-parser");
  }
  for (size_t length = 0; length < sizeof(ON_REQUEST); length++) {
    InvokeInfo info{99, 99, 99, 99};
    check(result, inspect_invoke(chip::ByteSpan(ON_REQUEST, length), info) != CHIP_NO_ERROR && info.endpoint == 99,
          "truncated-invoke");
    buffer.fill(0xa5);
    chip::MutableByteSpan output(buffer.data() + 1, length);
    check(result, encode_invoke(1, 1, output) != CHIP_NO_ERROR && buffer[0] == 0xa5 && buffer[length + 1] == 0xa5,
          "small-output-buffer");
  }
  memcpy(buffer.data(), ON_REQUEST, sizeof(ON_REQUEST));
  buffer[sizeof(ON_REQUEST)] = 0x09;  // anonymous true after the root structure
  InvokeInfo info{};
  check(result, inspect_invoke(chip::ByteSpan(buffer.data(), sizeof(ON_REQUEST) + 1), info) != CHIP_NO_ERROR,
        "trailing-element");
  buffer[0] = 0x16;  // array instead of the required root structure
  check(result, inspect_invoke(chip::ByteSpan(buffer.data(), sizeof(ON_REQUEST)), info) != CHIP_NO_ERROR, "wrong-root");
  memcpy(buffer.data(), ON_REQUEST, sizeof(ON_REQUEST));
  buffer[3] = 0x29;  // timed requests are outside the test helper's scope
  check(result, inspect_invoke(chip::ByteSpan(buffer.data(), sizeof(ON_REQUEST)), info) != CHIP_NO_ERROR,
        "timed-request");

  // Independent TLV wire vectors: little endian uint64, minimum int64, UTF-8.
  constexpr uint8_t UINT64_VECTOR[] = {0x07, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  constexpr uint8_t INT64_VECTOR[] = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80};
  chip::TLV::TLVWriter writer;
  writer.Init(buffer.data(), buffer.size());
  auto error = writer.Put(chip::TLV::AnonymousTag(), std::numeric_limits<uint64_t>::max());
  check(result,
        error == CHIP_NO_ERROR && writer.GetLengthWritten() == sizeof(UINT64_VECTOR) &&
            memcmp(buffer.data(), UINT64_VECTOR, sizeof(UINT64_VECTOR)) == 0,
        "uint64-little-endian");
  writer.Init(buffer.data(), buffer.size());
  error = writer.Put(chip::TLV::AnonymousTag(), std::numeric_limits<int64_t>::min());
  check(result,
        error == CHIP_NO_ERROR && writer.GetLengthWritten() == sizeof(INT64_VECTOR) &&
            memcmp(buffer.data(), INT64_VECTOR, sizeof(INT64_VECTOR)) == 0,
        "int64-little-endian");
  chip::TLV::TLVReader reader;
  reader.Init(chip::ByteSpan(UINT64_VECTOR));
  uint32_t narrow = 0;
  error = reader.Next();
  check(result, error == CHIP_NO_ERROR && reader.Get(narrow) != CHIP_NO_ERROR, "integer-overflow");
  constexpr uint8_t OVERSIZED_STRING[] = {0x10, 0xff, 0x01};
  reader.Init(chip::ByteSpan(OVERSIZED_STRING));
  error = reader.Next();
  chip::ByteSpan bytes;
  check(result, error != CHIP_NO_ERROR || reader.Get(bytes) != CHIP_NO_ERROR, "truncated-string");
  constexpr uint8_t UTF8_VECTOR[] = {0x0c, 0x02, 0xc3, 0xa4};
  writer.Init(buffer.data(), buffer.size());
  error = writer.PutString(chip::TLV::AnonymousTag(), "\xc3\xa4");
  check(result,
        error == CHIP_NO_ERROR && writer.GetLengthWritten() == sizeof(UTF8_VECTOR) &&
            memcmp(buffer.data(), UTF8_VECTOR, sizeof(UTF8_VECTOR)) == 0,
        "utf8-wire-vector");
  return result;
}

}  // namespace esphome::libretiny_matter_protocol
