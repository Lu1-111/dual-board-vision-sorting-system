#ifndef EFFICIENTNET_RESULT_DEADLINES_HPP
#define EFFICIENTNET_RESULT_DEADLINES_HPP
#include <cstdint>
namespace efficientnet {
// New input must be fresh. Slow but complete inference can be displayed as history.
constexpr uint64_t InputFreshnessMs=5000;
constexpr uint64_t CompletedResultDeadlineMs=15000;
inline bool freshInput(uint64_t ageMs){return ageMs<=InputFreshnessMs;}
inline bool publishableResult(uint64_t ageMs){return ageMs<=CompletedResultDeadlineMs;}
inline bool historicalResult(uint64_t ageMs){return ageMs>InputFreshnessMs;}
}
#endif
