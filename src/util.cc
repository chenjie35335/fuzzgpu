#include <iomanip>

#include "util.hh"

namespace fuzzgpu {

std::string hexdump(const uint8_t *bytes, size_t size_) {
  std::stringstream ss;

  for (size_t i = 0; i < size_; i += 16) {
    // address
    ss << std::hex << std::setw(8) << std::setfill('0') << i << "  ";

    for (size_t j = 0; j < 16; ++j) {
      if (i + j < size_) {
        ss << std::setw(2) << std::setfill('0')
           << static_cast<int>(bytes[i + j]) << " ";
      } else {
        ss << "   ";
      }
      if (j == 7)
        ss << " ";
    }

    ss << " ";

    // ASCII
    for (size_t j = 0; j < 16; ++j) {
      if (i + j < size_) {
        char c = bytes[i + j];
        ss << (std::isprint(c) ? c : '.');
      }
    }
    ss << "\n";
  }
  return ss.str();
}

uint64_t saturated_add(uint64_t x, uint64_t y) {
    uint64_t result = x + y;
    if (result < x || result < y) {
        return std::numeric_limits<uint64_t>::max(); // overflow
    }
    return result;
}

} // namespace fuzzgpu