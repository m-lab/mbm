#ifndef COMMON_WEB100_H
#define COMMON_WEB100_H

#ifndef USE_WEB100
#error USE_WEB100 should be defined if you expect this to work.
#endif  // !USE_WEB100

#include <stdint.h>

namespace mlab {
class ClientSocket;
class ServerSocket;
}  // namespace mlab

namespace web100 {

void Initialize();
void CreateConnection(const mlab::ServerSocket* socket);
void CreateConnection(const mlab::ClientSocket* socket);
void Start();
void Stop();
uint32_t GetLossCount();
void Shutdown();

}  // namespace web100

#endif  // COMMON_WEB100_H
