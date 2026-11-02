#include "artwork_http_body.h"
#include <array>
#include <cassert>
#include <cstdio>
using namespace usp_spotify_artwork_http;
void body() {
  std::array<uint8_t, 8> bytes{};
  Collector c(bytes.data(),bytes.size(),0xfffffff0);
  assert(c.header("Content-Type"," image/JPEG "));
  assert(c.header("Content-Length","4"));
  assert(c.header("content-length","4"));
  assert(c.header("Content-Encoding","identity"));
  assert(c.append("ab",2,0xfffffff1) && c.append("cd",2,16));
  assert(c.complete(200,true,17) && c.size()==4);
  assert(!c.complete(200,false,17) && !c.complete(302,true,17));
  assert(!c.complete(200,true,7984));
  assert(!c.header("content-length","5"));
  assert(!c.complete(200,true,18));
}
void invalid() {
  std::array<uint8_t, 8> bytes{};
  for (auto value : {"-1","+1","99999999999999","4294967296","9","3x",""}) {
    Collector c(bytes.data(),bytes.size(),0);
    assert(!c.header("content-length",value));
  }
  Collector c(bytes.data(),bytes.size(),0);
  assert(c.header("content-type","image/jpeg"));
  assert(!c.append("123456789",9,1));
  assert(!c.complete(200,true,2));
  Collector d(bytes.data(),bytes.size(),0);
  assert(!d.header("content-encoding","gzip"));
  Collector e(bytes.data(),bytes.size(),0);
  assert(!e.header("content-type","text/html"));
  Collector f(bytes.data(),bytes.size(),0);
  assert(!f.append(nullptr,1,1));
}
void retry() {
  Cooldown c;
  c.header("3600"); assert(c.delay==3600000 && !c.blocked);
  c.header("1"); assert(c.delay==3600000 && !c.blocked);
  c.header("86400"); assert(c.delay==86400000 && !c.blocked);
  c.header("86401"); assert(c.blocked);
  Cooldown d; d.header("Tue, 1 Jan"); assert(d.blocked);
  Cooldown e; e.header("0"); assert(e.delay==1000);
}
int main() {
  body(); invalid(); retry();
  puts("ARTWORK_BODY_NATIVE=PASS no_network body_cap timeout wrap retry_after_full");
}
