#pragma once

extern "C" {
#include <flashdb.h>
}
#include <array>
#include <string>

namespace test_nor {
extern std::array<uint8_t, 32768> flash;
extern bool fail_writes;
extern uint32_t fail_read_address;
extern size_t fail_read_size;
extern int write_count;
void open_image(const std::string &path);
void persist_image();
}  // namespace test_nor
