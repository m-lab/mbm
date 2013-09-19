#include <arpa/inet.h>
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
  if (argc < 3 || argc > 5) {
    std::cout << "Usage: " << argv[0]
              << " <server> <control_port> [rate] [socket_type]\n";
    return 1;
  }

  mlab::Initialize("mbm_client", MBM_VERSION);
  mlab::SetLogSeverity(mlab::WARNING);

  mlab::Host server(argv[1]);
  scoped_ptr<mlab::ClientSocket> socket(
      mlab::ClientSocket::CreateOrDie(server, atoi(argv[2])));

  uint32_t rate = 600;
  if (argc >= 4) {
    rate = atoi(argv[3]);
  }

  SocketType socket_type = SOCKETTYPE_TCP;
  if (argc == 5) {
    if (strncmp("udp", argv[4], 3) == 0)
      socket_type = SOCKETTYPE_UDP;
    else if (strncmp("tcp", argv[4], 3) != 0) {
      std::cerr << "Invalid socket type: " << argv[4] << "\n";
      return 1;
    }
  }

  const mbm::Config config(socket_type, rate, 0.0);
  socket->SendOrDie(mlab::Packet(config.AsString()));

  uint16_t port = atoi(socket->ReceiveOrDie(16).str().c_str());
  std::cout << "Connecting to port " << port << "\n";

  // Create a new socket based on config.
  scoped_ptr<mlab::ClientSocket> mbm_socket(
      mlab::ClientSocket::CreateOrDie(
          server, port, config.socket_type));

  mbm_socket->SendOrDie(mlab::Packet(READY, strlen(READY)));

  // Expect test to start now. Server drives the test by picking a CBR and
  // sending data at that rate while counting losses. All we need to do is
  // receive and dump the data.
  // TODO(dominic): Determine best size chunk to receive.
  const size_t chunk_len = 10 * 1024;
  uint32_t bytes_total = 0;
  ssize_t bytes_read;
  mlab::Packet chunk_len_pkt = mbm_socket->ReceiveX(sizeof(bytes_total), 
						    &bytes_read);
  bytes_total = ntohl(chunk_len_pkt.as<uint32_t>());
  std::cout << "expecting " << bytes_total << " bytes\n";

  uint32_t bytes_received = 0;
  std::string recv = mbm_socket->ReceiveOrDie(chunk_len).str();
  bytes_received += recv.length();
  while (bytes_received < bytes_total) {
    size_t remain = bytes_total - bytes_received;
    size_t read_len = remain < chunk_len ? remain : chunk_len;
    // std::cout << "." << std::flush;
    recv = mbm_socket->ReceiveOrDie(read_len).str();
    bytes_received += recv.length();
    // std::cout << "received " << bytes_received << "\n";
  }
  //  std::cout << recv.substr(recv.find(END_OF_LINE) + strlen(END_OF_LINE) - 1,
  //                         recv.length()) << "\n";
  std::cout << mbm_socket->ReceiveOrDie(chunk_len).str() << "\n";
  return 0;
}
