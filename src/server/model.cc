#include "server/model.h"

namespace mbm {
namespace model {

int target_pipe_size(int rate_kb_s, int rtt_ms, int mss_bytes) {
  int rate_bytes_ms = (rate_kb_s * 1000) / 8;
  return rate_bytes_ms * rtt_ms / mss_bytes;
}

int target_run_length(int rate_kb_s, int rtt_ms, int mss_bytes) {
  int pipe_size = target_pipe_size(rate_kb_s, rtt_ms, mss_bytes);
  return 3 * pipe_size * pipe_size;
}

} // namesapce model
} // namespace mbm
