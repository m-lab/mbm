#ifdef USE_WEB100

#include "common/web100.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
extern "C" {
#include <web100/web100.h>
}

#include <string>

#include "common/constants.h"
#include "mlab/socket.h"

namespace web100 {
namespace {
web100_connection* connection = NULL;
web100_agent* agent = NULL;
web100_snapshot* before = NULL;
web100_snapshot* after = NULL;

class Var {
 public:
  explicit Var(const std::string& name)
      : var_(NULL), group_(NULL) {
    int found =
        web100_agent_find_var_and_group(agent, "PktsRetrans", &group_, &var_);
    if (found != WEB100_ERR_SUCCESS) {
      web100_perror("web100");
      perror("sys");
      assert(false);
    }
  }

  // TODO(dominic): Add 'read' function here.

  web100_var* var() const { return var_; }
  web100_group* group() const { return group_; }

 private:
  web100_var* var_;
  web100_group* group_;
};

Var* pktsretrans = NULL;
Var* curretxqueue = NULL;
Var* currappwqueue = NULL;
Var* sampledrtt = NULL;

void AllocateSnapshots(web100_connection* connection) {
  // Only for the packets retrans var for now.
  before = web100_snapshot_alloc(pktsretrans->group(), connection);
  after = web100_snapshot_alloc(pktsretrans->group(), connection);
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

  pktsretrans = new Var("PktsRetrans");
  curretxqueue = new Var("CurRetxQueue");
  currappwqueue = new Var("CurAppWQueue");
  sampledrtt = new Var("SampledRTT");
}

void CreateConnection(const mlab::Socket* socket) {
  connection = web100_connection_from_socket(agent, socket->raw());
  if (connection == NULL) {
    web100_perror("web100");
    perror("sys");
    assert(false);
  }
  AllocateSnapshots(connection);
}

void Start() {
  assert(before != NULL);
  web100_snap(before);
}

void Stop() {
  assert(after != NULL);
  web100_snap(after);
}

uint32_t PacketRetransCount() {
  assert(before != NULL);
  assert(after != NULL);
  uint32_t result;
  web100_delta_any(pktsretrans->var(), after, before, &result);
  return result;
}

uint32_t UnackedBytes() {
  // TODO(dominic): This should probably use the snapshot from Stop.
  uint32_t retxqueue;
  if (web100_raw_read(curretxqueue->var(), connection, &retxqueue) !=
      WEB100_ERR_SUCCESS) {
    web100_perror("web100");
    perror("sys");
    assert(false);
  }

  uint32_t appwqueue;
  if (web100_raw_read(currappwqueue->var(), connection, &appwqueue) !=
      WEB100_ERR_SUCCESS) {
    web100_perror("web100");
    perror("sys");
    assert(false);
  }

  return appwqueue + retxqueue;
}

float RTTSeconds() {
  // TODO(dominic): This should probably use the snapshot from Stop.
  uint32_t rtt_ms;
  if (web100_raw_read(sampledrtt->var(), connection, &rtt_ms) !=
      WEB100_ERR_SUCCESS) {
    web100_perror("web100");
    perror("sys");
    assert(false);
  }

  return static_cast<float>(rtt_ms) / MS_PER_SEC;
}

void Shutdown() {
  delete sampledrtt;
  delete curretxqueue;
  delete currappwqueue;
  delete pktsretrans;

  web100_snapshot_free(after);
  web100_snapshot_free(before);
  web100_detach(agent);
}

}  // namespace web100

#endif  // USE_WEB100
