#include "server/cbr.h"

#if defined(OS_FREEBSD)
#include <netinet/in.h>
#include <sys/types.h>
#endif

#include <assert.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <iostream>

#include "common/scoped_ptr.h"
#include "common/config.h"
#include "common/constants.h"
#ifdef USE_WEB100
#include "common/web100.h"
#endif
#include "mlab/socket.h"
#include "mlab/accepted_socket.h"

// TODO: configuration
#define PACKETS_PER_CHUNK 3
#define TOTAL_PACKETS_TO_SEND 3000
#define NS_PER_SEC 1000000000

namespace mbm {
namespace {
uint32_t lost_packets = 0;

uint64_t get_time_ns() {
  struct timespec time;
#if defined(OS_FREEBSD)
  clock_gettime(CLOCK_MONOTONIC_PRECISE, &time);
#else
  clock_gettime(CLOCK_MONOTONIC_RAW, &time);
#endif
  return time.tv_sec * NS_PER_SEC + time.tv_nsec;
}

}  // namespace

Result RunCBR(const mlab::Socket* socket, const Config& config) {
  std::cout << "Running CBR at " << config.cbr_kb_s << " kb/s\n";

  const mlab::AcceptedSocket* accepted_socket = 
    reinterpret_cast<const mlab::AcceptedSocket*>(socket);

  lost_packets = 0;
  
  int socket_type = 0;
  socklen_t socket_type_len = sizeof(socket_type);
  if (getsockopt(accepted_socket->client_raw(),
		 SOL_SOCKET,
		 SO_TYPE,
		 &socket_type,
		 &socket_type_len)) {
    if (errno == ENOPROTOOPT) {
      std::cout << "Socket does not support SO_TYPE opt.\n";
    } else {
      std::cerr << "Failed to get SO_TYPE: " << strerror(errno) << "\n";
      return RESULT_ERROR;
    }
  }

  int tcp_mss = 0;
  socklen_t mss_len = sizeof(tcp_mss);

  if (socket_type == SOCK_STREAM) {
    if (getsockopt(accepted_socket->client_raw(), 
		   IPPROTO_TCP, 
		   TCP_MAXSEG, 
		   &tcp_mss, 
		   &mss_len)) {
      if (errno == ENOPROTOOPT) {
	std::cout << "Socket does not support TCP_MAXSEG opt. "
		  << "Using default MSS.\n";
      } else {
	std::cerr << "Failed to get TCP_MAXSEG: " << strerror(errno) << "\n";
	return RESULT_ERROR;
      }
    }
  } else if (socket_type == SOCK_DGRAM) {
    tcp_mss = TCP_MSS;
  } else {
    std::cerr << "Don't understand this socket_type: " << socket_type << "\n>";
    return RESULT_ERROR;
  }

  // we get rate as kilobits per second, turn that into bytes per second
  uint32_t bytes_per_sec = ((config.cbr_kb_s * 1000) / 8);

  // we're going to meter the bytes into the socket interface in units of 
  // tcp_mss
  uint32_t bytes_per_chunk = tcp_mss;

  // calculate how many chunks per second we want to send
  uint32_t chunks_per_sec = bytes_per_sec / bytes_per_chunk;

  // calculate how many ns per chunk
  uint32_t time_per_chunk_ns = NS_PER_SEC / chunks_per_sec;

  std::cout << "  tcp_mss: " << tcp_mss << "\n";
  std::cout << "  bytes_per_sec: " << bytes_per_sec << "\n";
  std::cout << "  bytes_per_chunk: " << bytes_per_chunk << "\n";
  std::cout << "  chunks_per_sec: " << chunks_per_sec << "\n";
  std::cout << "  time_per_chunk_ns: " << time_per_chunk_ns << "\n";

  // TODO(dominic): Tell the client the |bytes_per_chunk| so they know how much
  // to ask for on every tick.
  // DONE(sstuart) - sending total number of bytes that will be sent
  uint32_t wire_bytes_total = htonl(bytes_per_chunk * TOTAL_PACKETS_TO_SEND);
  socket->SendOrDie(mlab::Packet((const char *) &wire_bytes_total,
				 sizeof(wire_bytes_total)));
  std::cout << "  sending " << ntohl(wire_bytes_total) << " bytes\n";
  std::cout << "  should take " << (1.0 * ntohl(wire_bytes_total) / bytes_per_sec) << " seconds\n";

  char chunk_data[bytes_per_chunk];
  memset(chunk_data, 'x', bytes_per_chunk);
  mlab::Packet chunk_packet(chunk_data, bytes_per_chunk);

  // set the send buffer low
  int optval =  bytes_per_chunk * 10;
  if (setsockopt(accepted_socket->client_raw(), 
		 SOL_SOCKET, 
		 SO_SNDBUF, 
		 &optval, 
		 sizeof(optval))) {
    if (errno == ENOPROTOOPT) {
      std::cout << "Socket does not support SO_SNDBUF opt.\n";
    } else {
      std::cerr << "Failed to set SO_SNDBUF: " << strerror(errno) << "\n";
      return RESULT_ERROR;
    }
  }

  // show the send buffer
  int so_sndbuf = 0;
  socklen_t so_sndbuf_len = sizeof(so_sndbuf);
  if (getsockopt(accepted_socket->client_raw(), 
		 SOL_SOCKET, 
		 SO_SNDBUF, 
		 &so_sndbuf, 
		 &so_sndbuf_len)) {
    if (errno == ENOPROTOOPT) {
      std::cout << "Socket does not support SO_SNDBUF opt.\n";
    } else {
      std::cerr << "Failed to set SO_SNDBUF: " << strerror(errno) << "\n";
      return RESULT_ERROR;
    }
  } else {
    std::cout << "  so_sndbuf: " << so_sndbuf << "\n" << std::flush;
  }

#ifdef USE_WEB100
  web100::Start();
#endif
  uint32_t packets_sent = 0;
  uint32_t bytes_sent = 0;
  uint32_t sleep_count = 0;
  uint64_t outer_start_time = get_time_ns();
  while (packets_sent < TOTAL_PACKETS_TO_SEND) {
    // std::cout << '.' << std::flush;
    // uint64_t start_time = get_time_ns();

    // Embed sequence number.
    // TODO: Should we do three sends, one per packet?
    // TODO: if we're running UDP, get the sequence numbers back over TCP after
    // test to see which were lost.
    //sprintf(chunk_packet.buffer(), "%u:", packets_sent);

    socket->SendOrDie(chunk_packet);
    bytes_sent += chunk_packet.length();
    packets_sent++;

    // figure out the start time for the next chunk
    //
    uint64_t curr_time = get_time_ns();
    uint64_t next_start = outer_start_time + 
      (packets_sent * time_per_chunk_ns);

    // If we have time left over, sleep the remainder.
    int32_t left_over_ns = next_start - curr_time;

    if (left_over_ns > 0) {
      // std::cout << "." << std::flush;

      struct timespec sleep_req = {left_over_ns / NS_PER_SEC,
                                   left_over_ns % NS_PER_SEC};
      struct timespec sleep_rem;
      int slept = nanosleep(&sleep_req, &sleep_rem);
      sleep_count++;
      while (slept == -1) {
        assert(errno == EINTR);
        slept = nanosleep(&sleep_rem, &sleep_rem);
	sleep_count++;
      }
    } else {
      // Warning: start time of next chunk has already passed, no sleep
      // TODO(dominic): Should this be an error or inconclusive return state?
      // std::cout << "o" << std::flush;
    }
  }
  uint64_t outer_end_time = get_time_ns();
  uint32_t delta_time = outer_end_time - outer_start_time;
    
  socket->SendOrDie(mlab::Packet(END_OF_LINE, strlen(END_OF_LINE)));

  std::cout << "\nsent " << bytes_sent ;
  std::cout << "\ntime " << 1.0 * delta_time / NS_PER_SEC << "\n" 
	    << std::flush;

#ifdef USE_WEB100
  web100::Stop();
  lost_packets = web100::GetLossCount();
#endif

  std::cout << "  lost: " << lost_packets << "\n";
  std::cout << "  sent: " << packets_sent << "\n";
  std::cout << "  slept: " << sleep_count << "\n";

#if defined(OS_FREEBSD)
  struct tcp_info tcpinfo;
  socklen_t tcpinfo_len = sizeof(tcpinfo);

  if (getsockopt(accepted_socket->client_raw(), 
		 IPPROTO_TCP, 
		 TCP_INFO, 
		 &tcpinfo, 
		 &tcpinfo_len)) {
    if (errno == ENOPROTOOPT) {
      std::cout << "Socket does not support TCP_INFO opt.\n";
    } else {
      std::cerr << "Failed to get TCP_INFO: " << strerror(errno) << "\n";
      return RESULT_ERROR;
    }
  }
  std::cout << "  snd_mss: " << tcpinfo.tcpi_snd_mss << "\n";
  std::cout << "  rcv_mss: " << tcpinfo.tcpi_rcv_mss << "\n";
  std::cout << "  rtt: " << tcpinfo.tcpi_rtt << "\n";
  std::cout << "  rexmit: " << tcpinfo.tcpi_snd_rexmitpack << "\n";
  std::cout << "  ooopack: " << tcpinfo.tcpi_rcv_ooopack << "\n";

#endif

  double loss_ratio = static_cast<double>(lost_packets) / packets_sent;
  if (loss_ratio > 1.0)
    return RESULT_INCONCLUSIVE;

  if (loss_ratio > config.loss_threshold)
    return RESULT_FAIL;

  return RESULT_PASS;
}

}  // namespace mbm
