#include "common/config.h"

#include <assert.h>
#include <stdint.h>

#include <string>

namespace mbm {
namespace {
const std::string delimiter = ":";
}

Config::Config()
    : socket_type(static_cast<SocketType>(-1)),
      cbr_kb_s(0),
      rtt_ms(0),
      mss_bytes(0) {
}

Config::Config(SocketType socket_type, uint32_t cbr_kb_s,
               uint32_t rtt_ms, uint32_t mss_bytes)
    : socket_type(socket_type),
      cbr_kb_s(cbr_kb_s),
      rtt_ms(rtt_ms),
      mss_bytes(mss_bytes) {
}

}  // namespace mbm
