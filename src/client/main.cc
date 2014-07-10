#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>

#include "common/config.h"
#include "common/constants.h"
#include "common/result.h"
#include "common/scoped_ptr.h"
#include "common/time.h"
#include "common/traffic_data.h"
#include "gflags/gflags.h"
#include "mlab/client_socket.h"
#include "mlab/mlab.h"

DEFINE_string(server, "localhost", "The server to connect to");
DEFINE_int32(port, 4242, "The port to connect to");
DEFINE_bool(sweep, false, "Perform a sweep in UDP to find the best rate for a "
                          "TCP test");
DEFINE_int32(rate, 600, "The rate to test. Ignored if --sweep is set.");
DEFINE_string(socket_type, "tcp", "The transport protocol to use. Ignored if "
                                  "--sweep is set.");

DEFINE_int32(mss, 1460, "The target maximum segment size in bytes");
DEFINE_int32(rtt, 200, "The target round trip time in miliseconds");

DEFINE_int32(minrate, 400, "The minimum rate to test when --sweep is active.");
DEFINE_int32(maxrate, 1200, "The maximum rate to test when --sweep is active.");
DEFINE_int32(ratestep, 100, "The step to take between rates when --sweep is "
                            "active.");
DEFINE_bool(verbose, false, "Verbose output");

