#include <iostream>
#include <map>
#include <string>
#include <sstream>

#include <assert.h>
#include <errno.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "common/config.h"
#include "common/constants.h"
#include "common/pcap.h"
#include "common/scoped_ptr.h"
#include "mlab/mlab.h"
#include "mlab/server_socket.h"

#define PACKETS_PER_CHUNK 3
#define TOTAL_PACKETS_TO_SEND 100

namespace mbm {

std::set<uint32_t> sequence_nos;
uint32_t lost_packets = 0;

uint32_t get_time_ns() {
  struct timespec time;
  clock_gettime(CLOCK_MONOTONIC_RAW, &time);
  return time.tv_sec * 1000000000 + time.tv_nsec;
}

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
  //std::cout << "[PCAP] " << tcp_header->seq << "\n";
}

double RunCBR(const mlab::Socket* socket, uint32_t cbr_kb_s) {
  std::cout << "Running CBR at " << cbr_kb_s << "\n";

  sequence_nos.clear();
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

  uint32_t packets_sent = 0;
  while(packets_sent < TOTAL_PACKETS_TO_SEND) {
  //  std::cout << '.' << std::flush;
    uint32_t start_time = get_time_ns();

    // Rerandomize the chunk data just in case someone is trying to be too
    // clever.
    for (uint32_t i = 0; i < chunk_data.size(); ++i)
      chunk_data[i] = static_cast<char>(rand() % 255);

    // And send
    socket->Send(chunk_data);
    packets_sent += PACKETS_PER_CHUNK;

    // TODO(dominic): How can we capture but retain high CBR?
    pcap::Capture(PACKETS_PER_CHUNK, pcap_callback);

    std::cout << "." << std::flush;

    // If we have time left over, sleep the remainder.
    uint32_t end_time = get_time_ns();
    uint32_t time_taken_to_send_ns = end_time - start_time;
    int32_t left_over_ns = time_per_chunk_ns - time_taken_to_send_ns;

    if (left_over_ns > 0) {
      struct timespec sleep_req = {left_over_ns / 1000000000,
                                   left_over_ns % 1000000000};
      struct timespec sleep_rem;
      int slept = nanosleep(&sleep_req, &sleep_rem);
      while (slept == -1) {
        assert(errno == EINTR);
        slept = nanosleep(&sleep_rem, &sleep_rem);
      }
    } else {
      std::cout << "Warning: Took longer to send than we budgeted: " <<
                   time_per_chunk_ns << " ns vs " << time_taken_to_send_ns <<
                   " ns\n";
    }
  }
  std::cout << "\n";

  std::cout << "  lost: " << lost_packets << "\n";
  std::cout << "  sent: " << packets_sent << "\n";

  return (double) lost_packets / packets_sent;
}

// Utility class to send EOL and shutdown pcap before exit.
class CleanShutdown {
 public:
  explicit CleanShutdown(const mlab::Socket* socket)
      : socket_(socket) {}
  ~CleanShutdown() {
    socket_->Send(END_OF_LINE);
    pcap::Shutdown();
  }

 private:
  scoped_ptr<const mlab::Socket> socket_;
};

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
  // TODO(dominic): Either findalldevs or do something to encourage this device
  // to be correct.
  pcap::Initialize(std::string("src localhost and src port ") + port, "lo");

  mlab::ServerSocket* socket = mlab::ServerSocket::CreateOrDie(atoi(port));
  CleanShutdown shutdown(socket);
  socket->Select();
  socket->Accept();

  // TODO(dominic): Do we need to get the range from the client or can this be a
  // server setting?
  Config config;
  config.FromString(socket->Receive(1024));

  std::cout << "Setting CBR range [" << config.low_cbr_kb_s << ", " <<
               config.high_cbr_kb_s << "] kb/s\n";

  // The |loss_threshold| allows us to find the CBR that gives loss of a certain
  // percentage.
  double loss_threshold = 1.0;

  double loss_rate = RunCBR(socket, config.low_cbr_kb_s);
  if (loss_rate > loss_threshold) {
    std::cerr << "CBR of " << config.low_cbr_kb_s << " is already too lossy " <<
                 "(" << loss_rate * 100 << "%)\n";
    std::cerr << "Please provide a lower range to try.\n";
    return 1;
  }

  loss_rate = RunCBR(socket, config.high_cbr_kb_s);
  if (loss_rate <= loss_threshold) {
    std::cerr << "CBR of " << config.high_cbr_kb_s << " is not lossy " <<
                 "(" << loss_rate * 100 << "%)\n";
    std::cerr << "Please provide a higher range to try.\n";
    return 1;
  }

  // Binary search between the low and high CBR rates to determine where we
  // cross the loss threshold.
  uint32_t low = config.low_cbr_kb_s;
  uint32_t high = config.high_cbr_kb_s;
  std::map<uint32_t, double> cbr_loss_map;
  while (low < high) {
    uint32_t test_cbr = low + (high - low) / 2;
    loss_rate = RunCBR(socket, test_cbr);
    cbr_loss_map.insert(std::make_pair(test_cbr, loss_rate));
    if (loss_rate < loss_threshold)
      low = test_cbr;
    else if (loss_rate > loss_threshold)
      high = test_cbr;
    else
      break;
  }

  // Report the results.
  std::cout << "CBR, loss_rate\n";
  for (std::map<uint32_t, double>::const_iterator it = cbr_loss_map.begin();
       it != cbr_loss_map.end(); ++it) {
    std::cout << it->first << ", " << it->second * 100 << "%%\n";
  }

  return 0;
}
