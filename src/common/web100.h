#ifndef COMMON_WEB100_H
#define COMMON_WEB100_H

#ifndef USE_WEB100
#error USE_WEB100 should be defined if you expect this to work.
#endif  // !USE_WEB100

#include <stdint.h>

namespace mlab {
class Socket;
}  // namespace mlab

namespace web100 {

void Initialize();

void CreateConnection(const mlab::Socket* socket);

void Start();
void Stop();

uint32_t PacketRetransCount();
uint32_t UnackedBytes();
float RTTSeconds();

void Shutdown();

}  // namespace web100

#endif  // COMMON_WEB100_H
