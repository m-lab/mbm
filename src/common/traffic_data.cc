#include "common/traffic_data.h"

#include <arpa/inet.h>

#include <iostream>

#include "common/constants.h"

namespace mbm {


// static
TrafficData TrafficData::ntoh(const TrafficData& data) {
  return TrafficData(
           ntohl(data.seq_no_), ntohl(data.nonce_),
           ntohl(data.sec_), ntohl(data.rem_));
}

// static
TrafficData TrafficData::hton(const TrafficData& data) {
  return TrafficData(
           htonl(data.seq_no_), htonl(data.nonce_),
           htonl(data.sec_), htonl(data.rem_));
}

TrafficData::TrafficData()
  : seq_no_(0), nonce_(0), sec_(0), rem_(0) {
}

TrafficData::TrafficData(uint32_t seq_no, uint32_t nonce, uint64_t timestamp)
  : seq_no_(seq_no), nonce_(nonce) {
  sec_ = timestamp / NS_PER_SEC;
  rem_ = timestamp % NS_PER_SEC;
}

TrafficData::TrafficData(uint32_t seq_no, uint32_t nonce,
                         uint32_t sec, uint32_t rem)
  : seq_no_(seq_no), nonce_(nonce), sec_(sec), rem_(rem) {
}

uint64_t TrafficData::timestamp() const {
  return static_cast<uint64_t>(sec_) * NS_PER_SEC + static_cast<uint64_t>(rem_);
}


} // namespace mbm
