#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef USE_WEB100
extern "C" {
#include <web100/web100.h>
}
#endif

#include <iostream>
#include <sstream>

#include "common/config.h"
#include "common/constants.h"
#ifdef USE_WEB100
#include "common/web100.h"
#endif  // USE_WEB100
#include "common/scoped_ptr.h"
#include "mlab/mlab.h"
#include "mlab/server_socket.h"
#include "server/cbr.h"

// TODO: configuration
#define BASE_PORT 12345
#define NUM_PORTS 100

namespace mbm {

const char* control_port = NULL;
bool used_port[NUM_PORTS];
pthread_mutex_t used_port_mutex = PTHREAD_MUTEX_INITIALIZER;

const char* result_str[NUM_RESULTS] = {
  "FAIL", "PASS", "INCONCLUSIVE", "ERROR"
};

struct ServerConfig {
  ServerConfig(uint16_t port, const Config& config)
      : port(port), config(config) { }
  uint16_t port;
  Config config;
};

void* ServerThread(void* server_config_data) {
  scoped_ptr<ServerConfig> server_config(
      reinterpret_cast<ServerConfig*>(server_config_data));

  {
    const uint16_t port = server_config->port + BASE_PORT;

    // TODO: Consider not dying but picking a different port.
    scoped_ptr<mlab::ServerSocket> mbm_socket(mlab::ServerSocket::CreateOrDie(
        port, server_config->config.socket_type));

    std::cout << "Listening on " << port << "\n";

    mbm_socket->Select();
    mbm_socket->Accept();

#ifdef USE_WEB100
    web100::CreateConnection(mbm_socket.get());
#endif

    assert(mbm_socket->ReceiveOrDie(strlen(READY)).str() == READY);

    Result result = RunCBR(mbm_socket.get(), server_config->config);
    if (result == RESULT_ERROR)
      std::cerr << result_str[result];
    else
      std::cout << result_str[result];
    mbm_socket->SendOrDie(
        mlab::Packet(result_str[result], strlen(result_str[result])));
  }

  pthread_mutex_lock(&used_port_mutex);
  used_port[server_config->port] = false;
  pthread_mutex_unlock(&used_port_mutex);

  pthread_exit(NULL);
}

uint16_t GetAvailablePort() {
  // TODO: This could be smarter - maintain a set of unused ports, eg., and
  // pick the first.
  uint16_t mbm_port = 0;
  for (; mbm_port < NUM_PORTS; ++mbm_port) {
    if (!used_port[mbm_port])
      break;
  }
  assert(mbm_port != NUM_PORTS);
  return mbm_port;
}

}  // namespace mbm

int main(int argc, const char* argv[]) {
  using namespace mbm;

  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <control_port>\n";
    return 1;
  }

  mlab::Initialize("mbm_server", MBM_VERSION);
  mlab::SetLogSeverity(mlab::VERBOSE);

  control_port = argv[1];

#ifdef USE_WEB100
    web100::Initialize();
#endif

  scoped_ptr<mlab::ServerSocket> socket(
      mlab::ServerSocket::CreateOrDie(atoi(control_port)));

  for (int i = 0; i < NUM_PORTS; ++i)
    used_port[i] = false;

  while (true) {
    socket->Select();
    socket->Accept();

    const Config config(socket->ReceiveOrDie(1024).str());

    std::cout << "Setting config [" << config.socket_type << " | " <<
                 config.cbr_kb_s << " kb/s | " <<
                 config.loss_threshold << " %]\n";

    // Pick a port.
    uint16_t mbm_port = mbm::GetAvailablePort();
    pthread_mutex_lock(&used_port_mutex);
    used_port[mbm_port] = true;
    pthread_mutex_unlock(&used_port_mutex);

    // Note, the server thread will delete this.
    ServerConfig* server_config = new ServerConfig(mbm_port, config);

    // Each server socket runs on a different thread.
    pthread_t thread;
    int rc = pthread_create(&thread, NULL, mbm::ServerThread,
                            (void*) server_config);
    if (rc != 0) {
      std::cerr << "Failed to create thread: " << strerror(errno) <<
                   " [" << errno << "]\n";
      return 1;
    }

    // Let the client know that they can connect.
    std::stringstream ss;
    ss << mbm_port + BASE_PORT;
    socket->SendOrDie(mlab::Packet(ss.str()));
  }

#ifdef USE_WEB100
  web100::Shutdown();
#endif  // USE_WEB100

  pthread_exit(NULL);
  return 0;
}

