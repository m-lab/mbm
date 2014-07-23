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

  uint16_t port = available_port + BASE_PORT;

  do {
    const mlab::AcceptedSocket* ctrl_socket = server_config->ctrl_socket;
    // set send and receive timeout for ctrl socket
    timeval timeout = {5, 0};
    if (setsockopt(ctrl_socket->raw(), SOL_SOCKET, SO_RCVTIMEO, 
                   (const char*) &timeout, sizeof(timeout))
        == -1) {
      // TODO: handle error
      break;
    }
    if (setsockopt(ctrl_socket->raw(), SOL_SOCKET, SO_SNDTIMEO, 
                   (const char*) &timeout, sizeof(timeout))
        == -1) {
      // TODO: handle error
      break;
    }

    std::cout << "Getting config\n";
    ssize_t num_bytes;
    mlab::Packet config_buff = ctrl_socket->Receive(sizeof(Config), &num_bytes);
    if (num_bytes < 0 || static_cast<unsigned>(num_bytes) < sizeof(Config) ) {
      // TODO: handle error
      break;
    }
    const Config config = config_buff.as<Config>();

    std::cout << "Setting config [" << config.socket_type << " | "
              << config.cbr_kb_s << " kb/s | " << config.rtt_ms << " ms | "
              << config.mss_bytes << " bytes" << " ]\n";


    // create listen socket, if error occurs pick another port
    // if error occurs more than 3 times terminate the test
    mlab::ListenSocket* listen_socket = NULL;
    for (int count = 0; count < 3; ++count) {
      listen_socket = mlab::ListenSocket::Create(port, config.socket_type);
      if (listen_socket) break;

      uint16_t current = available_port;
      pthread_mutex_lock(&used_port_mutex);
      available_port = GetAvailablePort();
      used_port[current] = false;
      used_port[available_port] = true;
      pthread_mutex_unlock(&used_port_mutex);
      port = available_port + BASE_PORT;
    }
    if (!listen_socket) {
      // TODO:handle error
      break;
    }
    scoped_ptr<mlab::ListenSocket> mbm_socket(listen_socket);

    std::cout << "Listening on " << port << "\n";

    // Let the client know that they can connect.
    std::cout << "Telling client to connect on port " << port << "\n";
    if (!ctrl_socket->Send(mlab::Packet(htons(port)), &num_bytes)) {
      // TODO: handle error
      break;
    }

    mlab::AcceptedSocket* test_socket_buff = mbm_socket->Accept();
    if (!test_socket_buff) {
      // TODO: handle error
      break;
    }

    scoped_ptr<mlab::AcceptedSocket> test_socket(test_socket_buff);
    if (setsockopt(test_socket->raw(), SOL_SOCKET, SO_RCVTIMEO, 
                   (const char*) &timeout, sizeof(timeout))
        == -1) {
      // TODO: handle error
      break;
    }
    if (setsockopt(test_socket->raw(), SOL_SOCKET, SO_SNDTIMEO, 
                   (const char*) &timeout, sizeof(timeout))
        == -1) {
      // TODO: handle error
      break;
    }

    std::cout << "Waiting for READY\n";
    std::string ctrl_ready = ctrl_socket->Receive(strlen(READY), &num_bytes).str();
    std::string test_ready = test_socket->Receive(strlen(READY), &num_bytes).str();
    if (ctrl_ready != READY || test_ready != READY) {
      // TODO: handle error
      break;
    }
    if (!ctrl_socket->Send(mlab::Packet(READY, strlen(READY)), &num_bytes)) {
      // TODO: handle error
      break;
    }
    
    Result result = RunCBR(test_socket.get(),
                           ctrl_socket,
                           config);
    if (result == RESULT_ERROR)
      std::cerr << kResultStr[result] << "\n";
    else
      std::cout << kResultStr[result] << "\n";

    if (!ctrl_socket->Send(mlab::Packet(htonl(result)), &num_bytes)) {
      // TODO: handle error
      break;
    }
  } while(false);

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
  srand(SEED);

#ifdef USE_WEB100
  web100::Initialize();
#endif

  for (int i = 0; i < NUM_PORTS; ++i)
    used_port[i] = false;

  scoped_ptr<mlab::ListenSocket> socket(
      mlab::ListenSocket::CreateOrDie(FLAGS_port));
  std::cout << "Listening on port " << FLAGS_port << std::endl;

  while (true) {
    socket->Select();
    std::cout << "New connection\n";
    const mlab::AcceptedSocket* ctrl_socket(socket->Accept());
    if (!ctrl_socket) continue;

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
