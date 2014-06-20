#ifndef COMMON_TRAFFIC_DATA_H_
#define COMMON_TRAFFIC_DATA_H_

#include <stdint.h>

namespace mbm {
class TrafficData {
  public:
    static TrafficData ntoh(const TrafficData &data);
    static TrafficData hton(const TrafficData &data);
    TrafficData(uint32_t seq_no, uint32_t nonce, double timestamp);
    TrafficData();
    uint32_t seq_no() const { return seq_no_; };
    uint32_t nonce() const { return nonce_; };
    double timestamp() const { return timestamp_; };

  private:
    uint32_t seq_no_;
    uint32_t nonce_;
    double timestamp_;
};
}  // namespace mbm

#endif  // COMMON_TRAFFIC_DATA_H
