#ifdef USE_PCAP

#include "common/pcap.h"

#include <assert.h>

#include <iostream>

namespace mbm {
namespace pcap {
namespace {

pcap_t* handle;

}  // namespace

void Initialize(const std::string& filter, const char* dev /*= NULL*/) {
  char errbuf[PCAP_ERRBUF_SIZE];
  const char* device = dev ? dev : pcap_lookupdev(errbuf);
  if (device == NULL) {
    std::cerr << "Failed to find default device: " << errbuf << "\n";
    assert(false);
  }

  handle = pcap_open_live(device, BUFSIZ, 0, 0, errbuf);
  if (handle == NULL) {
    std::cerr << "Failed to open device '" << device << "': " << errbuf << "\n";
    assert(false);
  }

  bpf_u_int32 mask = 0;
  bpf_u_int32 net = 0;
  if (pcap_lookupnet(device, &net, &mask, errbuf) == -1) {
    std::cerr << "Failed to get netmask for device '" << device << "': " <<
                 errbuf << "\n";
  }

  struct bpf_program fp;
  if (pcap_compile(handle, &fp, filter.c_str(), 0, net) < 0) {
    std::cerr << "Failed to parse filter '" << filter << "': " <<
                 pcap_geterr(handle) << "\n";
    assert(false);
  }
  if (pcap_setfilter(handle, &fp) == -1) {
    std::cerr << "Failed to install filter '" << filter << "': " <<
                 pcap_geterr(handle) << "\n";
    assert(false);
  }
  pcap_freecode(&fp);
}

void Capture(uint32_t count, pcap_handler callback) {
  int pcap_packets = pcap_dispatch(handle, count, callback, NULL);
  if (pcap_packets == -1) {
    std::cerr << "Failed to dispatch capture: " << pcap_geterr(handle) << "\n";
    assert(false);
  } else if (pcap_packets == -2) {
    std::cerr << "Warning: pcap may be incomplete due to breakloop.\n";
  }
}

void Shutdown() {
  pcap_breakloop(handle);
  pcap_close(handle);
}

}  // namespace pcap
}  // namespace mbm

#endif  // USE_PCAP
