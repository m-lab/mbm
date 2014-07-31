#ifndef SERVER_TRAFFIC_GENERATOR
#define SERVER_TRAFFIC_GENERATOR

#include <vector>

#include "mlab/accepted_socket.h"

namespace mbm {

class TrafficGenerator {  
  public:
    TrafficGenerator(const mlab::AcceptedSocket *test_socket,
                     uint32_t bytes_per_chunk, uint32_t max_pkt);
    bool Send(uint32_t num_chunks, ssize_t& num_bytes);
    bool Send(uint32_t num_chunks);
    uint32_t packets_sent();
    uint64_t total_bytes_sent();
    uint32_t bytes_per_chunk();
    const std::vector<uint32_t>& nonce();
    const std::vector<uint64_t>& timestamps();

  private:
    const mlab::AcceptedSocket *test_socket_;
    uint32_t max_packets_;
    uint32_t bytes_per_chunk_;
    uint64_t total_bytes_sent_;
    uint32_t packets_sent_;
    uint32_t last_percent_;
    std::vector<char> buffer_;
    std::vector<uint32_t> nonce_;
    std::vector<uint64_t> timestamps_;
    
};

} // namespace mbm

#endif // SERVER_TRAFFIC_GENERATOR 
