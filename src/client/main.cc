#include <arpa/inet.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>

#include "common/config.h"
#include "common/constants.h"
#include "common/result.h"
#include "common/scoped_ptr.h"
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

mbm::Result Run(SocketType socket_type, int rate) {
  std::cout << "Running MBM test over "
            << (socket_type == SOCKETTYPE_TCP ? "tcp" : "udp") << " at "
            << rate << " kbps\n";

  const mlab::Host server(FLAGS_server);
  scoped_ptr<mlab::ClientSocket> ctrl_socket(
      mlab::ClientSocket::CreateOrDie(server, FLAGS_port));

  std::cout << "Sending config\n";
  const mbm::Config config(socket_type, rate, 0.0);
  ctrl_socket->SendOrDie(mlab::Packet(config.AsString()));

  std::cout << "Getting port\n";
  uint16_t port = atoi(ctrl_socket->ReceiveOrDie(16).str().c_str());

  std::cout << "Connecting on port " << port << "\n";
  // Create a new socket based on config.
  scoped_ptr<mlab::ClientSocket> mbm_socket(
      mlab::ClientSocket::CreateOrDie(server, port, socket_type));

  std::cout << "Sending READY\n";
  ctrl_socket->SendOrDie(mlab::Packet(READY, strlen(READY)));

  // Expect test to start now. Server drives the test by picking a CBR and
  // sending data at that rate while counting losses. All we need to do is
  // receive and dump the data.
  // TODO(dominic): Determine best size chunk to receive.
  const size_t chunk_len = 10 * 1024;
  uint32_t bytes_total = 0;
  ssize_t bytes_read;
  mlab::Packet chunk_len_pkt =
      ctrl_socket->ReceiveX(sizeof(bytes_total), &bytes_read);
  bytes_total = ntohl(chunk_len_pkt.as<uint32_t>());
  std::cout << "expecting " << bytes_total << " bytes\n";
  if (bytes_total == 0) {
    std::cerr << "Something went wrong. The server might have died.\n";
    return mbm::RESULT_ERROR;
  }

  uint32_t bytes_received = 0;
  uint32_t last_percent = 0;
  std::string recv = mbm_socket->ReceiveOrDie(chunk_len).str();
  bytes_received += recv.length();
  while (bytes_received < bytes_total) {
    size_t remain = bytes_total - bytes_received;
    size_t read_len = remain < chunk_len ? remain : chunk_len;
    recv = mbm_socket->ReceiveOrDie(read_len).str();
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
  std::cout << "\rbytes received: " << bytes_received << "\n";
  mbm::Result result;
  mlab::Packet result_pkt = ctrl_socket->ReceiveX(sizeof(result), &bytes_read);
  result = static_cast<mbm::Result>(ntohl(result_pkt.as<mbm::Result>()));
  std::cout << (socket_type == SOCKETTYPE_TCP ? "tcp" : "udp") << " @ " << rate
            << ": " << mbm::kResultStr[result] << "\n";
  return result;
}
}  // namespace

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  mlab::Initialize("mbm_client", MBM_VERSION);
  if (FLAGS_verbose)
    mlab::SetLogSeverity(mlab::VERBOSE);
  gflags::SetVersionString(MBM_VERSION);

  if (FLAGS_sweep) {
    // Do UDP sweep and then TCP test.
    int rate = FLAGS_minrate;
    for (; rate <= FLAGS_maxrate; rate += FLAGS_ratestep) {
      mbm::Result result = Run(SOCKETTYPE_UDP, rate);
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
    Run(SOCKETTYPE_TCP, rate - FLAGS_ratestep);
  } else {
    // Single run at a given rate.
    SocketType mbm_socket_type = SOCKETTYPE_TCP;
    if (FLAGS_socket_type == "udp")
      mbm_socket_type = SOCKETTYPE_UDP;

    Run(mbm_socket_type, FLAGS_rate);
  }

  return 0;
}
