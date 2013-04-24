#include "server/cbr.h"

#include <assert.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>

#include <iostream>

#include "common/config.h"
#include "common/constants.h"
#include "common/web100.h"
#include "mlab/socket.h"

// TODO: configuration
#define PACKETS_PER_CHUNK 3
#define TOTAL_PACKETS_TO_SEND 500

namespace mbm {
namespace {
uint32_t lost_packets = 0;

uint32_t get_time_ns() {
  struct timespec time;
  clock_gettime(CLOCK_MONOTONIC_RAW, &time);
  return time.tv_sec * 1000000000 + time.tv_nsec;
}

}  // namespace

Result RunCBR(const mlab::Socket* socket, const Config& config) {
  std::cout << "Running CBR at " << config.cbr_kb_s << "\n";

  lost_packets = 0;

  uint32_t bytes_per_chunk = PACKETS_PER_CHUNK * TCP_MSS;
  uint32_t bytes_per_ms = config.cbr_kb_s * 1024 / (8 * 1000);
  uint32_t time_per_chunk_ns = (1000000 * bytes_per_chunk) / bytes_per_ms;

  std::cout << "  bytes_per_chunk: " << bytes_per_chunk << "\n";
  std::cout << "  bytes_per_ms: " << bytes_per_ms << "\n";
  std::cout << "  time_per_chunk_ns: " << time_per_chunk_ns << "\n";

  // TODO(dominic): Tell the client the |bytes_per_chunk| so they know how much
  // to ask for on every tick.
  char chunk_data[bytes_per_chunk];
  memset(chunk_data, 0, bytes_per_chunk);

#ifdef USE_WEB100
  web100::Start();
#endif
  uint32_t packets_sent = 0;
  while (packets_sent < TOTAL_PACKETS_TO_SEND) {
  //  std::cout << '.' << std::flush;
    uint32_t start_time = get_time_ns();

    // Embed sequence number.
    // TODO: Should we do three sends, one per packet?
    // TODO: if we're running UDP, get the sequence numbers back over TCP after
    // test to see which were lost.
    sprintf(chunk_data, "%u:", packets_sent);

    // And send
    socket->Send(chunk_data);
    packets_sent += PACKETS_PER_CHUNK;

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

  double loss_ratio = static_cast<double>(lost_packets) / packets_sent;
  if (loss_ratio > 1.0)
    return RESULT_INCONCLUSIVE;

  if (loss_ratio > config.loss_threshold)
    return RESULT_FAIL;

  return RESULT_PASS;
}

}  // namespace mbm
