#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include <iostream>
#include <vector>

#include "mlab/accepted_socket.h"
#include "mlab/packet.h"
#include "common/constants.h"
#include "common/time.h"
#include "gflags/gflags.h"
#include "server/traffic_generator.h"

DECLARE_bool(verbose);

namespace mbm {

TrafficGenerator::TrafficGenerator(const mlab::AcceptedSocket *test_socket,
                                   uint32_t bytes_per_chunk)
    : test_socket_(test_socket),
      bytes_per_chunk(bytes_per_chunk),
      total_bytes_sent_(0),
      packets_sent_(0),
      last_percent_(0),
      buffer_(std::vector<char>(bytes_per_chunk,'x')) {
}
                 

uint32_t TrafficGenerator::send(int num_chunks){
  uint32_t bytes_sent = 0;
  for(int i=0; i<num_chunks; ++i){
    uint32_t seq_no = htonl(packets_sent_);
    memcpy(&buffer_[0], &seq_no, sizeof(packets_sent_));
    uint32_t nonce = htonl(rand());
    memcpy(&buffer_[0]+sizeof(packets_sent_), &nonce, sizeof(nonce));

    mlab::Packet chunk_packet(&buffer_[0], bytes_per_chunk);
    test_socket_->SendOrDie(chunk_packet);

    nonce_.push_back(ntohl(nonce));
    timestamps_.push_back(GetTimeNS());

    bytes_sent += chunk_packet.length();
    ++packets_sent_;

    if (FLAGS_verbose) {
      std::cout << "  s: " << std::hex << ntohl(seq_no) << " " << std::dec
                << ntohl(seq_no) << "\n";
      uint32_t percent = static_cast<uint32_t>(
          static_cast<float>(100 * packets_sent_) / TOTAL_PACKETS_TO_SEND);
      if (percent > last_percent_) {
        last_percent_ = percent;
        std::cout << "\r" << percent << "%" << std::flush;
      }
    }
  } // for loop
  total_bytes_sent_ += bytes_sent;
  
  return bytes_sent;
}

uint32_t TrafficGenerator::packets_sent(){
  return packets_sent_;
}

uint32_t TrafficGenerator::total_bytes_sent(){
  return total_bytes_sent_;
}

} // namespace mbm
