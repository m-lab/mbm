#ifdef USE_WEB100

#include "common/web100.h"

#include <assert.h>

extern "C" {
#include <web100/web100.h>
}

#include "mlab/socket.h"

namespace web100 {
namespace {
web100_agent* agent = NULL;
web100_var* var = NULL;
web100_snapshot* before = NULL;
web100_snapshot* after = NULL;
}  // namespace

void Initialize(const mlab::Socket* socket) {
  agent = web100_attach(WEB100_AGENT_TYPE_LOCAL, NULL);
  if (agent == NULL) {
    web100_perror("web100");
    return;
  }

  web100_group* group = NULL;
  web100_agent_find_var_and_group(agent, "RemotePort", &group, &var);

  assert(web100_get_var_type(var) == WEB100_TYPE_UNSIGNED32);
  assert(web100_get_var_size(var) == sizeof(uint32_t));

  web100_connection* connection = web100_connection_from_socket(agent, socket->raw());

  before = web100_snapshot_alloc(group, connection);
  after = web100_snapshot_alloc(group, connection);
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
