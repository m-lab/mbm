#ifndef COMMON_WEB100_H
#define COMMON_WEB100_H

#ifndef USE_WEB100
#error USE_WEB100 should be defined if you expect this to work.
#endif  // !USE_WEB100

#include <stdint.h>
extern "C" {
#include <web100/web100.h>
}

namespace mlab {
class Socket;
}  // namespace mlab

namespace web100 {

class Agent {
  public: 
    Agent();
    ~Agent();
    web100_agent* get();

  private:
    web100_agent* agent_;
};

class Connection {
  public:
    Connection() {};
    Connection(const mlab::Socket* socket, web100_agent* agent);
    uint32_t PacketRetransCount();
    uint32_t RetransmitQueueSize();
    uint32_t ApplicationWriteQueueSize();
    uint32_t SampleRTT();
    uint32_t CurCwnd();
    uint32_t SndUna();
    uint32_t SndNxt();
    int id();
    void Start();
    void Stop();
    ~Connection();

  private:
    class Var;
    const mlab::Socket* socket_;
    web100_connection* connection;
    Var* pktsretrans;
    Var* curretxqueue;
    Var* curappwqueue;
    Var* samplertt;
    Var* curcwnd;
    Var* snduna;
    Var* sndnxt;
};


}  // namespace web100

#endif  // COMMON_WEB100_H
