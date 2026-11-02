#include "usp_spotify_auth_form.h"
#include "usp_spotify_auth_return.h"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

using usp_spotify_auth_form::Form;

/** Decode into a fixed caller-owned buffer, then erase it after each request. */
void valid() {
  std::array<char, 4097> buffer{};
  const std::string begin = "client_id=abc123&redirect_uri=https%3A%2F%2Freturn.test%2Fcb";
  std::copy(begin.begin(), begin.end(), buffer.begin());
  Form form;
  assert(form.parse(buffer.data(), begin.size(), buffer.size()) && form.begin_shape());
  assert(form.client_id == "abc123" && form.redirect_uri == "https://return.test/cb");
  const std::string code = "state=MTExMTExMTExMTExMTExMQ&code=opaque%2B%2F%25%26%3D+value";
  std::copy(code.begin(), code.end(), buffer.begin());
  assert(form.parse(buffer.data(), code.size(), buffer.size()) && form.callback_shape());
  assert(form.code == "opaque+/%&= value");
  usp_spotify_pkce::zeroize(buffer.data(), buffer.size());
  for (char value : buffer) assert(value == 0);
}

/** No truncation, control injection, alias keys, or duplicate callback fields. */
void invalid() {
  const std::string cases[] = {
    "state=a&state=b&code=x", "code=x&%73tate=y",
    "state=x&code=%00", "state=x&code=%0A", "state=x&code=%FF",
    "state=x&code=%G1", "state=x&code=%0", "state=x&code=%",
    "state=x&unknown=y", "state=x&code=x&", "&state=x&code=x",
    "state=x&&code=x", "state", "state=x&code=x&code=y",
    std::string("state=x&code=a\0b", 16)
  };
  for (auto body : cases) {
    const auto length = body.size(); body.push_back(0);
    Form form;
    assert(!form.parse(body.data(), length, body.size()));
    assert(form.fields == 0 && form.code.empty() && form.state.empty());
  }
  std::array<char, 4100> body{}; body.fill('a');
  Form form;
  assert(!form.parse(body.data(), 4097, body.size()));
  assert(!form.parse(body.data(), 10, 10));
  assert(!form.parse(nullptr, 1, 2));
  assert(form.parse(nullptr, 0, 1) && form.fields == 0);
}

/** Shape admission is independent of parser success and requires exact fields. */
void shape() {
  for (auto body : {std::string("state=x&code=x&error=no"),
                    std::string("state=&code=x"), std::string("state=x&code="),
                    std::string("client_id=x"), std::string("client_id=x&redirect_uri=")}) {
    const auto length=body.size();body.push_back(0);Form form;
    assert(form.parse(body.data(),length,body.size()));
    assert(!form.callback_shape() && !form.begin_shape());
  }
}
int main() {
  valid();invalid();shape();
  std::cout << "SPOTIFY_AUTH_FORM_PREFLIGHT=PASS in_place bounded4096 strict_fields duplicates controls invalid_percent shapes erase no_network\n";
}
