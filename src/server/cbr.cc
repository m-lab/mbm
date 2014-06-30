#include "server/cbr.h"

#if defined(OS_FREEBSD)
#include <netinet/in.h>
#include <sys/types.h>
#endif

#include <assert.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <string.h>
#include <math.h>

#include <iostream>

#include "common/config.h"
#include "common/constants.h"
#include "common/scoped_ptr.h"
#include "common/time.h"
#include "common/traffic_data.h"
#include "server/traffic_generator.h"
#include "gflags/gflags.h"
#include "mlab/socket.h"
#include "mlab/accepted_socket.h"
#include "server/model.h"
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
  std::cout.precision(3);
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

  uint32_t wire_bytes_total = bytes_per_chunk * MAX_PACKETS_TO_SEND;
  std::cout << "  sending at most " << MAX_PACKETS_TO_SEND << " packets" << std::endl;
  std::cout << "  sending at most " << wire_bytes_total << " bytes\n";
  std::cout << "  should take " << (1.0 * wire_bytes_total / bytes_per_sec)
            << " seconds\n";

  // show the send buffer
  std::cout << "  so_sndbuf: " << test_socket->GetSendBufferSize() << "\n"
            << std::flush;

#ifdef USE_WEB100
  double p0 = 0.0;
  double p1 = 0.0;
  double k = 0.0;
  double s = 0.0;
  double alpha = 0.0;
  double beta = 0.0;
  double h1 = 0.0;
  double h2 = 0.0;
  if (test_socket->type() == SOCKETTYPE_TCP) {
    web100::Start();
    // calculate the parameters used in sequential probability ratio test
    uint64_t target_run_length = model::target_run_length(config.cbr_kb_s,
                                                     config.rtt_ms,
                                                     config.mss_bytes);
    p0 = 1.0 / target_run_length;
    p1 = 1.0 / (target_run_length / 4.0);
    k = log(p1 * (1 - p0) / (p0 * (1 - p1)));
    s = log((1-p0) / (1-p1)) / k;
    alpha = 0.05; // type I error
    beta = 0.05; // type II error
    h1 = log((1-alpha) / beta) / k;
    h2 = log((1-beta) / alpha) / k;
  }
#endif
  uint32_t sleep_count = 0;
  uint64_t outer_start_time = GetTimeNS();

  Result test_result = RESULT_INCONCLUSIVE;
  TrafficGenerator generator(test_socket, bytes_per_chunk);

  while (generator.packets_sent() < MAX_PACKETS_TO_SEND) {
    generator.send(1);

    #ifdef USE_WEB100
      if (test_socket->type() == SOCKETTYPE_TCP) {
        web100::Stop();
        // the sequential probability ratio test
        double xa = -h1 + s * generator.packets_sent();
        double xb = h2 + s * generator.packets_sent();
        uint32_t packet_loss = web100::PacketRetransCount();
        if(packet_loss <= xa) {
          // PASS
          std::cout << "passed SPRT" << std::endl;
          test_result = RESULT_PASS;
          ctrl_socket->SendOrDie(mlab::Packet(END));
          break;
        } else if(packet_loss >= xb) {
          // FAIL
          std::cout << "failed SPRT" << std::endl;
          test_result = RESULT_FAIL;
          ctrl_socket->SendOrDie(mlab::Packet(END));
          break;
        }
      }
    #endif
    
    // figure out the start time for the next chunk
    uint64_t curr_time = GetTimeNS();
    uint64_t next_start = outer_start_time + (generator.packets_sent() * time_per_chunk_ns);

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
      // INCONCLUSIVE
      std::cout << "failed to generate traffic" << std::endl;
      break;
    }
  }

  uint64_t outer_end_time = GetTimeNS();
  uint64_t delta_time = outer_end_time - outer_start_time;

  double delta_time_sec = static_cast<double>(delta_time) / NS_PER_SEC;

  std::cout << "\nPackets sent: " << generator.packets_sent() << "\n";
  std::cout << "bytes sent: " << generator.total_bytes_sent() << "\n";
  std::cout << "time: " << delta_time_sec << "\n";

  double send_rate = (generator.total_bytes_sent() * 8) / delta_time_sec;
  double rate_delta_percent = (send_rate * 100) / (bytes_per_sec * 8);
  std::cout << "send rate: " << send_rate << " b/sec ("
            << rate_delta_percent << "% of target)\n";

  double receive_rate =
    ctrl_socket->ReceiveOrDie(sizeof(receive_rate)).as<double>();
  std::cout << "receive rate: " << receive_rate << " b/sec\n";

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

  // Receive the data collected by the client
  uint32_t data_size_obj = 
    ntohl(ctrl_socket->ReceiveOrDie(sizeof(data_size_obj)).as<uint32_t>());
  uint32_t data_size_bytes = data_size_obj * sizeof(TrafficData);

  mlab::Packet recv_pkt = ctrl_socket->ReceiveOrDie(data_size_bytes);
  const TrafficData* recv_buffer =
    reinterpret_cast<const TrafficData*>(recv_pkt.buffer());

  std::vector<TrafficData> client_data(data_size_obj);
  for(uint32_t i=0; i<data_size_obj; ++i){
    client_data[i] = (TrafficData::ntoh(recv_buffer[i]));
  }

  std::cout << "  lost: " << lost_packets << "\n";
  std::cout << "  write queue: " << application_write_queue << "\n";
  std::cout << "  retransmit queue: " << retransmit_queue << "\n";
  std::cout << "  rtt: " << rtt_sec << "\n";
  std::cout << "  sent: " << generator.packets_sent() << "\n";
  std::cout << "  slept: " << sleep_count << "\n";


  if (rtt_sec > 0.0) {
    if ((application_write_queue + retransmit_queue) / rtt_sec <
        bytes_per_sec) {
      std::cout << "  kept up\n";
    } else {
      std::cout << "  failed to keep up\n";
    }
  }

  return test_result;
  // double loss_ratio = static_cast<double>(lost_packets) / generator.packets_sent();
  // if (loss_ratio > 1.0)
    // return RESULT_INCONCLUSIVE;

  // if (loss_ratio > config.loss_threshold)
    // return RESULT_FAIL;

  // return RESULT_PASS;
}

}  // namespace mbm
