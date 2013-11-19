#include "server/cbr.h"

#if defined(OS_FREEBSD)
#include <netinet/in.h>
#include <sys/types.h>
#endif

#include <assert.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <string.h>

#include <iostream>

#include "common/config.h"
#include "common/constants.h"
#include "common/scoped_ptr.h"
#include "common/time.h"
#include "gflags/gflags.h"
#include "mlab/socket.h"
#include "mlab/accepted_socket.h"
#ifdef USE_WEB100
#include "server/web100.h"
#endif

DECLARE_bool(verbose);

namespace mbm {

Result RunCBR(const mlab::AcceptedSocket* test_socket,
              const mlab::AcceptedSocket* ctrl_socket,
              const Config& config) {
#ifdef USE_WEB100
  if (test_socket->type() == SOCKETTYPE_TCP)
    web100::CreateConnection(test_socket);
#endif

  std::cout.setf(std::ios_base::fixed);
  std::cout << "Running CBR at " << config.cbr_kb_s << " kb/s\n";

  int tcp_mss = TCP_MSS;
  socklen_t mss_len = sizeof(tcp_mss);

  switch (test_socket->type()) {
    case SOCKETTYPE_TCP:
      if (getsockopt(test_socket->raw(), IPPROTO_TCP, TCP_MAXSEG,
                     &tcp_mss, &mss_len) != 0) {
        if (errno != ENOPROTOOPT) {
          std::cerr << "Failed to get TCP_MAXSEG: " << strerror(errno) << "\n";
          return RESULT_ERROR;
        }
        std::cout << "Socket does not support TCP_MAXSEG opt. "
                     "Using default MSS: " << TCP_MSS << ".\n";
      }
      break;

    case SOCKETTYPE_UDP:
      tcp_mss = TCP_MSS;
      break;

    default:
      std::cerr << "Unknown socket_type: " << test_socket->type() << "\n";
      return RESULT_ERROR;
  }

  if (config.cbr_kb_s == 0) {
    std::cerr << "Rate should be > 0\n";
    return RESULT_ERROR;
  }

  // we get rate as kilobits per second, turn that into bytes per second
  uint32_t bytes_per_sec = ((config.cbr_kb_s * 1000) / 8);

  // we're going to meter the bytes into the socket interface in units of
  // tcp_mss
  uint32_t bytes_per_chunk = tcp_mss;
  ctrl_socket->SendOrDie(mlab::Packet(htonl(bytes_per_chunk)));

  // calculate how many chunks per second we want to send
  uint32_t chunks_per_sec = bytes_per_sec / bytes_per_chunk;

  // calculate how many ns per chunk
  uint32_t time_per_chunk_ns = NS_PER_SEC / chunks_per_sec;

  std::cout << "  tcp_mss: " << tcp_mss << "\n";
  std::cout << "  bytes_per_sec: " << bytes_per_sec << "\n";
  std::cout << "  bytes_per_chunk: " << bytes_per_chunk << "\n";
  std::cout << "  chunks_per_sec: " << chunks_per_sec << "\n";
  std::cout << "  time_per_chunk_ns: " << time_per_chunk_ns << "\n";

  uint32_t wire_bytes_total = bytes_per_chunk * TOTAL_PACKETS_TO_SEND;
  std::cout << "  sending " << wire_bytes_total << " bytes\n";
  std::cout << "  should take " << (1.0 * wire_bytes_total / bytes_per_sec)
            << " seconds\n";

  char* chunk_buffer = new char[bytes_per_chunk];
  memset(chunk_buffer, 'x', bytes_per_chunk);

  // set the send buffer low
  //test_socket->SetSendBufferSize(bytes_per_chunk * 10);

  // show the send buffer
  std::cout << "  so_sndbuf: " << test_socket->GetSendBufferSize() << "\n"
            << std::flush;

#ifdef USE_WEB100
  if (test_socket->type() == SOCKETTYPE_TCP)
    web100::Start();
#endif
  uint32_t packets_sent = 0;
  uint32_t bytes_sent = 0;
  uint32_t sleep_count = 0;
  uint64_t outer_start_time = GetTimeNS();
  uint32_t last_percent = 0;
  while (packets_sent < TOTAL_PACKETS_TO_SEND) {
    // Set the first 32-bits to the sequence number.
    uint32_t seq_no = htonl(packets_sent);
    memcpy(chunk_buffer, &seq_no, sizeof(packets_sent));
    mlab::Packet chunk_packet(chunk_buffer, bytes_per_chunk);

    test_socket->SendOrDie(chunk_packet);
    bytes_sent += chunk_packet.length();
    ++packets_sent;

    if (FLAGS_verbose) {
      std::cout << "  s: " << std::hex << ntohl(seq_no) << " " << std::dec
                << ntohl(seq_no) << "\n";
      uint32_t percent = static_cast<uint32_t>(
          static_cast<float>(100 * packets_sent) / TOTAL_PACKETS_TO_SEND);
      if (percent > last_percent) {
        last_percent = percent;
        std::cout << "\r" << percent << "%" << std::flush;
      }
    }

    // figure out the start time for the next chunk
    uint64_t curr_time = GetTimeNS();
    uint64_t next_start = outer_start_time + (packets_sent * time_per_chunk_ns);

    // If we have time left over, sleep the remainder.
    int32_t left_over_ns = next_start - curr_time;
    if (left_over_ns > 0) {
      // std::cout << "." << std::flush;
      struct timespec sleep_req = {left_over_ns / NS_PER_SEC,
                                   left_over_ns % NS_PER_SEC};
      struct timespec sleep_rem;
      int slept = nanosleep(&sleep_req, &sleep_rem);
      ++sleep_count;
      while (slept == -1) {
        assert(errno == EINTR);
        slept = nanosleep(&sleep_rem, &sleep_rem);
        ++sleep_count;
      }
    } else {
      // Warning: start time of next chunk has already passed, no sleep
      // TODO(dominic): Should this be an error or inconclusive return state?
      // std::cout << "o" << std::flush;
    }
  }
  uint64_t outer_end_time = GetTimeNS();
  uint64_t delta_time = outer_end_time - outer_start_time;

  double delta_time_sec = static_cast<double>(delta_time) / NS_PER_SEC;

  std::cout << "\nbytes sent: " << bytes_sent << "\n";
  std::cout << "time: " << delta_time_sec << "\n";
  std::cout << "send rate: " << (bytes_sent*8) / delta_time_sec << " b/sec\n";

  uint32_t lost_packets = 0;
  uint32_t application_write_queue = 0;
  uint32_t retransmit_queue = 0;
  double rtt_sec = 0.0;

#ifdef USE_WEB100
  if (test_socket->type() == SOCKETTYPE_TCP) {
    web100::Stop();
    lost_packets = web100::PacketRetransCount();
    application_write_queue = web100::ApplicationWriteQueueSize();
    retransmit_queue = web100::RetransmitQueueSize();
    rtt_sec = static_cast<double>(web100::SampleRTT()) / MS_PER_SEC;
  }
#endif

  // TODO(dominic): Issue #7: if we're running UDP, get the sequence numbers
  // back over control channel to see which were lost/retransmitted.
  // TODO(dominic): Issue #9: get the receive rate back over control channel to
  // see what rate was actually achieved over the wire.

  std::cout << "  lost: " << lost_packets << "\n";
  std::cout << "  write queue: " << application_write_queue << "\n";
  std::cout << "  retransmit queue: " << retransmit_queue << "\n";
  std::cout << "  rtt: " << rtt_sec << "\n";
  std::cout << "  sent: " << packets_sent << "\n";
  std::cout << "  slept: " << sleep_count << "\n";

  if (rtt_sec > 0.0) {
    if ((application_write_queue + retransmit_queue) / rtt_sec <
        bytes_per_sec) {
      std::cout << "  kept up\n";
    } else {
      std::cout << "  failed to keep up\n";
    }
  }

  double loss_ratio = static_cast<double>(lost_packets) / packets_sent;
  if (loss_ratio > 1.0)
    return RESULT_INCONCLUSIVE;

  if (loss_ratio > config.loss_threshold)
    return RESULT_FAIL;

  return RESULT_PASS;
}

}  // namespace mbm
