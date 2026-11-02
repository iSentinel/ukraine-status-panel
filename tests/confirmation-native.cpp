#include "usp_confirmation.h"
#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
  usp_confirmation::Confirmation request;
  assert(!request.pending(0));
  assert(!request.activate(0));
  assert(request.pending(9999));
  assert(request.activate(9999));
  assert(!request.pending(9999));
  assert(!request.activate(20000));
  assert(!request.pending(30000));
  assert(!request.activate(30000));
  request.clear();
  assert(!request.activate(30001));
  request.clear();
  assert(!request.activate(UINT32_MAX - 20));
  assert(request.pending(10));
  assert(request.activate(10));
  assert(!request.pending(10));
  std::puts("CONFIRMATION_NATIVE=PASS first second expiry cancel millis_wrap");
}
