#include "mlab/server_socket.h"

#include <iostream>
#include <string>
#include <sstream>

int main(int argc, const char* argv[]) {
  mlab::ServerSocket* socket = mlab::ServerSocket::CreateOrDie(4242);
  socket->Accept();

  uint32_t cbr;
  std::istringstream(socket->Receive(sizeof(uint32_t))) >> cbr;

  std::cout << "Setting CBR to " << cbr << "\n";
  return 0;
}
