#include "common/traffic_data.h"

#include <arpa/inet.h>

namespace mbm {

TrafficData::TrafficData()
  : seq_no_(0), nonce_(0), timestamp_(0.0) {
}

TrafficData::TrafficData(uint32_t seq_no, uint32_t nonce, double timestamp)
  : seq_no_(seq_no), nonce_(nonce), timestamp_(timestamp) {
}

// static
TrafficData TrafficData::ntoh(const TrafficData& data) {
  return TrafficData(ntohl(data.seq_no_), ntohl(data.nonce_), data.timestamp_);
}

// static
TrafficData TrafficData::hton(const TrafficData& data) {
  return TrafficData(htonl(data.seq_no_), htonl(data.nonce_), data.timestamp_);
}
    
} // namespace mbm
