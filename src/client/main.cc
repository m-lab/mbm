#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>

#include "common/config.h"
#include "common/constants.h"
#include "common/scoped_ptr.h"
#include "gflags/gflags.h"
#include "mlab/client_socket.h"
#include "mlab/mlab.h"

DEFINE_string(server, "localhost", "The server to connect to");
DEFINE_int32(port, 4242, "The port to connect to");
DEFINE_string(socket_type, "tcp", "The transport protocol to use");

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
}  // namespace

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  mlab::Initialize("mbm_client", MBM_VERSION);
  mlab::SetLogSeverity(mlab::WARNING);

  mlab::Host server(FLAGS_server);
  scoped_ptr<mlab::ClientSocket> socket(
      mlab::ClientSocket::CreateOrDie(server, FLAGS_port));

  SocketType socket_type = SOCKETTYPE_TCP;
  if (FLAGS_socket_type == "udp")
    socket_type = SOCKETTYPE_UDP;

  const mbm::Config config(socket_type, 600 * 1024, 0.0);
  socket->SendOrDie(mlab::Packet(config.AsString()));

  uint16_t port = atoi(socket->ReceiveOrDie(16).str().c_str());
  std::cout << "Connecting to port " << port << "\n";

  // Create a new socket based on config.
  scoped_ptr<mlab::ClientSocket> mbm_socket(
      mlab::ClientSocket::CreateOrDie(server, port, config.socket_type));

  mbm_socket->SendOrDie(mlab::Packet(READY, strlen(READY)));

  // Expect test to start now. Server drives the test by picking a CBR and
  // sending data at that rate while counting losses. All we need to do is
  // receive and dump the data.
  // TODO(dominic): Determine best size chunk to receive.
  const size_t chunk_len = 10 * 1024;
  std::string recv = mbm_socket->ReceiveOrDie(chunk_len).str();
  while (recv.find(END_OF_LINE) == std::string::npos) {
    std::cout << "." << std::flush;
    recv = mbm_socket->ReceiveOrDie(chunk_len).str();
  }
  std::cout << recv.substr(recv.find(END_OF_LINE) + strlen(END_OF_LINE) - 1,
                           recv.length()) << "\n";
  std::cout << mbm_socket->ReceiveOrDie(chunk_len).str() << "\n";
  return 0;
}
