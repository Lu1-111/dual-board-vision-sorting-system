#include "result_deadlines.hpp"
#include <cstdio>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(){
    CHECK(efficientnet::freshInput(5000));
    CHECK(!efficientnet::freshInput(5001));
    CHECK(!efficientnet::freshInput(10000));
    CHECK(efficientnet::publishableResult(10000));
    CHECK(efficientnet::historicalResult(10000));
    CHECK(!efficientnet::historicalResult(5000));
    CHECK(efficientnet::historicalResult(5001));
    CHECK(efficientnet::publishableResult(15000));
    CHECK(!efficientnet::publishableResult(15001));
    CHECK(!efficientnet::publishableResult(UINT64_MAX));
    std::puts("RESULT_DEADLINES_PASS: input5s; completed15s; history after5s");
}
