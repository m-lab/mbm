#ifndef COMMON_RESULT_H_
#define COMMON_RESULT_H_

namespace mbm {
enum Result {
  RESULT_FAIL,
  RESULT_PASS,
  RESULT_INCONCLUSIVE,
  RESULT_ERROR,
  NUM_RESULTS
};

extern const char* kResultStr[NUM_RESULTS];
}  // namespace mbm

#endif  // COMMON_RESULT_H_
