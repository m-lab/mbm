#ifndef COMMON_CONFIG_H_
#define COMMON_CONFIG_H_

#include <stdint.h>
#include <string>

#include "mlab/socket_type.h"

namespace mbm {
class Config {
 public:
  Config(SocketType socket_type, uint32_t cbr_kb_s, double loss_threshold);
  Config(const std::string& str);

  SocketType socket_type;
  uint32_t cbr_kb_s;
  double loss_threshold;

  std::string AsString() const;
};
}  // namespace mbm

#endif  // COMMON_CONFIG_H_
