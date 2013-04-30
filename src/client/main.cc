#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>

#include "common/config.h"
#include "common/constants.h"
#include "common/scoped_ptr.h"
#include "mlab/client_socket.h"
#include "mlab/mlab.h"

int main(int argc, const char* argv[]) {
  if (argc < 3 || argc > 4) {
    std::cerr << "Usage: " << argv[0] << " <server> <control_port> [socket_type]\n";
    return 1;
  }

  mlab::Initialize("mbm_client", MBM_VERSION);
  mlab::SetLogSeverity(mlab::WARNING);

  mlab::Host server(argv[1]);
  scoped_ptr<mlab::ClientSocket> socket(
      mlab::ClientSocket::CreateOrDie(server, atoi(argv[2])));

  SocketType socket_type = SOCKETTYPE_TCP;
  if (argc == 4) {
    if (strncmp("udp", argv[3], 3) == 0)
      socket_type = SOCKETTYPE_UDP;
    else if (strncmp("tcp", argv[3], 3) != 0) {
      std::cerr << "Invalid socket type: " << argv[3] << "\n";
      return 1;
    }
  }

  const mbm::Config config(socket_type, 600 * 1024, 0.0);
  socket->Send(mlab::Packet(config.AsString()));

  uint16_t port = atoi(socket->Receive(16).str().c_str());
  std::cout << "Connecting to port " << port << "\n";

  // Create a new socket based on config.
  scoped_ptr<mlab::ClientSocket> mbm_socket(
      mlab::ClientSocket::CreateOrDie(
          server, port, config.socket_type));

  mbm_socket->Send(mlab::Packet(READY, strlen(READY)));

  // Expect test to start now. Server drives the test by picking a CBR and
  // sending data at that rate while counting losses. All we need to do is
  // receive and dump the data.
  // TODO(dominic): Determine best size chunk to receive.
  const size_t chunk_len = 10 * 1024;
  std::string recv = mbm_socket->Receive(chunk_len).str();
  while (recv.find(END_OF_LINE) == std::string::npos) {
    std::cout << "." << std::flush;
//    if (recv.empty())
//      break;
    recv = mbm_socket->Receive(chunk_len).str();
  }
  std::cout << recv.substr(recv.find(END_OF_LINE) + strlen(END_OF_LINE) - 1,
                           recv.length()) << "\n";
  std::cout << mbm_socket->Receive(chunk_len).str() << "\n";
  return 0;
}
