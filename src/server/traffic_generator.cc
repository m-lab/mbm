#include "server/traffic_generator.h"

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
#include "server/web100.h"

DECLARE_bool(verbose);

namespace mbm {

TrafficGenerator::TrafficGenerator(const mlab::AcceptedSocket *test_socket,
                                   uint32_t bytes_per_chunk, uint32_t max_pkt)
    : test_socket_(test_socket),
      max_packets_(max_pkt),
      bytes_per_chunk_(bytes_per_chunk),
      total_bytes_sent_(0),
      packets_sent_(0),
      last_percent_(0),
      buffer_(std::vector<char>(bytes_per_chunk,'x')) {
  nonce_.reserve(max_pkt);
  timestamps_.reserve(max_pkt);
}

bool TrafficGenerator::Send(uint32_t num_chunks, ssize_t& num_bytes){
  num_bytes = 0;
  ssize_t local_num_bytes;
  for(uint32_t i=0; i<num_chunks; ++i){
    uint32_t seq_no = htonl(packets_sent_);
    memcpy(&buffer_[0], &seq_no, sizeof(packets_sent_));
    uint32_t nonce = htonl(rand());
    memcpy(&buffer_[0]+sizeof(packets_sent_), &nonce, sizeof(nonce));


    mlab::Packet chunk_packet(&buffer_[0], bytes_per_chunk_);

    if (!test_socket_->Send(chunk_packet, &local_num_bytes)) {
      num_bytes = -1;
      return false;
    }

    nonce_.push_back(ntohl(nonce));
    timestamps_.push_back(GetTimeNS());

    num_bytes += chunk_packet.length();
    ++packets_sent_;

    if (FLAGS_verbose) {
      std::cout << "  s: " << std::hex << ntohl(seq_no) << " " << std::dec
                << ntohl(seq_no) << "\n";
      std::cout << "  nonce: " << std::hex << ntohl(nonce) << " " << std::dec
                << ntohl(nonce) << "\n";
      if (max_packets_ != 0) {
        uint32_t percent = static_cast<uint32_t>(
            static_cast<float>(100 * packets_sent_) / max_packets_);
        if (percent > last_percent_) {
          last_percent_ = percent;
          std::cout << "\r" << percent << "%" << std::flush;
        }
      }
    }
  } // for loop
  total_bytes_sent_ += num_bytes;
  
  return (static_cast<unsigned>(num_bytes) == num_chunks * bytes_per_chunk_);
}

bool TrafficGenerator::Send(uint32_t num_chunks) {
  ssize_t num_bytes;
  return Send(num_chunks, num_bytes);
}

uint32_t TrafficGenerator::packets_sent(){
  return packets_sent_;
}

uint64_t TrafficGenerator::total_bytes_sent(){
  return total_bytes_sent_;
}

uint32_t TrafficGenerator::bytes_per_chunk(){
  return bytes_per_chunk_;
}


const std::vector<uint32_t>& TrafficGenerator::nonce() {
  return nonce_;
}

const std::vector<uint64_t>& TrafficGenerator::timestamps() {
  return timestamps_;
}

} // namespace mbm
