#ifndef COMMON_CONFIG_H_
#define COMMON_CONFIG_H_

#include <stdint.h>
#include <string>

#include "mlab/socket_type.h"

namespace mbm {
class Config {
 public:
  Config();
  Config(SocketType socket_type, uint32_t cbr_kb_s,
         uint32_t rtt_ms, uint32_t mss_bytes, uint32_t burst_size);

  SocketType socket_type;
  uint32_t cbr_kb_s;
  uint32_t rtt_ms;
  uint32_t mss_bytes;
  uint32_t burst_size;
};
}  // namespace mbm

#endif  // COMMON_CONFIG_H_
