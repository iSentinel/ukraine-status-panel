#include "artwork_request_plan.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace usp_spotify_artwork;
constexpr auto A = "https://i.scdn.co/image/albumA";
constexpr auto B = "https://i.scdn.co/image/albumB";

void origins() {
  assert(allowed_url(A));
  for (auto bad : {"", "http://i.scdn.co/image/a",
      "https://i.scdn.co.evil/image/a", "https://i.scdn.co@127.0.0.1/image/a",
      "https://127.0.0.1/image/a", "https://i.scdn.co:443/image/a",
      "https://i.scdn.co/image/", "https://i.scdn.co/image/a?b=1",
      "https://i.scdn.co/image/%2e", "https://i.scdn.co/image/../a",
      "https://i.scdn.co/image/a#b", "https://i.scdn.co/image/a\nb"}) {
    assert(!allowed_url(bad));
  }
  assert(!allowed_url(std::string(A) + std::string(257, 'a')));
  std::string nul(A);
  nul.push_back(0);
  nul.push_back('x');
  assert(!allowed_url(nul));
}

void lifecycle() {
  RequestPlan plan;
  assert(!plan.begin(0).ticket);
  assert(plan.select(A));
  auto first = plan.begin(0).ticket;
  assert(first && !plan.begin(1).ticket && !plan.select(A));
  assert(plan.select(B));
  auto second = plan.begin(2).ticket;
  assert(second && first != second);
  assert(!plan.finish(first, true, 3, 0) && !plan.ready());
  assert(plan.select(A));
  auto third = plan.begin(4).ticket;
  assert(third && third != first);
  assert(!plan.finish(first, true, 5, 0));
  assert(!plan.finish(second, true, 5, 0));
  assert(plan.finish(third, true, 6, 0) && plan.ready());
  assert(!plan.finish(third, true, 6, 0) && !plan.begin(7).ticket);
  assert(!plan.select(A) && plan.ready());
  plan.clear();
  assert(!plan.ready() && !plan.begin(8).ticket);
}

void exponential_retry() {
  RequestPlan plan;
  assert(plan.select(A));
  uint32_t now = 0;
  const uint32_t expected[] = {5000, 10000, 20000, 40000, 80000, 160000, 300000};
  for (uint32_t delay : expected) {
    auto request = plan.begin(now);
    assert(request.ticket);
    assert(plan.finish(request.ticket, false, now, 5000));
    assert(!plan.begin(now + delay - 1).ticket);
    now += delay;
  }
  auto capped = plan.begin(now);
  assert(capped.ticket);
  assert(plan.finish(capped.ticket, false, now, 5000));
  assert(!plan.begin(now + 299999).ticket);
  assert(plan.begin(now + 300000).ticket);
}

void permanent_failure() {
  RequestPlan plan;
  plan.select(A);
  auto request = plan.begin(100);
  assert(request.ticket);
  assert(plan.finish(request.ticket, false, 100, 5000, true));
  assert(!plan.begin(UINT32_MAX).ticket);
  assert(!plan.select(A));
  assert(!plan.begin(200).ticket);
  assert(plan.select(B));
  assert(plan.begin(201).ticket);
}

void owned_request_and_wrap() {
  RequestPlan plan;
  plan.select(A);
  auto request = plan.begin(0);
  assert(request.ticket && std::string_view(request.url.data()) == A);
  plan.select(B);
  assert(std::string_view(request.url.data()) == A);
  assert(!plan.finish(request.ticket, true, 1, 0));

  RequestPlan wrap;
  wrap.select(A);
  auto first = wrap.begin(0xfffffff0).ticket;
  assert(wrap.finish(first, false, 0xfffffff0, 0));
  assert(!wrap.begin(983).ticket);
  auto second = wrap.begin(984).ticket;
  assert(second && first != second);
}

int main() {
  origins();
  lifecycle();
  exponential_retry();
  permanent_failure();
  owned_request_and_wrap();
  puts("SPOTIFY_ARTWORK_REQUEST_PLAN_NATIVE=PASS exponential5_300 permanent_per_url no_network no_pixels");
}
