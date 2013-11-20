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

Result Run(SocketType socket_type, int rate) {
  std::cout.setf(std::ios_base::fixed);
  std::cout.precision(3);
  std::cout << "Running MBM test over "
            << (socket_type == SOCKETTYPE_TCP ? "tcp" : "udp") << " at "
            << rate << " kbps\n";

  const mlab::Host server(FLAGS_server);
  scoped_ptr<mlab::ClientSocket> ctrl_socket(
      mlab::ClientSocket::CreateOrDie(server, FLAGS_port));

  std::cout << "Sending config\n";
  const Config config(socket_type, rate, 0.0);
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
  uint32_t bytes_total = chunk_len * TOTAL_PACKETS_TO_SEND;
  std::cout << "expecting " << bytes_total << " bytes\n";
  if (bytes_total == 0) {
    std::cerr << "Something went wrong. The server might have died.\n";
    return RESULT_ERROR;
  }

  uint64_t start_time = GetTimeNS();
  std::vector<uint32_t> seq_nos;
  uint32_t bytes_received = 0;
  uint32_t last_percent = 0;
  mlab::Packet recv = mbm_socket->ReceiveOrDie(chunk_len);
  if (recv.length() == 0) {
    std::cerr << "Something went wrong. The server might have died: "
              << strerror(errno) << "\n";
    return RESULT_ERROR;
  }
  bytes_received += recv.length();
  seq_nos.push_back(ntohl(recv.as<uint32_t>()));
  while (bytes_received < bytes_total) {
    size_t remain = bytes_total - bytes_received;
    size_t read_len = remain < chunk_len ? remain : chunk_len;
    recv = mbm_socket->ReceiveX(read_len, &bytes_read);
    if (recv.length() == 0) {
      std::cerr << "Something went wrong. The server might have died: "
                << strerror(errno) << "\n";
      return RESULT_ERROR;
    }
    bytes_received += recv.length();
    seq_nos.push_back(ntohl(recv.as<uint32_t>()));
    if (FLAGS_verbose) {
      uint32_t percent = static_cast<uint32_t>(
          static_cast<double>(100 * bytes_received) / bytes_total);
      if (percent > last_percent) {
        last_percent = percent;
        std::cout << "\r" << percent << "%" << std::flush;
      }
    }
  }
  uint64_t end_time = GetTimeNS();

  if (FLAGS_verbose) {
    for (std::vector<uint32_t>::const_iterator it = seq_nos.begin();
         it != seq_nos.end(); ++it) {
      std::cout << "  s: " << std::hex << *it << " " << std::dec << *it << "\n";
    }
  }

  uint64_t delta_time = end_time - start_time;
  double delta_time_sec = static_cast<double>(delta_time) / NS_PER_SEC;

  std::cout << "\nbytes received: " << bytes_received << "\n";
  std::cout << "time: " << delta_time_sec << "\n";

  double receive_rate = (bytes_received * 8) / delta_time_sec;
  double rate_delta_percent = (receive_rate * 100) / (rate * 1000);
  std::cout << "receive rate: " << receive_rate << " b/sec ("
            << rate_delta_percent << "% of target)\n";

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
      mbm::Result result = mbm::Run(SOCKETTYPE_UDP, rate);
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
    mbm::Run(SOCKETTYPE_TCP, rate - FLAGS_ratestep);
  } else {
    // Single run at a given rate.
    SocketType mbm_socket_type = SOCKETTYPE_TCP;
    if (FLAGS_socket_type == "udp")
      mbm_socket_type = SOCKETTYPE_UDP;

    mbm::Run(mbm_socket_type, FLAGS_rate);
  }

  return 0;
}
