#ifndef COMMON_TRAFFIC_DATA_H_
#define COMMON_TRAFFIC_DATA_H_

#include <stdint.h>

namespace mbm {
class TrafficData {
  public:
    static TrafficData ntoh(const TrafficData &data);
    static TrafficData hton(const TrafficData &data);
    TrafficData(uint32_t seq_no, uint32_t nonce, uint64_t timestamp);
    TrafficData();
    uint32_t seq_no() const { return seq_no_; };
    uint32_t nonce() const { return nonce_; };
    uint64_t timestamp() const;

  private:
    TrafficData(uint32_t seq_no, uint32_t nonce, uint32_t sec, uint32_t rem);
    uint32_t seq_no_;
    uint32_t nonce_;
    uint32_t sec_;
    uint32_t rem_;
};
}  // namespace mbm

#endif  // COMMON_TRAFFIC_DATA_H
