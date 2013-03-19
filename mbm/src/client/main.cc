#include "common/config.h"
#include "common/constants.h"
#include "common/scoped_ptr.h"
#include "mlab/client_socket.h"

#include <assert.h>
#include <stdlib.h>

#include <iostream>

int main(int argc, const char* argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <server> <port>\n";
    return 1;
  }

  mlab::Host server(argv[1]);
  scoped_ptr<mlab::ClientSocket> socket(
      mlab::ClientSocket::CreateOrDie(server, atoi(argv[2])));

  const Config config(1024, 100 * 1024, 10);
  socket->Send(config.AsString());

  // Expect test to start now. Server drives the test by picking a CBR and
  // sending data at that rate while counting losses. All we need to do is
  // receive and dump the data.
  // TODO(dominic): Determine best size chunk to receive.
  while (socket->Receive(10 * 1024).find(END_OF_LINE) == std::string::npos) {
  }
  return 0;
}
