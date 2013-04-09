#include <assert.h>
#include <errno.h>
#ifdef USE_PCAP
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#endif  // USE_PCAP
#include <netinet/tcp.h>
#ifdef USE_PCAP
#include <pcap.h>
#endif
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#ifdef USE_WEB100
extern "C" {
#include <web100/web100.h>
}
#endif

#include <iostream>
#include <map>
#include <string>
#include <sstream>

#include "common/config.h"
#include "common/constants.h"
#ifdef USE_PCAP
#include "common/pcap.h"
#endif  // USE_PCAP
#ifdef USE_WEB100
#include "common/web100.h"
#endif  // USE_WEB100
#include "common/scoped_ptr.h"
#include "mlab/mlab.h"
#include "mlab/server_socket.h"

#define PACKETS_PER_CHUNK 3
#define TOTAL_PACKETS_TO_SEND 500

namespace mbm {

#ifdef USE_PCAP
std::set<uint32_t> sequence_nos;
#endif  // USE_PCAP

uint32_t lost_packets = 0;

uint32_t get_time_ns() {
  struct timespec time;
  clock_gettime(CLOCK_MONOTONIC_RAW, &time);
  return time.tv_sec * 1000000000 + time.tv_nsec;
}

#ifdef USE_PCAP
void pcap_callback(u_char* args, const struct pcap_pkthdr* header,
                   const u_char* packet) {
  size_t packet_len = header->len;
  if (packet_len < sizeof(struct ethhdr) + sizeof(struct iphdr)) {
    std::cerr << "X";
    return;
  }
  const struct iphdr* ip = reinterpret_cast<const struct iphdr*>(
      packet + sizeof(struct ethhdr));
  packet_len -= sizeof(struct ethhdr);
  size_t ip_header_len = ip->ihl * 4;
  if (packet_len < ip_header_len) {
    std::cerr << "X";
    return;
  }

  if (ip->protocol != IPPROTO_TCP) {
    std::cerr << "P";
    return;
  }

  const struct tcphdr* tcp_header = reinterpret_cast<const struct tcphdr*>(
      packet + sizeof(struct ethhdr) + ip_header_len);
  packet_len -= ip_header_len;
  if (packet_len < sizeof(tcp_header)) {
    std::cerr << "X";
    return;
  }

  // If the sequence number is a duplicate, count it as lost.
  if (sequence_nos.insert(tcp_header->seq).second == false)
    ++lost_packets;
  // std::cout << "[PCAP] " << tcp_header->seq << "\n";
}
#endif  // USE_PCAP

double RunCBR(const mlab::Socket* socket, uint32_t cbr_kb_s) {
  std::cout << "Running CBR at " << cbr_kb_s << "\n";

#ifdef USE_PCAP
  sequence_nos.clear();
#endif  // USE_PCAP
  lost_packets = 0;

  // TODO(dominic): Should we tell the client the |bytes_per_chunk| so they know
  // how much to ask for on every tick?
  uint32_t bytes_per_chunk = PACKETS_PER_CHUNK * TCP_MSS;
  uint32_t bytes_per_ms = cbr_kb_s * 1024 / (8 * 1000);
  uint32_t time_per_chunk_ns = (1000000 * bytes_per_chunk) / bytes_per_ms;

  std::cout << "  bytes_per_chunk: " << bytes_per_chunk << "\n";
  std::cout << "  bytes_per_ms: " << bytes_per_ms << "\n";
  std::cout << "  time_per_chunk_ns: " << time_per_chunk_ns << "\n";

  std::string chunk_data(bytes_per_chunk, 'b');

#ifdef USE_WEB100
  web100::Start();
#endif
  uint32_t packets_sent = 0;
  while (packets_sent < TOTAL_PACKETS_TO_SEND) {
  //  std::cout << '.' << std::flush;
    uint32_t start_time = get_time_ns();

    // Rerandomize the chunk data just in case someone is trying to be too
    // clever.
    for (uint32_t i = 0; i < chunk_data.size(); ++i)
      chunk_data[i] = static_cast<char>(rand() % 255);

    // And send
    socket->Send(chunk_data);
    packets_sent += PACKETS_PER_CHUNK;

#ifdef USE_PCAP
    // TODO(dominic): How can we capture but retain high CBR?
    pcap::Capture(PACKETS_PER_CHUNK, pcap_callback);
#endif  // USE_PCAP

    // If we have time left over, sleep the remainder.
    uint32_t end_time = get_time_ns();
    uint32_t time_taken_to_send_ns = end_time - start_time;
    int32_t left_over_ns = time_per_chunk_ns - time_taken_to_send_ns;

    if (left_over_ns > 0) {
      std::cout << "." << std::flush;

      struct timespec sleep_req = {left_over_ns / 1000000000,
                                   left_over_ns % 1000000000};
      struct timespec sleep_rem;
      int slept = nanosleep(&sleep_req, &sleep_rem);
      while (slept == -1) {
        assert(errno == EINTR);
        slept = nanosleep(&sleep_rem, &sleep_rem);
      }
    } else {
      // Warning: Took longer to send than we budgeted.
      std::cout << "o" << std::flush;
    }
  }
  socket->Send(END_OF_LINE);
  std::cout << "\n";
#ifdef USE_WEB100
  web100::Stop();
  lost_packets = web100::GetLossCount();
#endif

  std::cout << "  lost: " << lost_packets << "\n";
  std::cout << "  sent: " << packets_sent << "\n";

  return static_cast<double>(lost_packets) / packets_sent;
}

}  // namespace mbm

int main(int argc, const char* argv[]) {
  using namespace mbm;

  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <port>\n";
    return 1;
  }

  mlab::Initialize("mbm_server", MBM_VERSION);
  mlab::SetLogSeverity(mlab::VERBOSE);

  const char* port = argv[1];

#ifdef USE_PCAP
  // TODO(dominic): Either findalldevs or do something to encourage this device
  // to be correct.
  pcap::Initialize(std::string("src localhost and src port ") + port, "lo");
#endif  // USE_PCAP

#ifdef USE_WEB100
    web100::Initialize();
#endif

  while (true) {
    scoped_ptr<mlab::ServerSocket> socket(
        mlab::ServerSocket::CreateOrDie(atoi(port)));

    socket->Select();
    socket->Accept();

    Config config;
    config.FromString(socket->Receive(1024));

    std::cout << "Setting config [" << config.cbr_kb_s << " kb/s | " <<
                 config.loss_threshold << " %]\n";

#ifdef USE_WEB100
    web100::CreateConnection(socket.get());
#endif

    double loss_rate = RunCBR(socket.get(), config.cbr_kb_s);
    if (loss_rate > config.loss_threshold) {
      std::cout << "FAIL\n";
      socket->Send("FAIL");
    } else {
      std::cout << "PASS\n";
      socket->Send("PASS");
    }

#ifdef USE_WEB100
    web100::Shutdown();
#endif  // USE_WEB100
  }

#ifdef USE_PCAP
    pcap::Shutdown();
#endif  // USE_PCAP

  return 0;
}