namespace mbm {
namespace {
bool ValidatePort(const char* flagname, int32_t value) {
  if (value > 0 && value < 65536)
    return true;
  std::cerr << "Invalid value for --" << flagname << ": " << value << "\n";
  return false;
}

bool ValidateSocketType(const char* flagname, const std::string& value) {
  if (value == "udp" || value == "tcp")
    return true;
  std::cerr << "Invalid value for --" << flagname << ": " << value << "\n";
  return false;
}

const bool port_validator =
    gflags::RegisterFlagValidator(&FLAGS_port, &ValidatePort);
const bool socket_type_validator =
    gflags::RegisterFlagValidator(&FLAGS_socket_type, &ValidateSocketType);

Result Run(SocketType socket_type, int rate, int rtt, int mss) {
  std::cout.setf(std::ios_base::fixed);
  std::cout.precision(3);
  std::cout << "Running MBM test over "
            << (socket_type == SOCKETTYPE_TCP ? "tcp" : "udp")
            << " with target parameters:" << std::endl
            << "rate: " << rate << " kbps" << std::endl
            << "rtt: " << rtt << " ms" << std::endl
            << "mss: " << mss << " bytes" << std::endl;

  const mlab::Host server(FLAGS_server);
  scoped_ptr<mlab::ClientSocket> ctrl_socket(
      mlab::ClientSocket::CreateOrDie(server, FLAGS_port));

  std::cout << "Sending config\n";
  const Config config(socket_type, rate, rtt, mss, 0.0);
  ctrl_socket->SendOrDie(mlab::Packet(config));

  std::cout << "Getting port\n";
  uint16_t port =
      ntohs(ctrl_socket->ReceiveOrDie(sizeof(uint16_t)).as<uint16_t>());

  std::cout << "Connecting on port " << port << "\n";
  // Create a new socket based on config.
  scoped_ptr<mlab::ClientSocket> mbm_socket(
      mlab::ClientSocket::CreateOrDie(server, port, socket_type));

  std::cout << "Sending READY\n";
  ctrl_socket->SendOrDie(mlab::Packet(READY, strlen(READY)));
  // TODO: There may need to be a 'wait for ready-ack' loop if mbm_socket is UDP
  // and loss is high.
  mbm_socket->SendOrDie(mlab::Packet(READY, strlen(READY)));

  // Expect test to start now. Server drives the test by picking a CBR and
  // sending data at that rate while counting losses. All we need to do is
  // receive and dump the data.
  ssize_t bytes_read;
  const uint32_t chunk_len = ntohl(
      ctrl_socket->ReceiveX(sizeof(chunk_len), &bytes_read).as<uint32_t>());
  const uint32_t dump_size = ntohl(
      ctrl_socket->ReceiveX(sizeof(dump_size), &bytes_read).as<uint32_t>());


  // Slowstart data dump
  std::vector<TrafficData> data_slowstart;
  for (uint64_t i=0; i < dump_size; ++i) {
    mlab::Packet recv = i == 0 ? mbm_socket->ReceiveOrDie(chunk_len)
                               : mbm_socket->ReceiveX(chunk_len, &bytes_read);
    double timestamp = GetTimeNS();
    uint32_t seq_no = ntohl(recv.as<uint32_t>());
    uint32_t nonce = ntohl(*reinterpret_cast<const uint32_t*>(&recv.buffer()[4]));
    data_slowstart.push_back(TrafficData(seq_no, nonce, timestamp));

    if (recv.length() == 0) {
      std::cerr << "Something went wrong. The server might have died: "
                << strerror(errno) << "\n";
      return RESULT_ERROR;
    }
  }

  // Actual test traffic
  std::vector<TrafficData> data_collected;
  uint32_t bytes_total = chunk_len * MAX_PACKETS_TO_SEND;
  uint32_t bytes_received = 0;
  uint32_t last_percent = 0;

  std::cout << "expecting at most " << bytes_total << " bytes\n";
  if (bytes_total == 0) {
    std::cerr << "Something went wrong. The server might have died.\n";
    return RESULT_ERROR;
  }

  fd_set fds;
  uint64_t start_time = GetTimeNS();

  while (true) {
    FD_ZERO(&fds);
    FD_SET(ctrl_socket->raw(), &fds);
    FD_SET(mbm_socket->raw(), &fds);
    int num_ready = select(FD_SETSIZE, &fds, NULL, NULL, NULL);
    if(num_ready == -1) {
      // error
    }
    if (FD_ISSET(ctrl_socket->raw(), &fds) != 0) {
      std::string msg = ctrl_socket->ReceiveOrDie(sizeof(END)).str();
      std::cout << "Received END" << std::endl;
      break;
    }
    if (FD_ISSET(mbm_socket->raw(), &fds) != 0) {
      size_t remain = bytes_total - bytes_received;
      size_t read_len = remain < chunk_len ? remain : chunk_len;
      mlab::Packet recv = bytes_received == 0 ? mbm_socket->ReceiveOrDie(chunk_len)
                                 : mbm_socket->ReceiveX(read_len, &bytes_read);
      double timestamp = GetTimeNS();
      uint32_t seq_no = ntohl(recv.as<uint32_t>());
      uint32_t nonce = ntohl(*reinterpret_cast<const uint32_t*>(&recv.buffer()[4]));
      data_collected.push_back(TrafficData(seq_no, nonce, timestamp));

      if (recv.length() == 0) {
        std::cerr << "Something went wrong. The server might have died: "
                  << strerror(errno) << "\n";
        return RESULT_ERROR;
      }
      bytes_received += recv.length();
      if (FLAGS_verbose) {
        uint32_t percent = static_cast<uint32_t>(
            static_cast<double>(100 * bytes_received) / bytes_total);
        if (percent > last_percent) {
          last_percent = percent;
          std::cout << "\r" << percent << "%" << std::flush;
        }
      }
    }
  }
  uint64_t end_time = GetTimeNS();


  if (FLAGS_verbose) {
    for (std::vector<TrafficData>::const_iterator it = data_collected.begin();
         it != data_collected.end(); ++it) {
      std::cout << "  seq_no: " << std::hex << it->seq_no() << " "
                << std::dec << it->seq_no() << "\n";
      std::cout << "  nonce: " << std::hex << it->nonce() << " "
                << std::dec << it->nonce() << "\n";
      std::cout << "  timestamp: " << std::hex << it->timestamp() << " "
                << std::dec << it->timestamp() << "\n";
    }
  }

  uint64_t delta_time = end_time - start_time;
  double delta_time_sec = static_cast<double>(delta_time) / NS_PER_SEC;
  double receive_rate = (bytes_received * 8) / delta_time_sec;
  double rate_delta_percent = (receive_rate * 100) / (rate * 1000);

  std::cout << "\nbytes received: " << bytes_received << "\n";
  std::cout << "time: " << delta_time_sec << "\n";
  std::cout << "receive rate: " << receive_rate << " b/sec ("
            << rate_delta_percent << "% of target)\n";

  std::cout << "Sending receive rate\n";
  ctrl_socket->SendOrDie(mlab::Packet(receive_rate));

  // Send the collected data back to the server
  uint32_t data_size_obj = data_collected.size();
  ctrl_socket->SendOrDie(mlab::Packet(htonl(data_size_obj)));
  uint32_t data_size_bytes = data_size_obj * sizeof(TrafficData);

  std::cout << "sending collected data..." << std::endl;

  std::vector<TrafficData> send_buffer(data_size_obj);
  for (uint32_t i=0; i<data_size_obj; ++i) {
    send_buffer[i] = TrafficData::hton(data_collected[i]);
  }

  ctrl_socket->SendOrDie(
    mlab::Packet(
      reinterpret_cast<const char*>(&send_buffer[0]), data_size_bytes));


  Result result;
  mlab::Packet result_pkt = ctrl_socket->ReceiveX(sizeof(result), &bytes_read);
  result = static_cast<Result>(ntohl(result_pkt.as<Result>()));
  std::cout << (socket_type == SOCKETTYPE_TCP ? "tcp" : "udp") << " @ " << rate
            << ": " << kResultStr[result] << "\n";
  return result;
}
}  // namespace
}  // namespace mbm

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  mlab::Initialize("mbm_client", MBM_VERSION);
  mlab::SetLogSeverity(mlab::WARNING);
  if (FLAGS_verbose)
    mlab::SetLogSeverity(mlab::VERBOSE);
  gflags::SetVersionString(MBM_VERSION);

  if (FLAGS_sweep) {
    // Do UDP sweep and then TCP test.
    int rate = FLAGS_minrate;
    for (; rate <= FLAGS_maxrate; rate += FLAGS_ratestep) {
      mbm::Result result = mbm::Run(SOCKETTYPE_UDP, rate, FLAGS_rtt, FLAGS_mss);
      if (result == mbm::RESULT_FAIL) {
        if (rate == FLAGS_minrate) {
          std::cerr << "Minimum rate " << FLAGS_minrate << " kbps is too "
                    << "high\n";
          return 1;
        }
        std::cout << "First UDP fail at " << rate << " kbps\n";
        break;
      } else if (result == mbm::RESULT_INCONCLUSIVE) {
        std::cerr << "Inconclusive result at " << rate << " kbps\n";
      }
    }

    if (rate > FLAGS_maxrate) {
      std::cerr << "Maxmimum rate " << FLAGS_maxrate << " kbps is too low\n";
      return 1;
    }
    mbm::Run(SOCKETTYPE_TCP, rate - FLAGS_ratestep, FLAGS_rtt, FLAGS_mss);
  } else {
    // Single run at a given rate.
    SocketType mbm_socket_type = SOCKETTYPE_TCP;
    if (FLAGS_socket_type == "udp")
      mbm_socket_type = SOCKETTYPE_UDP;

    mbm::Run(mbm_socket_type, FLAGS_rate, FLAGS_rtt, FLAGS_mss);
  }

  return 0;
}
