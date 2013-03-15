#ifndef CONFIG_H_
#define CONFIG_H_

#include <stdint.h>

#include <string>

class Config {
 public:
  Config();
  Config(uint32_t low_cbr_kb_s, uint32_t high_cbr_kb_s, uint32_t time_per_step);

  uint32_t low_cbr_kb_s;
  uint32_t high_cbr_kb_s;
  uint32_t time_per_test;

  std::string AsString() const;
  void FromString(const std::string& config_str);
};

#endif  // CONFIG_H_
