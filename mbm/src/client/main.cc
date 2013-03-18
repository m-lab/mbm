#include "common/config.h"
#include "common/constants.h"
#include "common/scoped_ptr.h"
#include "mlab/client_socket.h"

int main(int argc, const char* argv[]) {
  scoped_ptr<mlab::ClientSocket> socket(
      mlab::ClientSocket::CreateOrDie(mlab::Host("127.0.0.1"), 4242));

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
