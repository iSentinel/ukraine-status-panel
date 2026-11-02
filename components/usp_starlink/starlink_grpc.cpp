#include "starlink_grpc.h"

#include "esphome/core/hal.h"

#include "esp_heap_caps.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <nghttp2/nghttp2.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace esphome::usp_starlink {
namespace {

constexpr const char *RPC_PATH = "/SpaceX.API.Device.Device/Handle";
constexpr size_t MAX_GRPC_BODY = 64U * 1024U;

struct H2Context {
  int fd{-1};
  int32_t stream_id{-1};
  std::vector<uint8_t> request{};
  size_t request_offset{0};
  uint8_t *response{nullptr};
  size_t response_capacity{0};
  size_t response_size{0};
  bool response_overflow{false};
  int grpc_status{-1};
  bool stream_closed{false};
  bool dispatched{false};
};

void *h2_malloc(size_t size, void *) {
  return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void h2_free(void *ptr, void *) {
  if (ptr != nullptr) heap_caps_free(ptr);
}

void *h2_calloc(size_t count, size_t size, void *) {
  return heap_caps_calloc(
      count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void *h2_realloc(void *ptr, size_t size, void *) {
  return heap_caps_realloc(
      ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

bool deadline_reached(uint32_t deadline) {
  return static_cast<int32_t>(millis() - deadline) >= 0;
}

bool wait_fd(int fd, bool write, uint32_t deadline) {
  while (!deadline_reached(deadline)) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    timeval tv{0, 100000};
    const int rc = ::select(fd + 1, write ? nullptr : &fds,
                          write ? &fds : nullptr, nullptr, &tv);
    if (rc > 0 && FD_ISSET(fd, &fds)) return true;
    if (rc < 0 && errno != EINTR) return false;
  }
  return false;
}

int connect_socket(const std::string &host, uint16_t port,
                   uint32_t deadline) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  char service[8];
  std::snprintf(service, sizeof(service), "%u", unsigned(port));
  addrinfo *result = nullptr;
  if (::getaddrinfo(host.c_str(), service, &hints, &result) != 0) return -1;
  int connected = -1;
  for (auto *it = result; it != nullptr && connected < 0; it = it->ai_next) {
    const int fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (fd < 0) continue;
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    const int rc = ::connect(fd, it->ai_addr, it->ai_addrlen);
    if (rc == 0 || (errno == EINPROGRESS && wait_fd(fd, true, deadline))) {
      int error = 0;
      socklen_t size = sizeof(error);
      if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0)
        connected = fd;
    }
    if (connected != fd) ::close(fd);
  }
  ::freeaddrinfo(result);
  return connected;
}

bool send_all(int fd, const uint8_t *data, size_t size, uint32_t deadline) {
  size_t offset = 0;
  while (offset < size && !deadline_reached(deadline)) {
    const ssize_t sent = ::send(fd, data + offset, size - offset, 0);
    if (sent > 0) {
      offset += size_t(sent);
      continue;
    }
    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
      if (!wait_fd(fd, true, deadline)) return false;
      continue;
    }
    return false;
  }
  return offset == size;
}

ssize_t data_read_cb(nghttp2_session *, int32_t, uint8_t *buf, size_t length,
                     uint32_t *flags, nghttp2_data_source *, void *user_data) {
  auto *ctx = static_cast<H2Context *>(user_data);
  const size_t left = ctx->request.size() - ctx->request_offset;
  const size_t count = std::min(left, length);
  if (count != 0) {
    std::memcpy(buf, ctx->request.data() + ctx->request_offset, count);
    ctx->request_offset += count;
  }
  if (ctx->request_offset == ctx->request.size()) *flags |= NGHTTP2_DATA_FLAG_EOF;
  return ssize_t(count);
}

int on_data_cb(nghttp2_session *, uint8_t, int32_t stream_id,
               const uint8_t *data, size_t len, void *user_data) {
  auto *ctx = static_cast<H2Context *>(user_data);
  if (stream_id != ctx->stream_id) return 0;
  if (ctx->response_size > MAX_GRPC_BODY ||
      len > MAX_GRPC_BODY - ctx->response_size) {
    ctx->response_overflow = true;
    return 0;
  }
  if (ctx->response != nullptr) {
    if (len > ctx->response_capacity - ctx->response_size) {
      ctx->response_overflow = true;
      return 0;
    }
    std::memcpy(ctx->response + ctx->response_size, data, len);
  }
  ctx->response_size += len;
  return 0;
}

int on_header_cb(nghttp2_session *, const nghttp2_frame *frame,
                 const uint8_t *name, size_t name_len, const uint8_t *value,
                 size_t value_len, uint8_t, void *user_data) {
  auto *ctx = static_cast<H2Context *>(user_data);
  if (frame->hd.stream_id != ctx->stream_id) return 0;
  const std::string key(reinterpret_cast<const char *>(name), name_len);
  if (key != "grpc-status") return 0;
  const std::string text(reinterpret_cast<const char *>(value), value_len);
  ctx->grpc_status = std::atoi(text.c_str());
  return 0;
}

int on_stream_close_cb(nghttp2_session *, int32_t stream_id, uint32_t,
                       void *user_data) {
  auto *ctx = static_cast<H2Context *>(user_data);
  if (stream_id == ctx->stream_id) ctx->stream_closed = true;
  return 0;
}

bool flush_session(nghttp2_session *session, H2Context *ctx,
                   uint32_t deadline) {
  while (!deadline_reached(deadline)) {
    const uint8_t *data = nullptr;
    const ssize_t size = nghttp2_session_mem_send(session, &data);
    if (size < 0) return false;
    if (size == 0) return true;
    if (!send_all(ctx->fd, data, size_t(size), deadline)) return false;
    ctx->dispatched = true;
  }
  return false;
}

nghttp2_nv nv(const char *name, const std::string &value) {
  return {reinterpret_cast<uint8_t *>(const_cast<char *>(name)),
          reinterpret_cast<uint8_t *>(const_cast<char *>(value.data())),
          std::strlen(name), value.size(), NGHTTP2_NV_FLAG_NONE};
}

std::vector<uint8_t> grpc_frame(const std::vector<uint8_t> &protobuf) {
  std::vector<uint8_t> body(5U + protobuf.size(), 0);
  const uint32_t size = uint32_t(protobuf.size());
  body[1] = uint8_t(size >> 24U);
  body[2] = uint8_t(size >> 16U);
  body[3] = uint8_t(size >> 8U);
  body[4] = uint8_t(size);
  std::copy(protobuf.begin(), protobuf.end(), body.begin() + 5);
  return body;
}

RpcCallResult grpc_call(const std::string &host, uint16_t port,
                        uint32_t timeout_ms, const std::vector<uint8_t> &msg,
                        uint8_t *response, size_t response_capacity,
                        size_t *response_size) {
  RpcCallResult out{};
  const uint32_t started = millis();
  const uint32_t deadline = started + timeout_ms;
  H2Context ctx{};
  ctx.response = response;
  ctx.response_capacity = response_capacity;
  if (response_size != nullptr) *response_size = 0;
  ctx.fd = connect_socket(host, port, deadline);
  if (ctx.fd < 0) return out;
  ctx.request = grpc_frame(msg);

  nghttp2_session_callbacks *callbacks = nullptr;
  nghttp2_session *session = nullptr;
  if (nghttp2_session_callbacks_new(&callbacks) != 0 || callbacks == nullptr) {
    ::close(ctx.fd);
    return out;
  }
  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, on_data_cb);
  nghttp2_session_callbacks_set_on_header_callback(callbacks, on_header_cb);
  nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, on_stream_close_cb);
  nghttp2_mem memory{nullptr, h2_malloc, h2_free, h2_calloc, h2_realloc};
  if (nghttp2_session_client_new3(
          &session, callbacks, &ctx, nullptr, &memory) != 0) {
    nghttp2_session_callbacks_del(callbacks);
    ::close(ctx.fd);
    return out;
  }
  nghttp2_session_callbacks_del(callbacks);

