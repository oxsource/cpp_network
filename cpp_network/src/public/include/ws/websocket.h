#ifndef CPP_NETWORK_WS_WEBSOCKET_H_
#define CPP_NETWORK_WS_WEBSOCKET_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "comm/error.h"
#include "comm/export.h"
#include "comm/options.h"
#include "comm/result.h"

namespace cpp_network {
namespace ws {

// A single WebSocket message: raw payload bytes plus the frame type
// (text vs binary). Zero-length payloads are legal; Receive never
// synthesizes empty messages on its own.
struct CPP_NETWORK_WS_EXPORT WsMessage {
  std::vector<uint8_t> data;
  bool is_text = true;
};

// Standard close codes (RFC 6455 §7.4.1). Codes 3000-4999 are
// application-defined and travel through Error.close_code() as integers;
// this enum covers the well-known range used in Close().
enum class CPP_NETWORK_WS_EXPORT WsCloseCode : uint16_t {
  kNormal = 1000,
  kGoingAway = 1001,
  kProtocolError = 1002,
  kUnsupportedData = 1003,
  kNoStatus = 1005,
  kAbnormal = 1006,
};

// Synchronous WebSocket client session over ws:// and wss://.
//
// Channel rules (specs/006 FR-001):
//   * Only "ws://" and "wss://" URLs are accepted; anything else fails fast
//     with kInvalidArgument before any network activity.
//   * TLS configuration applies to wss:// only. On a plaintext ws://
//     connection any TLS settings are silently ignored (documented decision).
//   * Default verification rejects self-signed certificates exactly like the
//     HTTP client; inject a trust anchor via Tls::Builder to accept them.
//
// Threading: one WebSocket must be driven by one thread at a time; multiple
// connections are independent.
class CPP_NETWORK_WS_EXPORT WebSocket {
 public:
  // Blocks until the opening handshake completes or the configured connect
  // timeout expires. Errors carry kInvalidArgument / kConnectionTimeout /
  // kCertificateVerificationFailed / ... per contracts/websocket-api.md.
  static comm::Result<WebSocket> Connect(const std::string& url,
                                         const comm::Options& options);

  bool IsOpen() const;

  // Sends one complete message atomically; segmentation is handled below the
  // API surface. Requires OPEN state.
  comm::Result<void> Send(const WsMessage& msg);

  // Blocks until one full inbound message arrives. Keep-alive pings are
  // answered transparently and never surface here. After a peer-initiated
  // close the returned error carries the peer close code/reason.
  comm::Result<WsMessage> Receive();

  // Performs the closing handshake, waiting at most the configured timeout
  // for the peer's acknowledgement. Repeated calls are idempotent.
  comm::Result<void> Close(WsCloseCode code, const std::string& reason);

  ~WebSocket();

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
  explicit WebSocket(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
};

}  // namespace ws
}  // namespace cpp_network

#endif  // CPP_NETWORK_WS_WEBSOCKET_H_
