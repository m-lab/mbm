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
#include "server/stat_test.h"
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

  std::cout.setf(std::ios_base::fixed);
  std::cout.precision(3);
  std::cout << "Running CBR at " << config.cbr_kb_s << " kb/s\n";


  uint32_t tcp_mss = config.mss_bytes;
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
                  << "Using default " << TCP_MSS << std::endl;
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
  uint32_t bytes_per_chunk = std::min(tcp_mss, config.mss_bytes); 
  ctrl_socket->SendOrDie(mlab::Packet(htonl(bytes_per_chunk)));

  // calculate how many chunks per second we want to send
  uint32_t chunks_per_sec = bytes_per_sec / bytes_per_chunk;

  // calculate how many ns per chunk
  uint32_t time_per_chunk_ns = NS_PER_SEC / chunks_per_sec;

  // calculate how many sec per chunk
  double time_per_chunk_sec = 1.0 / chunks_per_sec;

  // traffic pattern log
  std::cout << "  tcp_mss: " << tcp_mss << "\n";
  std::cout << "  bytes_per_sec: " << bytes_per_sec << "\n";
  std::cout << "  bytes_per_chunk: " << bytes_per_chunk << "\n";
  std::cout << "  chunks_per_sec: " << chunks_per_sec << "\n";
  std::cout << "  time_per_chunk_ns: " << time_per_chunk_ns << "\n";
  std::cout << "  time_per_chunk_sec: " << time_per_chunk_sec << "\n";

  uint32_t wire_bytes_total = bytes_per_chunk * MAX_PACKETS_TO_SEND;
  std::cout << "  sending at most " << MAX_PACKETS_TO_SEND << " packets" << std::endl;
  std::cout << "  sending at most " << wire_bytes_total << " bytes\n";
  std::cout << "  should take at most " << (1.0 * wire_bytes_total / bytes_per_sec)
            << " seconds\n";



  // Model Computation
  uint64_t target_pipe_size = model::target_pipe_size(config.cbr_kb_s,
                                                      config.rtt_ms,
                                                      config.mss_bytes);
  uint64_t target_run_length = model::target_run_length(config.cbr_kb_s,
                                                        config.rtt_ms,
                                                        config.mss_bytes);
  uint64_t target_pipe_size_bytes = target_pipe_size * config.mss_bytes;



  #ifdef USE_WEB100
  TrafficGenerator growth_generator(test_socket, bytes_per_chunk);
  web100::Connection growth_connection(test_socket);
  growth_connection.Start();
  //uint32_t dump_size = static_cast<uint32_t>(3 * target_pipe_size);
  //ctrl_socket->SendOrDie(mlab::Packet(htonl(dump_size)));
  std::cout << "start growing phase" << std::endl;
  while (growth_generator.packets_sent() < MAX_PACKETS_TO_SEND) {
    growth_connection.Stop();
    if (growth_connection.CurCwnd() >= target_pipe_size_bytes) {
      std::cout << "cwnd reached" << std::endl;
      break;
    }
    growth_generator.send(target_pipe_size);
    struct timespec sleep_req = { config.rtt_ms * 1000 * 1000 / NS_PER_SEC,
                                  config.rtt_ms * 1000 * 1000 % NS_PER_SEC };
    struct timespec sleep_rem;
    nanosleep(&sleep_req, &sleep_rem);
  }
  std::cout << "growing phase done" << std::endl;

  while (growth_connection.SndNxt() - growth_connection.SndUna()
          >= target_pipe_size_bytes / 2) {
  }
  std::cout << "done spinning" << std::endl;

  {
    struct timespec sleep_req = {0, NS_PER_SEC / 10 };
    struct timespec sleep_rem;
    nanosleep(&sleep_req, &sleep_rem);
  }
  // notify the client that the init phase has ended
  ctrl_socket->SendOrDie(mlab::Packet(END));
  #endif



  // Start the test
  StatTest tester(target_run_length);
  TrafficGenerator generator(test_socket, bytes_per_chunk);
  #ifdef USE_WEB100
  web100::Connection test_connection(test_socket);
  test_connection.Start();
  #endif

  uint32_t sleep_count = 0;
  Result test_result = RESULT_INCONCLUSIVE;
  uint64_t outer_start_time = GetTimeNS();

  while (generator.packets_sent() < MAX_PACKETS_TO_SEND) {
    generator.send(1);

    #ifdef USE_WEB100
    if (test_socket->type() == SOCKETTYPE_TCP) {
      // sample the data once a second
      if (generator.packets_sent() % chunks_per_sec == 0) {
        // statistical test
        test_connection.Stop();
        uint32_t loss = test_connection.PacketRetransCount();
        uint32_t n = generator.packets_sent();

        test_result = tester.test_result(n, loss);
        if (test_result == RESULT_PASS) {
          std::cout << "passed SPRT" << std::endl;
          break;
        } else if (test_result == RESULT_FAIL) {
          std::cout << "failed SPRT" << std::endl;
          break;
        }
       }
    }
    #endif
    
    // figure out the start time for the next chunk
    uint64_t next_start = outer_start_time + (generator.packets_sent() * time_per_chunk_ns);
    uint64_t curr_time = GetTimeNS();

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
      std::cout << "failed by " << 1.0 * left_over_ns / NS_PER_SEC
                << "s to generate traffic" << std::endl;
      break;
    }
  }
  // notify the client that the test has ended
  {
    uint64_t rtt_ns = config.rtt_ms * 1000 * 1000;
    struct timespec sleep_req = { rtt_ns / NS_PER_SEC, rtt_ns % NS_PER_SEC };
    struct timespec sleep_rem;
    nanosleep(&sleep_req, &sleep_rem);
  }
  ctrl_socket->SendOrDie(mlab::Packet(END));

  uint64_t outer_end_time = GetTimeNS();
  uint64_t delta_time = outer_end_time - outer_start_time;
  double delta_time_sec = static_cast<double>(delta_time) / NS_PER_SEC;


  // Traffic statistics from web100
  uint32_t lost_packets = 0;
  uint32_t application_write_queue = 0;
  uint32_t retransmit_queue = 0;
  double rtt_sec = 0.0;