  const std::string authority = host + ":" + std::to_string(port);
  const std::string method = "POST";
  const std::string scheme = "http";
  const std::string path = RPC_PATH;
  const std::string content_type = "application/grpc";
  const std::string te = "trailers";
  std::array<nghttp2_nv, 6> headers = {nv(":method", method), nv(":scheme", scheme),
      nv(":path", path), nv(":authority", authority),
      nv("content-type", content_type), nv("te", te)};
  nghttp2_data_provider provider{};
  provider.source.ptr = &ctx;
  provider.read_callback = data_read_cb;
  nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, nullptr, 0);
  ctx.stream_id = nghttp2_submit_request(session, nullptr, headers.data(),
                                         headers.size(), &provider, nullptr);
  if (ctx.stream_id < 0 || !flush_session(session, &ctx, deadline)) goto cleanup;

  while (!ctx.stream_closed && !deadline_reached(deadline)) {
    if (!wait_fd(ctx.fd, false, deadline)) break;
    uint8_t buffer[4096];
    const ssize_t count = ::recv(ctx.fd, buffer, sizeof(buffer), 0);
    if (count == 0) break;
    if (count < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
      break;
    }
    if (nghttp2_session_mem_recv(session, buffer, size_t(count)) < 0) break;
    if (!flush_session(session, &ctx, deadline)) break;
  }

  out.dispatched = ctx.dispatched;
  out.grpc_status = ctx.grpc_status;
  out.latency_ms = millis() - started;
  if (response_size != nullptr) *response_size = ctx.response_size;
  if (ctx.stream_closed && ctx.grpc_status == 0 &&
      !ctx.response_overflow) {
    out.outcome = RpcOutcome::OK;
  } else if (ctx.stream_closed && ctx.grpc_status > 0) {
    out.outcome = RpcOutcome::REMOTE_ERROR;
  } else if (ctx.dispatched) {
    out.outcome = RpcOutcome::RESULT_UNKNOWN;
  }
