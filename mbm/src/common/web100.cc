#ifdef USE_WEB100

#include "common/web100.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>

extern "C" {
#include <web100/web100.h>
}

#include "mlab/client_socket.h"
#include "mlab/server_socket.h"

namespace web100 {
namespace {

web100_agent* agent = NULL;
web100_var* var = NULL;
web100_group* group = NULL;
web100_snapshot* before = NULL;
web100_snapshot* after = NULL;

web100_connection* CreateConnection(int sock_fd) {
  web100_connection* connection = web100_connection_from_socket(agent, sock_fd);
  if (connection == NULL) {
    web100_perror("web100");
    perror("sys");
    assert(false);
  }
  return connection;
}

void AllocateSnapshots(web100_connection* connection) {
  before = web100_snapshot_alloc(group, connection);
  after = web100_snapshot_alloc(group, connection);
  assert(before != NULL);
  assert(after != NULL);
}

}  // namespace

void Initialize() {
  agent = web100_attach(WEB100_AGENT_TYPE_LOCAL, NULL);
  if (agent == NULL) {
    web100_perror("web100");
    perror("sys");
    assert(false);
  }

  int found = web100_agent_find_var_and_group(agent, "PktsRetrans", &group, &var);
  if (found != WEB100_ERR_SUCCESS) {
    web100_perror("web100");
    perror("sys");
    assert(false);
  }

  assert(web100_get_var_type(var) == WEB100_TYPE_COUNTER32);
  assert(web100_get_var_size(var) == sizeof(uint32_t));
}

void CreateConnection(const mlab::ServerSocket* socket) {
  AllocateSnapshots(CreateConnection(socket->client_raw()));
}

void CreateConnection(const mlab::ClientSocket* socket) {
  AllocateSnapshots(CreateConnection(socket->raw()));
}

void Start() {
  assert(before != NULL);
  web100_snap(before);
}

void Stop() {
  assert(after != NULL);
  web100_snap(after);
}

uint32_t GetLossCount() {
  assert(before != NULL);
  assert(after != NULL);
  uint32_t result;
  web100_delta_any(var, before, after, &result);
  return result;
}

void Shutdown() {
  web100_snapshot_free(after);
  web100_snapshot_free(before);
  web100_detach(agent);
}

}  // namespace web100

#endif  // USE_WEB100
