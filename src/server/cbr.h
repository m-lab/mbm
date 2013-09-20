#ifndef SERVER_CBR_H
#define SERVER_CBR_H

namespace mlab {
class AcceptedSocket;
}

namespace mbm {

struct Config;

enum Result {
  RESULT_FAIL,
  RESULT_PASS,
  RESULT_INCONCLUSIVE,
  RESULT_ERROR,
  NUM_RESULTS
};

Result RunCBR(const mlab::AcceptedSocket* socket, const Config& config);

}  // namespace mbm

#endif  // SERVER_CBR_H