cleanup:
  nghttp2_session_del(session);
  ::close(ctx.fd);
  return out;
}

/** Read a uint64 varint without accepting a truncated or overflowing value. */
bool read_varint(const uint8_t *data, size_t size, size_t *offset,
                 uint64_t *value) {
  uint64_t result = 0;
  for (unsigned shift = 0; shift < 64 && *offset < size; shift += 7) {
    const uint8_t byte = data[(*offset)++];
    if (shift == 63 && byte > 1) return false;
    result |= uint64_t(byte & 0x7FU) << shift;
    if ((byte & 0x80U) == 0) {
      *value = result;
      return true;
    }
  }
  return false;
}

/** Validate field numbers before narrowing the protobuf key. */
bool read_key(const uint8_t *data, size_t size, size_t *offset,
              uint32_t *field, uint8_t *wire) {
  uint64_t key = 0;
  if (!read_varint(data, size, offset, &key) ||
      (key >> 3U) == 0 || (key >> 3U) > 0x1FFFFFFFU) return false;
  *field = uint32_t(key >> 3U);
  *wire = uint8_t(key & 7U);
  return true;
}

/** Skip supported unknown fields only when their entire value is present. */
bool skip_field(const uint8_t *data, size_t size, size_t *offset, uint8_t wire) {
  uint64_t len = 0;
  if (wire == 0) return read_varint(data, size, offset, &len);
  if (wire == 1 && size - *offset >= 8) { *offset += 8; return true; }
  if (wire == 5 && size - *offset >= 4) { *offset += 4; return true; }
  if (wire != 2 || !read_varint(data, size, offset, &len) ||
      len > size - *offset) return false;
  *offset += size_t(len);
  return true;
}

/** Visit all matching messages so repeated containers merge like protobuf. */
template<typename Visitor>
bool visit_messages(const uint8_t *data, size_t size, uint32_t wanted,
                    Visitor consume, bool *found) {
  size_t offset = 0;
  while (offset < size) {
    uint32_t field = 0;
    uint8_t wire = 0;
    if (!read_key(data, size, &offset, &field, &wire)) return false;
    if (field == wanted) {
      uint64_t len = 0;
      if (wire != 2 || !read_varint(data, size, &offset, &len) ||
          len > size - offset) return false;
      if (!consume(data + offset, size_t(len))) return false;
      offset += size_t(len);
      *found = true;
    } else if (!skip_field(data, size, &offset, wire)) {
      return false;
    }
  }
  return true;
}

