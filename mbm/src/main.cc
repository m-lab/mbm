#include "mlab/ns.h"

#include <iostream>

int main(int argc, const char* argv[]) {
  const mlab::Host& host = mlab::ns::GetHostForTool("mobiperf");
  std::cout << "Found " << host.original_hostname << "\n";
  return 0;
}
