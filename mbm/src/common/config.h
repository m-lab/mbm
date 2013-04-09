#ifndef CONFIG_H_
#define CONFIG_H_

#include <stdint.h>

#include <string>

class Config {
 public:
  Config();
  Config(uint32_t cbr_kb_s, double loss_threshold);

  uint32_t cbr_kb_s;
  double loss_threshold;

  std::string AsString() const;
  void FromString(const std::string& config_str);
};

#endif  // CONFIG_H_
