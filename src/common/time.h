#ifndef COMMON_TIME_H_
#define COMMON_TIME_H_

#include <stdint.h>

#include <string>

namespace mbm {
uint64_t GetTimeNS();
std::string GetTestTimeStr();
void NanoSleepX(uint64_t sec, uint64_t ns);

}  // namespace mbm

#endif  // COMMON_TIME_H_
