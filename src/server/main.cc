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

#include "common/config.h"
#include "common/constants.h"
#include "common/scoped_ptr.h"
#include "gflags/gflags.h"
#include "mlab/accepted_socket.h"
#include "mlab/mlab.h"
#include "mlab/listen_socket.h"
#include "server/cbr.h"
#ifdef USE_WEB100
#include "server/web100.h"
#endif  // USE_WEB100

// TODO: configuration
#define BASE_PORT 12345
#define NUM_PORTS 100

DEFINE_int32(port, 4242, "The port to listen on");
DEFINE_bool(verbose, false, "Verbose output");

namespace {
bool ValidatePort(const char* flagname, int32_t value) {
  if (value > 0 && value < 65536)
    return true;
  std::cerr << "Invalid value for --" << flagname << ": " << value << "\n";
  return false;
}
}  // namespace

DEFINE_validator(port, ValidatePort);

namespace mbm {
bool used_port[NUM_PORTS];
pthread_mutex_t used_port_mutex = PTHREAD_MUTEX_INITIALIZER;

struct ServerConfig {
  // Takes ownership of the control socket.
  ServerConfig( const mlab::AcceptedSocket* ctrl_socket)
      : ctrl_socket(ctrl_socket) {}
  ~ServerConfig() {
    delete ctrl_socket;
  }

  const mlab::AcceptedSocket* ctrl_socket;
};

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


void* ServerThread(void* server_config_data) {
  scoped_ptr<ServerConfig> server_config(
      reinterpret_cast<ServerConfig*>(server_config_data));

  // Pick a port.
  pthread_mutex_lock(&used_port_mutex);
  uint16_t available_port = GetAvailablePort();
  used_port[available_port] = true;
  pthread_mutex_unlock(&used_port_mutex);

  const uint16_t port = available_port + BASE_PORT;

  {
    const mlab::AcceptedSocket* ctrl_socket = server_config->ctrl_socket;

    std::cout << "Getting config\n";
    const Config config =
        ctrl_socket->ReceiveOrDie(sizeof(Config)).as<Config>();

    std::cout << "Setting config [" << config.socket_type << " | "
              << config.cbr_kb_s << " kb/s | " << config.loss_threshold
              << " %]\n";


    // TODO: Consider not dying but picking a different port.
    scoped_ptr<mlab::ListenSocket> mbm_socket(mlab::ListenSocket::CreateOrDie(
        port, config.socket_type));

    std::cout << "Listening on " << port << "\n";

    // Let the client know that they can connect.
    std::cout << "Telling client to connect on port " << port << "\n";
    ctrl_socket->SendOrDie(mlab::Packet(htons(port)));

    mbm_socket->Select();
    scoped_ptr<mlab::AcceptedSocket> test_socket(mbm_socket->Accept());

    std::cout << "Waiting for READY\n";
    assert(ctrl_socket->ReceiveOrDie(strlen(READY)).str() == READY);
    // TODO(dominic): This may need to become a while loop if test_socket is UDP
    // and loss is high.
    assert(test_socket->ReceiveOrDie(strlen(READY)).str() == READY);

    // TODO(dominic): Consider passing the ServerConfig entirely
    Result result = RunCBR(test_socket.get(),
                           ctrl_socket,
                           config);
    if (result == RESULT_ERROR)
      std::cerr << kResultStr[result] << "\n";
    else
      std::cout << kResultStr[result] << "\n";
    ctrl_socket->SendOrDie(mlab::Packet(htonl(result)));
  }

  pthread_mutex_lock(&used_port_mutex);
  used_port[available_port] = false;
  pthread_mutex_unlock(&used_port_mutex);

  pthread_exit(NULL);
}

}  // namespace mbm

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  using namespace mbm;

  mlab::Initialize("mbm_server", MBM_VERSION);
  mlab::SetLogSeverity(mlab::WARNING);
  if (FLAGS_verbose)
    mlab::SetLogSeverity(mlab::VERBOSE);
  gflags::SetVersionString(MBM_VERSION);

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
    const mlab::AcceptedSocket* ctrl_socket(socket->Accept());

    ServerConfig* server_config =
        new ServerConfig(ctrl_socket);

    // Each server socket runs on a different thread.
    pthread_t thread;
    int rc = pthread_create(&thread, NULL, mbm::ServerThread,
                            (void*)server_config);
    if (rc != 0) {
      std::cerr << "Failed to create thread: " << strerror(errno) << " ["
                << errno << "]\n";
      return 1;
    }
  }

#ifdef USE_WEB100
  web100::Shutdown();
#endif  // USE_WEB100

  pthread_exit(NULL);
  return 0;
}
