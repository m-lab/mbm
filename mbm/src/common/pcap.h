#ifndef COMMON_PCAP_H
#define COMMON_PCAP_H

#include <string>

#include <pcap.h>
#include <stdint.h>

namespace mbm {
namespace pcap {

void Initialize(const std::string& filter, const char* device = NULL);
void Capture(uint32_t count, pcap_handler callback);
void Shutdown();

}  // namespace pcap
}  // namespace mbm

#endif  // COMMON_PCAP_H
