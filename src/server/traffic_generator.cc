#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include <vector>

#include "mlab/accepted_socket.h"
#include "mlab/packet.h"
#include "common/time.h"
#include "server/traffic_generator.h"

namespace mbm {

TrafficGenerator::TrafficGenerator(mlab::AcceptedSocket *test_socket, uint32_t bytes_per_chunk)
    : test_socket_(test_socket),
      bytes_per_chunk(bytes_per_chunk),
      total_bytes_sent_(0),
      packets_sent_(0),
      buffer_(std::vector<char>(bytes_per_chunk,'x')) {
}
                 

uint32_t TrafficGenerator::send(int num_chunks){
  uint32_t bytes_sent = 0;
  for(int i=0; i<num_chunks; ++i){
    uint32_t seq_no = htonl(packets_sent_);
    memcpy(&buffer_[0], &seq_no, sizeof(packets_sent_));
    uint32_t nonce = rand();
    memcpy(&buffer_[0]+sizeof(packets_sent_), &nonce, sizeof(nonce));

    mlab::Packet chunk_packet(&buffer_[0], bytes_per_chunk);
    test_socket_->SendOrDie(chunk_packet);

    nonce_.push_back(nonce);
    timestamps_.push_back(GetTimeNS());

    bytes_sent += chunk_packet.length();
    ++packets_sent_;
  }
  total_bytes_sent_ += bytes_sent;
  
  return bytes_sent;
}

} // namespace mbm
