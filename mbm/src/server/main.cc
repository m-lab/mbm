#include <iostream>
#include <map>
#include <string>
#include <sstream>

#include <errno.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common/config.h"
#include "common/constants.h"
#include "common/scoped_ptr.h"
#include "mlab/server_socket.h"

#define TIME_PER_CHUNK_MSECS 200

double RunCBR(const mlab::Socket* socket, uint32_t cbr_kb_s, uint32_t secs) {
  std::cout << "Running CBR at " << cbr_kb_s << " for " << secs <<
               " seconds\n";

  uint32_t total_num_bytes = cbr_kb_s * 1024 * secs / 8;
  uint32_t num_chunks = 1000 * secs / TIME_PER_CHUNK_MSECS;
  uint32_t bytes_per_chunk = total_num_bytes / num_chunks;

  std::string chunk_data(bytes_per_chunk, 'b');
  
  for (uint32_t i = 0; i < num_chunks; ++i) {
    for (uint32_t i = 0; i < bytes_per_chunk; ++i)
      chunk_data[i] = static_cast<char>(rand() % 255);
    socket->Send(chunk_data);

    // TODO(dominic): Take into account the time taken for the above.
    usleep(TIME_PER_CHUNK_MSECS * 1000);
  }

  struct tcp_info tcp_info;
  socklen_t tcp_info_len = sizeof(tcp_info);
  if (getsockopt(socket->raw(), IPPROTO_TCP, TCP_INFO, &tcp_info,
          &tcp_info_len) == -1) {
    std::cerr << "Failed to get tcp_info: " << strerror(errno) <<
                 "[" << errno << "]\n";
  } else {
    std::cout << "  retransmits: " << tcp_info.tcpi_total_retrans << "\n";
  }

  return (double) tcp_info.tcpi_total_retrans / total_num_bytes;
}

int main(int argc, const char* argv[]) {
  scoped_ptr<mlab::ServerSocket> socket(mlab::ServerSocket::CreateOrDie(4242));
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

  if (RunCBR(socket.get(), config.low_cbr_kb_s, config.time_per_test) >
      loss_threshold) {
    std::cerr << "CBR of " << config.low_cbr_kb_s << " is already too lossy.\n";
    std::cerr << "Please provide a lower range to try.\n";
    socket->Send(END_OF_LINE);
    return 1;
  }

  if (RunCBR(socket.get(), config.high_cbr_kb_s, config.time_per_test) <=
      loss_threshold) {
    std::cerr << "CBR of " << config.high_cbr_kb_s << " is not lossy.\n";
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
    double loss_rate = RunCBR(socket.get(), test_cbr, config.time_per_test);
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
    std::cout << it->first << ", " << it->second << "\n";
  }
  return 0;
}
