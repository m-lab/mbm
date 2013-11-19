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
web100_agent* agent = NULL;

class Var {
 public:
  explicit Var(const std::string& name, web100_connection* connection)
      : var_(NULL),
        group_(NULL),
        before_(NULL),
        after_(NULL),
        started(false),
        stopped(false) {
    assert(agent != NULL);
    assert(connection != NULL);

    if (web100_agent_find_var_and_group(agent, "PktsRetrans", &group_, &var_) !=
        WEB100_ERR_SUCCESS) {
      web100_perror("web100");
      perror("sys");
      assert(false);
    }

    before_ = web100_snapshot_alloc(group_, connection);
    assert(before_ != NULL);

    after_ = web100_snapshot_alloc(group_, connection);
    assert(after_ != NULL);
  }

  ~Var() {
    web100_snapshot_free(after_);
    web100_snapshot_free(before_);
  }

  void start() {
    assert(!started);
    assert(!stopped);
    if (web100_snap(before_) != WEB100_ERR_SUCCESS) {
      web100_perror("web100");
      perror("sys");
      assert(false);
    }
    started = true;
  }

  void stop() {
    assert(!stopped);
    if (web100_snap(after_) != WEB100_ERR_SUCCESS) {
      web100_perror("web100");
      perror("sys");
      assert(false);
    }
    stopped = true;
  }

  // TODO(dominic): assume counter32 or gauge32 types. should be more general.
  uint32_t delta() const {
    assert(started && stopped);
    uint32_t delta;
    if (web100_delta_any(var_, after_, before_, &delta) != WEB100_ERR_SUCCESS) {
      web100_perror("web100");
      perror("sys");
      assert(false);
    }
    return delta;
  }

  uint32_t get() const {
    assert(stopped);
    uint32_t result;
    if (web100_snap_read(var_, after_, &result) != WEB100_ERR_SUCCESS) {
      web100_perror("web100");
      perror("sys");
      assert(false);
    }
    return result;
  }

 private:
  web100_var* var_;
  web100_group* group_;
  web100_snapshot* before_;
  web100_snapshot* after_;

  bool started;
  bool stopped;
};

Var* pktsretrans = NULL;
Var* curretxqueue = NULL;
Var* curappwqueue = NULL;
Var* samplertt = NULL;
}  // namespace

void Initialize() {
  agent = web100_attach(WEB100_AGENT_TYPE_LOCAL, NULL);
  if (agent == NULL) {
    web100_perror("web100");
    perror("sys");
    assert(false);
  }
}

void CreateConnection(const mlab::Socket* socket) {
  web100_connection* connection =
      web100_connection_from_socket(agent, socket->raw());
  if (connection == NULL) {
    web100_perror("web100");
    perror("sys");
    assert(false);
  }

  pktsretrans = new Var("PktsRetrans", connection);
  curretxqueue = new Var("CurRetxQueue", connection);
  curappwqueue = new Var("CurAppWQueue", connection);
  samplertt = new Var("SampleRTT", connection);
}

void Start() {
  pktsretrans->start();
}

void Stop() {
  pktsretrans->stop();
  curretxqueue->stop();
  curappwqueue->stop();
  samplertt->stop();
}

uint32_t PacketRetransCount() {
  return pktsretrans->delta();
}

uint32_t RetransmitQueueSize() {
  return curretxqueue->get();
}

uint32_t ApplicationWriteQueueSize() {
  return curappwqueue->get();
}

uint32_t SampleRTT() {
  return samplertt->get();
}

void Shutdown() {
  delete samplertt;
  delete curretxqueue;
  delete curappwqueue;
  delete pktsretrans;

  web100_detach(agent);
}
}  // namespace web100

#endif  // USE_WEB100