/** Decode one complete, uncompressed unary gRPC response. */
bool extract_grpc_message(const uint8_t *body, size_t body_size,
                          const uint8_t **message, size_t *size) {
  if (body == nullptr || body_size < 5 || body[0] != 0) return false;
  const uint32_t len = (uint32_t(body[1]) << 24U) | (uint32_t(body[2]) << 16U) |
                       (uint32_t(body[3]) << 8U) | uint32_t(body[4]);
  if (len != body_size - 5U) return false;
  *message = body + 5;
  *size = len;
  return true;
}

/** Apply proto3 scalar defaults only inside a present, valid GPS message. */
bool read_gps_fields(const uint8_t *data, size_t size, DishGpsStatus *status) {
  size_t offset = 0;
  while (offset < size) {
    uint32_t field = 0;
    uint8_t wire = 0;
    if (!read_key(data, size, &offset, &field, &wire)) return false;
    if (field == 1 || field == 2 || field == 4) {
      uint64_t value = 0;
      if (wire != 0 || !read_varint(data, size, &offset, &value)) return false;
      if (field == 1) status->gps_valid = value != 0;
      if (field == 2) {
        if (value > UINT32_MAX) return false;
        status->gps_sats = uint32_t(value);
      }
      if (field == 4) status->inhibit_gps = value != 0;
    } else if (!skip_field(data, size, &offset, wire)) {
      return false;
    }
  }
  return true;
}

/** Publish a status only after all required containers and fields validate. */
bool parse_gps_status_bytes(const uint8_t *body, size_t body_size,
                            DishGpsStatus *status) {
  if (status == nullptr) return false;
  const uint8_t *root = nullptr;
  size_t root_size = 0;
  if (!extract_grpc_message(body, body_size, &root, &root_size)) return false;
  DishGpsStatus parsed{};
  parsed.grpc_status = status->grpc_status;
  bool has_dish = false, has_gps = false;
  auto read_gps = [&](const uint8_t *data, size_t size) {
    return read_gps_fields(data, size, &parsed);
  };
  auto read_dish = [&](const uint8_t *data, size_t size) {
    return visit_messages(data, size, 1015, read_gps, &has_gps);
  };
  if (!visit_messages(root, root_size, 2004, read_dish, &has_dish) ||
      !has_dish || !has_gps) return false;
  parsed.parsed = true;
  *status = parsed;
  return true;
}

bool parse_gps_status(const std::vector<uint8_t> &body,
                      DishGpsStatus *status) {
  return parse_gps_status_bytes(body.data(), body.size(), status);
}

}  // namespace

RpcCallResult dish_get_status(const std::string &host, uint16_t port,
                              uint32_t timeout_ms, DishGpsStatus *status) {
  const std::vector<uint8_t> request{0xE2, 0x3E, 0x00};
  auto *response = static_cast<uint8_t *>(heap_caps_malloc(
      MAX_GRPC_BODY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (response == nullptr) return {};
  size_t response_size = 0;
  auto result = grpc_call(
      host, port, timeout_ms, request, response, MAX_GRPC_BODY,
      &response_size);
  if (status != nullptr) status->grpc_status = result.grpc_status;
  if (result.outcome == RpcOutcome::OK && status != nullptr &&
      !parse_gps_status_bytes(response, response_size, status)) {
    result.outcome = RpcOutcome::PROTOCOL_ERROR;
  }
  heap_caps_free(response);
  return result;
}

RpcCallResult dish_set_gps_inhibit(const std::string &host, uint16_t port,
                                   uint32_t timeout_ms, bool inhibit) {
  const std::vector<uint8_t> request{0xF2, 0x7D, 0x02, 0x08,
                                     uint8_t(inhibit ? 1 : 0)};
  return grpc_call(
      host, port, timeout_ms, request, nullptr, 0, nullptr);
}

}  // namespace esphome::usp_starlink
