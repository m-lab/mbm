#include "common/time.h"

#include <time.h>

#include "common/constants.h"

namespace mbm {
uint64_t GetTimeNS() {
  struct timespec time;
#if defined(OS_FREEBSD)
  clock_gettime(CLOCK_MONOTONIC_PRECISE, &time);
#else
  clock_gettime(CLOCK_MONOTONIC_RAW, &time);
#endif
  return time.tv_sec * NS_PER_SEC + time.tv_nsec;
}
}
