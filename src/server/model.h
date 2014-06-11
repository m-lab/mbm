#ifndef SERVER_MODEL_H
#define SERVER_MODEL_H

namespace mbm {
namespace model {

int target_pipe_size(int rate_kb_s, int rtt_ms, int mss_bytes);

int target_run_length(int rate_kb_s, int rtt_ms, int mss_bytes);
    
} // namespace model
} // namespace mbm
#endif // SERVER_MODEL_H
