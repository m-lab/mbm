#ifndef SERVER_MODEL_H
#define SERVER_MODEL_H

#include <stdint.h>

namespace mbm {
namespace model {

uint64_t target_pipe_size(int rate_kb_s, int rtt_ms, int mss_bytes);

uint64_t target_run_length(int rate_kb_s, int rtt_ms, int mss_bytes);
    
} // namespace model
} // namespace mbm
#endif // SERVER_MODEL_H
