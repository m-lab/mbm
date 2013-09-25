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
#include "gflags/gflags.h"
#include "mlab/accepted_socket.h"
#include "mlab/mlab.h"
#include "mlab/listen_socket.h"
#include "server/cbr.h"

// TODO: configuration
#define BASE_PORT 12345
#define NUM_PORTS 100

DEFINE_int32(port, 4242, "The port to listen on");

namespace {
bool ValidatePort(const char* flagname, int32_t value) {
  if (value > 0 && value < 65536)
    return true;
  std::cerr << "Invalid value for --" << flagname << ": " << value << "\n";
  return false;
}
}  // namespace

namespace mbm {
bool used_port[NUM_PORTS];
pthread_mutex_t used_port_mutex = PTHREAD_MUTEX_INITIALIZER;

struct ServerConfig {
  ServerConfig(uint16_t port, const Config& config)
      : port(port), config(config) {}
  uint16_t port;
  Config config;
};

void* ServerThread(void* server_config_data) {
  scoped_ptr<ServerConfig> server_config(
      reinterpret_cast<ServerConfig*>(server_config_data));

  {
    const uint16_t port = server_config->port + BASE_PORT;

    // TODO: Consider not dying but picking a different port.
    scoped_ptr<mlab::ListenSocket> mbm_socket(mlab::ListenSocket::CreateOrDie(
        port, server_config->config.socket_type));

    std::cout << "Listening on " << port << "\n";

    mbm_socket->Select();
    scoped_ptr<mlab::AcceptedSocket> accepted_socket(mbm_socket->Accept());

#ifdef USE_WEB100
    web100::CreateConnection(accepted_socket.get());
#endif

    std::cout << "Waiting for READY\n";
    assert(accepted_socket->ReceiveOrDie(strlen(READY)).str() == READY);

    Result result = RunCBR(accepted_socket.get(), server_config->config);
    if (result == RESULT_ERROR)
      std::cerr << kResultStr[result] << "\n";
    else
      std::cout << kResultStr[result] << "\n";
    // TODO: This should be sent back on the control socket, but it no longer
    // exists. So we need to extend the lifetime of the control socket until
    // this thread completes, then send this packet, then delete the control
    // socket.
    accepted_socket->SendOrDie(mlab::Packet(htonl(result)));
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

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  using namespace mbm;

  mlab::Initialize("mbm_server", MBM_VERSION);
  mlab::SetLogSeverity(mlab::VERBOSE);

#ifdef USE_WEB100
  web100::Initialize();
#endif

  for (int i = 0; i < NUM_PORTS; ++i)
    used_port[i] = false;

  scoped_ptr<mlab::ListenSocket> socket(
      mlab::ListenSocket::CreateOrDie(FLAGS_port));

  while (true) {
    socket->Select();
    std::cout << "New connection\n";
    scoped_ptr<mlab::AcceptedSocket> ctrl_socket(socket->Accept());

    std::cout << "Getting config\n";
    const Config config(ctrl_socket->ReceiveOrDie(1024).str());

    std::cout << "Setting config [" << config.socket_type << " | "
              << config.cbr_kb_s << " kb/s | " << config.loss_threshold
              << " %]\n";

    // Pick a port.
    uint16_t mbm_port = mbm::GetAvailablePort();
    pthread_mutex_lock(&used_port_mutex);
    used_port[mbm_port] = true;
    pthread_mutex_unlock(&used_port_mutex);

    // Note, the server thread will delete this.
    // TODO(dominic): This should pass the client address through to restrict
    // connections.
    ServerConfig* server_config = new ServerConfig(mbm_port, config);

    // Each server socket runs on a different thread.
    pthread_t thread;
    std::cout << "Starting server thread for port " << (mbm_port + BASE_PORT)
              << "\n";
    int rc = pthread_create(&thread, NULL, mbm::ServerThread,
                            (void*)server_config);
    if (rc != 0) {
      std::cerr << "Failed to create thread: " << strerror(errno) << " ["
                << errno << "]\n";
      return 1;
    }

    // Let the client know that they can connect.
    std::stringstream ss;
    ss << mbm_port + BASE_PORT;
    std::cout << "Telling client to connect on port " << (mbm_port + BASE_PORT)
              << "\n";
    ctrl_socket->SendOrDie(mlab::Packet(ss.str()));
  }

#ifdef USE_WEB100
  web100::Shutdown();
#endif  // USE_WEB100

  pthread_exit(NULL);
  return 0;
}
