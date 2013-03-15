#include <iostream>
#include <map>
#include <string>
#include <sstream>

#include "common/config.h"
#include "common/scoped_ptr.h"
#include "mlab/server_socket.h"

Config config;

double RunCBR(uint32_t cbr) {
  std::cout << "Running CBR at " << cbr << " for " << config.time_per_test <<
               " seconds\n";
  return cbr / 1000.0 - 0.3;
}

int main(int argc, const char* argv[]) {
  scoped_ptr<mlab::ServerSocket> socket(mlab::ServerSocket::CreateOrDie(4242));
  socket->Select();
  socket->Accept();

  // TODO(dominic): Do we need to get the range from the client or can this be a
  // server setting?
  config.FromString(socket->Receive(sizeof(config)));

  std::cout << "Setting CBR range to " << config.low_cbr_kb_s << " -> " <<
               config.high_cbr_kb_s << "\n";

  // The |loss_threshold| allows us to find the CBR that gives loss of a certain
  // percentage.
  double loss_threshold = 0.0;

  if (RunCBR(config.low_cbr_kb_s) > loss_threshold) {
    std::cerr << "CBR of " << config.low_cbr_kb_s << " is already too lossy.\n";
    std::cerr << "Please provide a lower range to try.\n";
    return 1;
  }

  if (RunCBR(config.high_cbr_kb_s) < loss_threshold) {
    std::cerr << "CBR of " << config.high_cbr_kb_s << " is not lossy.\n";
    std::cerr << "Please provide a higher range to try.\n";
    return 1;
  }

  // Binary search between the low and high CBR rates to determine where we
  // cross the loss threshold.
  uint32_t low = config.low_cbr_kb_s;
  uint32_t high = config.high_cbr_kb_s;
  std::map<uint32_t, double> cbr_loss_map;
  while (low < high) {
    uint32_t test_cbr = low + (high - low) / 2;
    double loss_rate = RunCBR(test_cbr);
    cbr_loss_map.insert(std::make_pair(test_cbr, loss_rate));
    if (loss_rate < loss_threshold)
      low = test_cbr;
    else if (loss_rate > loss_threshold)
      high = test_cbr;
    else
      break;
  }

  // Report the results.
  std::cout << "CBR, loss_rate\n";
  for (std::map<uint32_t, double>::const_iterator it = cbr_loss_map.begin();
       it != cbr_loss_map.end(); ++it) {
    std::cout << it->first << ", " << it->second << "\n";
  }
  return 0;
}
