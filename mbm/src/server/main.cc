#include <iostream>
#include <map>
#include <string>
#include <sstream>

#include <assert.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "common/config.h"
#include "common/constants.h"
#include "common/scoped_ptr.h"
#include "mlab/mlab.h"
#include "mlab/server_socket.h"

#define PACKETS_PER_CHUNK 3
#define TOTAL_PACKETS_TO_SEND 100

void GetTCPInfo(const mlab::Socket* socket, struct tcp_info* tcp_info) {
  socklen_t tcp_info_len = sizeof(tcp_info);
  assert(getsockopt(socket->raw(), IPPROTO_TCP, TCP_INFO,
                    tcp_info, &tcp_info_len) != -1);
  (void) tcp_info_len;
}

uint32_t get_time_ns() {
  struct timespec time;
  clock_gettime(CLOCK_MONOTONIC_RAW, &time);
  return time.tv_sec * 1000000000 + time.tv_nsec;
}

double RunCBR(const mlab::Socket* socket, uint32_t cbr_kb_s) {
  std::cout << "Running CBR at " << cbr_kb_s << "\n";

  struct tcp_info tcp_info;
  GetTCPInfo(socket, &tcp_info);

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

  GetTCPInfo(socket, &tcp_info);
  std::cout << "  retrans: " << tcp_info.tcpi_retrans << "\n";
  std::cout << "  lost: " << tcp_info.tcpi_lost << "\n";
  std::cout << "  retransmits: " << (uint32_t) tcp_info.tcpi_retransmits << "\n";
  std::cout << "  total_retrans: " << tcp_info.tcpi_total_retrans << "\n";

  return (double) tcp_info.tcpi_retransmits / packets_sent;
}

int main(int argc, const char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <port>\n";
    return 1;
  }

  mlab::Initialize("mbm_server", MBM_VERSION);
  mlab::SetLogSeverity(mlab::VERBOSE);

  scoped_ptr<mlab::ServerSocket> socket(
      mlab::ServerSocket::CreateOrDie(atoi(argv[1])));
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
  double loss_threshold = 0.0;

  double loss_rate = RunCBR(socket.get(), config.low_cbr_kb_s);
  if (loss_rate > loss_threshold) {
    std::cerr << "CBR of " << config.low_cbr_kb_s << " is already too lossy " <<
                 "(" << loss_rate * 100 << "%)\n";
    std::cerr << "Please provide a lower range to try.\n";
    socket->Send(END_OF_LINE);
    return 1;
  }

  loss_rate = RunCBR(socket.get(), config.high_cbr_kb_s);
  if (loss_rate <= loss_threshold) {
    std::cerr << "CBR of " << config.high_cbr_kb_s << " is not lossy " <<
                 "(" << loss_rate * 100 << "%)\n";
    std::cerr << "Please provide a higher range to try.\n";
    socket->Send(END_OF_LINE);
    return 1;
  }

  // Binary search between the low and high CBR rates to determine where we
  // cross the loss threshold.
  uint32_t low = config.low_cbr_kb_s;
  uint32_t high = config.high_cbr_kb_s;
  std::map<uint32_t, double> cbr_loss_map;
  while (low < high) {
    uint32_t test_cbr = low + (high - low) / 2;
    loss_rate = RunCBR(socket.get(), test_cbr);
    cbr_loss_map.insert(std::make_pair(test_cbr, loss_rate));
    if (loss_rate < loss_threshold)
      low = test_cbr;
    else if (loss_rate > loss_threshold)
      high = test_cbr;
    else
      break;
  }

  socket->Send(END_OF_LINE);

  // Report the results.
  std::cout << "CBR, loss_rate\n";
  for (std::map<uint32_t, double>::const_iterator it = cbr_loss_map.begin();
       it != cbr_loss_map.end(); ++it) {
    std::cout << it->first << ", " << it->second * 100 << "%%\n";
  }
  return 0;
}
