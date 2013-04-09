#include "common/config.h"

#include <assert.h>
#include <stdint.h>

#include <sstream>
#include <string>

namespace {
const std::string delimiter = ":";
}

Config::Config() : cbr_kb_s(0), loss_threshold(0.0) {
}

Config::Config(uint32_t cbr_kb_s, double loss_threshold)
    : cbr_kb_s(cbr_kb_s), loss_threshold(loss_threshold) { }

std::string Config::AsString() const {
  std::ostringstream ss;
  ss << cbr_kb_s << delimiter << loss_threshold << delimiter;
  return ss.str();
}

void Config::FromString(const std::string& str) {
  std::string::size_type last_pos = str.find_first_not_of(delimiter, 0);
  std::string::size_type pos = str.find_first_of(delimiter, last_pos);
  std::istringstream(str.substr(last_pos, pos - last_pos)) >> cbr_kb_s;

  last_pos = str.find_first_not_of(delimiter, pos);
  pos = str.find_first_of(delimiter, last_pos);
  std::istringstream(str.substr(last_pos, pos - last_pos)) >> loss_threshold;
}
