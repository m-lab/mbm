#ifndef SERVER_TRAFFIC_GENERATOR
#define SERVER_TRAFFIC_GENERATOR

#include <vector>

#include "mlab/accepted_socket.h"

namespace mbm {

class TrafficGenerator {  
  public:
    TrafficGenerator(mlab::AcceptedSocket *test_socket, uint32_t bytes_per_chunk);
    uint32_t send(int num_chunks);

  private:
    mlab::AcceptedSocket *test_socket_;
    uint32_t bytes_per_chunk;
    uint32_t total_bytes_sent_;
    uint32_t packets_sent_;
    std::vector<char> buffer_;
    std::vector<uint32_t> nonce_;
    std::vector<uint64_t> timestamps_;
    
};

} // namespace mbm

#endif // SERVER_TRAFFIC_GENERATOR 
