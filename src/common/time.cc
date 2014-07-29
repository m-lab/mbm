#include "common/time.h"

#include <time.h>
#include <assert.h>

#include <iostream>
#include <sstream>

#include "common/constants.h"

namespace mbm {

uint64_t GetTimeNS() {
  struct timespec time;
#if defined(OS_FREEBSD)
  clock_gettime(CLOCK_MONOTONIC_PRECISE, &time);
#else
  clock_gettime(CLOCK_MONOTONIC_RAW, &time);
#endif
  return static_cast<uint64_t>(time.tv_sec) * NS_PER_SEC + time.tv_nsec;
}

std::string GetTestTimeStr() {
  struct timespec time;
  clock_gettime(CLOCK_REALTIME, &time);
  struct tm* time_tm = gmtime(&time.tv_sec);
  char buffer[100];
  strftime(buffer, 100, "%Y%m%dT%T.", time_tm);
  std::stringstream ss;
  ss << buffer << time.tv_nsec << 'Z';
  return ss.str();
}

void NanoSleepX(uint64_t sec, uint64_t ns) {
  struct timespec sleep_req = {sec, ns};
  struct timespec sleep_rem;
  int slept = nanosleep(&sleep_req, &sleep_rem);
  while (slept == -1) {
    assert(errno == EINTR);  
    slept = nanosleep(&sleep_rem, &sleep_rem);
  }
}

} // namespace mbm