#ifdef USE_WEB100
  if (test_socket->type() == SOCKETTYPE_TCP) {
    test_connection.Stop();
    lost_packets = test_connection.PacketRetransCount();
    application_write_queue = test_connection.ApplicationWriteQueueSize();
    retransmit_queue = test_connection.RetransmitQueueSize();
    rtt_sec = static_cast<double>(test_connection.SampleRTT()) / MS_PER_SEC;
  }
#endif

  // Observed data rates
  double send_rate = (generator.total_bytes_sent() * 8) / delta_time_sec;
  double send_rate_delta_percent = (send_rate * 100) / (bytes_per_sec * 8);
  double recv_rate =
    ctrl_socket->ReceiveOrDie(sizeof(recv_rate)).as<double>();
  double recv_rate_delta_percent = (recv_rate * 100) / (bytes_per_sec * 8);


  // Receive the data collected by the client
  uint32_t data_size_obj = 
    ntohl(ctrl_socket->ReceiveOrDie(sizeof(data_size_obj)).as<uint32_t>());
  uint32_t data_size_bytes = data_size_obj * sizeof(TrafficData);
  std::cout << "client data: " << data_size_bytes << " bytes" << std::endl;

  std::vector<TrafficData> client_data(data_size_obj);
  std::vector<uint8_t> bytes_buffer;
  uint32_t total_recv_bytes = 0;
  while (total_recv_bytes < data_size_bytes) {
    mlab::Packet recv_pkt =
      ctrl_socket->ReceiveOrDie(data_size_bytes - total_recv_bytes);
    uint32_t recv_bytes = recv_pkt.length();
    bytes_buffer.insert(bytes_buffer.end(),
                        recv_pkt.buffer(),
                        recv_pkt.buffer() + recv_bytes);
    total_recv_bytes += recv_bytes;
  }
  const TrafficData* recv_buffer =
      reinterpret_cast<const TrafficData*>(&bytes_buffer[0]);
  for(uint32_t i=0; i < data_size_obj; ++i){
    client_data[i] = TrafficData::ntoh(recv_buffer[i]);
  }


  std::cout << "\nPackets sent: " << generator.packets_sent() << "\n";
  std::cout << "bytes sent: " << generator.total_bytes_sent() << "\n";
  std::cout << "time: " << delta_time_sec << "\n";
  std::cout << "send rate: " << send_rate << " b/sec ("
            << send_rate_delta_percent << "% of target)\n";
  std::cout << "recv rate: " << recv_rate << " b/sec ("
            << recv_rate_delta_percent << "% of target)\n";


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

  std::cout << "Done CBR" << std::endl;
  return test_result;
}

}  // namespace mbm
