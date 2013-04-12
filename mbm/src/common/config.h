#ifndef CONFIG_H_
#define CONFIG_H_

#include <stdint.h>

#include <string>

#include "mlab/socket_type.h"

class Config {
 public:
  Config(SocketType socket_type, uint32_t cbr_kb_s, double loss_threshold);
  Config(const std::string& str);

  SocketType socket_type;
  uint32_t cbr_kb_s;
  double loss_threshold;

  std::string AsString() const;
};

#endif  // CONFIG_H_
