#ifndef SERVER_CBR_H
#define SERVER_CBR_H

#include "common/result.h"

namespace mlab {
class AcceptedSocket;
}

namespace mbm {
struct Config;

Result RunCBR(const mlab::AcceptedSocket* socket, const Config& config);
}  // namespace mbm

#endif  // SERVER_CBR_H
