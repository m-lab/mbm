#ifndef SERVER_CBR_H
#define SERVER_CBR_H

namespace mlab {
class Socket;
}

namespace mbm {

struct Config;

enum Result {
  RESULT_FAIL,
  RESULT_PASS,
  RESULT_INCONCLUSIVE
};

Result RunCBR(const mlab::Socket* socket, const Config& config);

}  // namespace mbm

#endif  // SERVER_CBR_H
