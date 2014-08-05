#include "server/model.h"

#include <stdint.h>

#include <algorithm>

#include "common/constants.h"

namespace mbm {
namespace model {

uint64_t target_pipe_size(int rate_kb_s, int rtt_ms, int mss_bytes) {
  // convert kilobits per second to bytes per milisecond
  uint64_t rate_bytes_ms = rate_kb_s / 8;
  return std::max(rate_bytes_ms * rtt_ms / mss_bytes,
                  static_cast<uint64_t>(MIN_TARGET_PIPE_SIZE));
}

uint64_t target_run_length(int rate_kb_s, int rtt_ms, int mss_bytes) {
  uint64_t pipe_size = target_pipe_size(rate_kb_s, rtt_ms, mss_bytes);
  return 3 * pipe_size * pipe_size;
}

} // namesapce model
} // namespace mbm
