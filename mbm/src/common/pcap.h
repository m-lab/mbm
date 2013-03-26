#ifndef COMMON_PCAP_H
#define COMMON_PCAP_H

#ifndef USE_PCAP
#error USE_PCAP should be defined if you expect this to work.
#endif  // !USE_PCAP

#include <pcap.h>
#include <stdint.h>

#include <string>

namespace mbm {
namespace pcap {

void Initialize(const std::string& filter, const char* device = NULL);
void Capture(uint32_t count, pcap_handler callback);
void Shutdown();

}  // namespace pcap
}  // namespace mbm

#endif  // COMMON_PCAP_H
