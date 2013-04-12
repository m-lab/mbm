#include <assert.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#ifdef USE_WEB100
extern "C" {
#include <web100/web100.h>
}
#endif

#include <iostream>
#include <map>
#include <string>
#include <sstream>

#include "common/config.h"
#include "common/constants.h"
#ifdef USE_WEB100
#include "common/web100.h"
#endif  // USE_WEB100
#include "common/scoped_ptr.h"
#include "mlab/mlab.h"
#include "mlab/server_socket.h"

// TODO: configuration
#define PACKETS_PER_CHUNK 3
#define TOTAL_PACKETS_TO_SEND 500

#define BASE_PORT 12345
#define NUM_PORTS 100

namespace mbm {

enum Result {
  RESULT_FAIL,
  RESULT_PASS,
  RESULT_INCONCLUSIVE
};

const char* control_port = NULL;
uint32_t lost_packets = 0;
bool used_port[NUM_PORTS];
pthread_mutex_t used_port_mutex = PTHREAD_MUTEX_INITIALIZER;

struct ServerConfig {
  ServerConfig(uint16_t port, const Config& config)
      : port(port), config(config) { }
  uint16_t port;
  Config config;
};

uint32_t get_time_ns() {
  struct timespec time;
  clock_gettime(CLOCK_MONOTONIC_RAW, &time);
  return time.tv_sec * 1000000000 + time.tv_nsec;
}

Result RunCBR(const mlab::Socket* socket, const Config& config) {
  std::cout << "Running CBR at " << config.cbr_kb_s << "\n";

  lost_packets = 0;

  uint32_t bytes_per_chunk = PACKETS_PER_CHUNK * TCP_MSS;
  uint32_t bytes_per_ms = config.cbr_kb_s * 1024 / (8 * 1000);
  uint32_t time_per_chunk_ns = (1000000 * bytes_per_chunk) / bytes_per_ms;

  std::cout << "  bytes_per_chunk: " << bytes_per_chunk << "\n";
  std::cout << "  bytes_per_ms: " << bytes_per_ms << "\n";
  std::cout << "  time_per_chunk_ns: " << time_per_chunk_ns << "\n";

  // TODO(dominic): Tell the client the |bytes_per_chunk| so they know how much
  // to ask for on every tick.
  char chunk_data[bytes_per_chunk];
  memset(chunk_data, 0, bytes_per_chunk);

#ifdef USE_WEB100
  web100::Start();
#endif
  uint32_t packets_sent = 0;
  while (packets_sent < TOTAL_PACKETS_TO_SEND) {
  //  std::cout << '.' << std::flush;
    uint32_t start_time = get_time_ns();

    // Embed sequence number.
    // TODO: Should we do three sends, one per packet?
    // TODO: if we're running UDP, get the sequence numbers back over TCP after
    // test to see which were lost.
    sprintf(chunk_data, "%u:", packets_sent);

    // And send
    socket->Send(chunk_data);
    packets_sent += PACKETS_PER_CHUNK;

    // If we have time left over, sleep the remainder.
    uint32_t end_time = get_time_ns();
    uint32_t time_taken_to_send_ns = end_time - start_time;
    int32_t left_over_ns = time_per_chunk_ns - time_taken_to_send_ns;

    if (left_over_ns > 0) {
      std::cout << "." << std::flush;

      struct timespec sleep_req = {left_over_ns / 1000000000,
                                   left_over_ns % 1000000000};
      struct timespec sleep_rem;
      int slept = nanosleep(&sleep_req, &sleep_rem);
      while (slept == -1) {
        assert(errno == EINTR);
        slept = nanosleep(&sleep_rem, &sleep_rem);
      }
    } else {
      // Warning: Took longer to send than we budgeted.
      std::cout << "o" << std::flush;
    }
  }
  socket->Send(END_OF_LINE);
  std::cout << "\n";
#ifdef USE_WEB100
  web100::Stop();
  lost_packets = web100::GetLossCount();
#endif

  std::cout << "  lost: " << lost_packets << "\n";
  std::cout << "  sent: " << packets_sent << "\n";

  double loss_ratio = static_cast<double>(lost_packets) / packets_sent;
  if (loss_ratio > 1.0)
    return RESULT_INCONCLUSIVE;

  if (loss_ratio > config.loss_threshold)
    return RESULT_FAIL;

  return RESULT_PASS;
}

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

    assert(mbm_socket->Receive(strlen(READY)) == READY);

    Result result = RunCBR(mbm_socket.get(), server_config->config);
    switch (result) {
      case RESULT_PASS:
        std::cout << "PASS\n";
        mbm_socket->Send("PASS");
        break;
      case RESULT_FAIL:
        std::cout << "FAIL\n";
        mbm_socket->Send("FAIL");
        break;
      case RESULT_INCONCLUSIVE:
        std::cout << "INCONCLUSIVE\n";
        mbm_socket->Send("INCONCLUSIVE");
        break;
    }
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

  for (int i = 0; i < NUM_PORTS; ++i)
    used_port[i] = false;

  while (true) {
    scoped_ptr<mlab::ServerSocket> socket(
        mlab::ServerSocket::CreateOrDie(atoi(control_port)));

    socket->Select();
    socket->Accept();

    const Config config(socket->Receive(1024));

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
    socket->Send(ss.str());
  }

#ifdef USE_WEB100
  web100::Shutdown();
#endif  // USE_WEB100

  pthread_exit(NULL);
  return 0;
}

