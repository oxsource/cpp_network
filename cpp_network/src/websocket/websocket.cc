// WebSocket session implementation over the bundled libwebsockets
// (specs/006). This translation unit owns the sync pump: every public call
// drives lws_service on the calling thread and translates callbacks into
// the four-state machine documented in specs/006-libwebsockets-wss/
// data-model.md.
//
// Channel wiring (research.md D3/D5):
//   * wss://  -> lws client with LCCSCF_USE_SSL; trust anchors and client
//     certificates flow via the memory APIs (client_ssl_ca_mem etc.), so no
//     temporary files are ever involved.
//   * ws://   -> plain socket; TLS options are skipped entirely (spec
//     clarification 2026-08-27 Q2: silent ignore).
//
// Threading model matches the spec assumption of one driving thread per
// connection: the lws context is created per session and its callbacks run
// on whichever thread invokes the public API.
#include "ws/websocket.h"

#include <chrono>
#include <cctype>
#include <deque>
#include <functional>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>

#include <arpa/inet.h>
#include <libwebsockets.h>

namespace cpp_network {
namespace ws {

// ws is a peer module but shares the comm core types (Options/Result/Tls).
using cpp_network::comm::Error;
using cpp_network::comm::ErrorCode;
using cpp_network::comm::Options;
using cpp_network::comm::Result;
using cpp_network::comm::Tls;
using cpp_network::comm::VerifyMode;

namespace {

constexpr char kProtocolName[] = "cpp-network-ws";

// Protocol constants: URL schemes, transport defaults and RFC 6455 limits.
constexpr char kWsScheme[] = "ws://";
constexpr char kWssScheme[] = "wss://";
constexpr char kSchemeSeparator[] = "://";
constexpr char kPortDigits[] = "0123456789";
constexpr char kDefaultPath[] = "/";
constexpr int kWsDefaultPort = 80;
constexpr int kWssDefaultPort = 443;
// RFC 6455 §5.5.1 caps the close-reason body at 123 bytes.
constexpr size_t kMaxCloseReasonBytes = 123;
constexpr int kRxBufferBytes = 8192;

// Error/status strings surfaced to callers (transport protocol vocabulary).
constexpr char kErrScheme[] = "WebSocket URLs must be ws:// or wss://";
constexpr char kErrMissingHost[] = "missing host in WebSocket URL";
constexpr char kErrHostMissing[] = "missing host";
constexpr char kErrInvalidPort[] = "invalid port";
constexpr char kErrPortRange[] = "port out of range";
constexpr char kErrCertFiles[] = "failed to read client certificate/key files";
constexpr char kErrLwsContextFailed[] = "lws_create_context failed";
constexpr char kErrLwsWritePrefix[] = "lws_write failed (";
constexpr char kErrClientConnectFailed[] = "client connect failed";
constexpr char kErrConnectDispatch[] = "connect dispatch failed for host: ";
constexpr char kErrConnectTimeout[] = "websocket connect timeout";
constexpr char kErrWriteTimeout[] = "websocket write timeout";
constexpr char kErrReadTimeout[] = "websocket read timeout";
constexpr char kErrClosedByPeer[] = "connection closed by peer";
constexpr char kErrConnectionClosed[] = "connection closed";
constexpr char kErrConcurrentSend[] = "concurrent Send is not supported";

enum class WsState { kConnecting, kOpen, kClosing, kClosed };

ErrorCode ClassifyConnectFailure(const char* reason) {
  // lws exposes a free-text reason; classify pragmatically so callers get
  // HTTP-parity codes for verification failures (the actionable class).
  std::string r = reason != nullptr ? reason : "";
  for (auto& c : r) {
    c = static_cast<char>(
        std::tolower(static_cast<unsigned char>(c)));
  }
  // X509_V_ERR text from OpenSSL surfaces as "server's cert didn't look
  // good" through lws, hence the extra token set.
  static const char* kVerifyMarkers[] = {
      "verify", "self-signed", "issuer", "certificate",
      "x509_v_err", "didn't look good",
  };
  for (const char* marker : kVerifyMarkers) {
    if (r.find(marker) != std::string::npos) {
      return ErrorCode::kCertificateVerificationFailed;
    }
  }
  static const char* kTlsHandshakeMarkers[] = {"ssl", "handshake"};
  for (const char* marker : kTlsHandshakeMarkers) {
    if (r.find(marker) != std::string::npos) {
      return ErrorCode::kTlsHandshakeFailed;
    }
  }
  return ErrorCode::kConnectionRefused;
}

// File-scope session object handed to lws as the context user. Public Impl
// wraps this; keeping lws-facing state outside the private nested type keeps
// the callback free-function simple while respecting the header contract.
struct WsSession {
  struct lws_context* context = nullptr;

  WsState state = WsState::kConnecting;
  struct lws* wsi = nullptr;
  Error failure;
  uint16_t peer_close_code = 0;
  std::string peer_close_reason;

  // Outbound: one message at a time (single-thread pump discipline).
  // Canonical lws partial-write pattern: whole payload kept here WITHOUT
  // prefix; every writable resumes by building [LWS_PRE | tail-window]
  // scratch so libwebsockets can fragment/reframe however it needs to.
  std::vector<uint8_t> tx_payload;
  size_t tx_sent = 0;
  bool tx_active = false;
  bool tx_is_text = false;
  Error tx_failure;   // write-phase failures land here for the caller

  // Per-session timeout budget copied from Options at Connect() time.
  std::chrono::milliseconds write_timeout_ms{10000};
  std::chrono::milliseconds read_timeout_ms{30000};

  // Close handshake armed by Close(); executed via lws_close_reason() from
  // the WRITABLE callback (the documented entry point), after which lib-
  // websockets drives the two-way close exchange itself.
  bool close_pending = false;
  uint16_t close_code = 1000;
  std::string close_reason;

  // Inbound: fragmented frames accumulate until a final fragment completes
  // one whole message (FR-005); finished messages wait in this queue.
  std::deque<std::pair<std::vector<uint8_t>, bool>> rx_queue;
  std::vector<uint8_t> rx_assembling;
  bool rx_first_chunk_seen = false;
  bool rx_is_text = false;

  // TLS material must outlive every transfer; held here and passed through
  // lws_context_creation_info memory pointers.
  std::string ca_pem;
  std::string ca_path;   // filepath-form anchor outlives context creation
  std::string client_cert_pem;
  std::string client_key_pem;

  void Fail(const Error& error) {
    if (failure.ok()) failure = error;
    state = WsState::kClosed;
  }
};

int WsCallback(struct lws* wsi, enum lws_callback_reasons reason, void*,
               void* in, size_t len) {
  auto* session =
      static_cast<WsSession*>(lws_context_user(lws_get_context(wsi)));
  if (session == nullptr) return 0;
  switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
      session->state = WsState::kOpen;
      break;
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
      session->Fail(
          Error(ClassifyConnectFailure(static_cast<const char*>(in)),
                in != nullptr
                    ? std::string(static_cast<const char*>(in), len)
                    : std::string(kErrClientConnectFailed)));
      break;
    case LWS_CALLBACK_CLIENT_WRITEABLE: {
      if (session->close_pending) {
        // Per lws-ws-close.h: lws_close_reason annotates the reason and the
        // callback must RETURN NONZERO to actually request the closure —
        // libwebsockets then emits the close frame (code + aux reason) and
        // runs the two-way handshake itself.
        const unsigned char* aux =
            session->close_reason.empty()
                ? nullptr
                : reinterpret_cast<const unsigned char*>(
                      session->close_reason.data());
        lws_close_reason(
            session->wsi,
            static_cast<enum lws_close_status>(session->close_code),
            const_cast<unsigned char*>(aux), session->close_reason.size());
        session->close_pending = false;
        return 1;  // non-zero: request connection closure
      }
      if (!session->tx_active || session->state != WsState::kOpen) break;
      // One lws_write per writable covering ALL remaining bytes: observed
      // empirically (Phase 4, build-matrix.md notes) that manual windowing
      // around NO_FIN/CONTINUATION corrupts lws' internal frame pipeline,
      // while libwebsockets fragments/reframes a single large write
      // correctly. A short write (<remaining) is tolerated: it means the
      // kernel/lws drained less than offered — resume with the remainder
      // on the NEXT writable instead of arming an immediate re-entry.
      const size_t remaining =
          session->tx_payload.size() - session->tx_sent;
      const size_t window = remaining;
      const bool first = session->tx_sent == 0;
      const bool final_slice =
          (session->tx_sent + window) == session->tx_payload.size();
      int flags = session->tx_is_text ? LWS_WRITE_TEXT : LWS_WRITE_BINARY;
      if (!first) flags |= LWS_WRITE_CONTINUATION;
      if (!final_slice) flags |= LWS_WRITE_NO_FIN;

      // Build [LWS_PRE | window] scratch for THIS call only; note the
      // window never contains bytes libwebsockets already absorbed, since
      // tx_sent advances strictly by the value lws_write returns.
      std::vector<uint8_t> scratch(window + LWS_PRE);
      if (window > 0) {
        std::memcpy(scratch.data() + LWS_PRE,
                    session->tx_payload.data() + session->tx_sent, window);
      }
      const ssize_t written =
          lws_write(wsi, scratch.data() + LWS_PRE, window,
                    static_cast<lws_write_protocol>(flags));
      if (written < 0) {
        session->tx_failure = Error(
            ErrorCode::kInternalError,
            std::string(kErrLwsWritePrefix) + std::to_string(written) + ")");
        session->Fail(session->tx_failure);
        break;
      }
      session->tx_sent += static_cast<size_t>(written);
      if (session->tx_sent >= session->tx_payload.size()) {
        session->tx_active = false;
        break;
      }
      // Partial offer: drop remainder silently onto the next writable the
      // library schedules once its outgoing queue drains.
      lws_callback_on_writable(wsi);
      break;
    }
    case LWS_CALLBACK_CLIENT_RECEIVE: {
      if (lws_is_first_fragment(wsi)) {
        session->rx_assembling.clear();
        session->rx_first_chunk_seen = true;
        session->rx_is_text = lws_frame_is_binary(wsi) == 0;
      }
      const auto* begin = static_cast<const uint8_t*>(in);
      session->rx_assembling.insert(session->rx_assembling.end(), begin,
                                    begin + len);
      if (lws_is_final_fragment(wsi)) {
        std::vector<uint8_t> complete;
        complete.swap(session->rx_assembling);
        session->rx_first_chunk_seen = false;
        session->rx_queue.emplace_back(std::move(complete),
                                       session->rx_is_text);
      }
      break;
    }
    case LWS_CALLBACK_CLOSED:
      // fallthrough
    case LWS_CALLBACK_CLIENT_CLOSED:
      if (session->state != WsState::kClosed) {
        session->state = WsState::kClosed;
      }
      break;
    case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
      // `in` points at the close code + optional reason body.
      if (len >= 2) {
        uint16_t raw = 0;
        std::memcpy(&raw, in, sizeof(raw));
        session->peer_close_code = ntohs(raw);
        if (len > 2) {
          session->peer_close_reason.assign(
              static_cast<const char*>(in) + 2, len - 2);
        }
      }
      break;
    default:
      break;
  }
  return 0;
}

const struct lws_protocols kProtocols[] = {
    {kProtocolName, WsCallback, sizeof(void*) /* session userdata */,
     kRxBufferBytes, 0, nullptr, 0},
    {nullptr, nullptr, 0, 0, 0, nullptr, 0},
};

struct UrlParts {
  bool secure = false;
  std::string host;
  int port = 0;
  std::string path = kDefaultPath;
};

Result<UrlParts> ParseWsUrl(const std::string& url) {
  UrlParts parts;
  if (url.rfind(kWssScheme, 0) == 0) {
    parts.secure = true;
    parts.port = kWssDefaultPort;
  } else if (url.rfind(kWsScheme, 0) == 0) {
    parts.port = kWsDefaultPort;
  } else {
    return Result<UrlParts>::Err(
        Error(ErrorCode::kInvalidArgument, kErrScheme));
  }
  std::string rest = url.substr(url.find(kSchemeSeparator) + 3);
  if (rest.empty() || rest[0] == '/' || rest[0] == ':') {
    return Result<UrlParts>::Err(
        Error(ErrorCode::kInvalidArgument, kErrMissingHost));
  }
  const auto slash = rest.find('/');
  const std::string authority =
      slash == std::string::npos ? rest : rest.substr(0, slash);
  if (slash != std::string::npos) parts.path = rest.substr(slash);

  const auto colon = authority.rfind(':');
  if (colon != std::string::npos &&
      authority.find(']') == std::string::npos) {
    const std::string port_text = authority.substr(colon + 1);
    if (port_text.empty() ||
        port_text.find_first_not_of(kPortDigits) != std::string::npos) {
      return Result<UrlParts>::Err(
          Error(ErrorCode::kInvalidArgument, kErrInvalidPort));
    }
    const long parsed = std::strtol(port_text.c_str(), nullptr, 10);
    if (parsed <= 0 || parsed > 65535) {
      return Result<UrlParts>::Err(
          Error(ErrorCode::kInvalidArgument, kErrPortRange));
    }
    parts.port = static_cast<int>(parsed);
    parts.host = authority.substr(0, colon);
  } else {
    parts.host = authority;
  }
  if (parts.host.empty()) {
    return Result<UrlParts>::Err(
        Error(ErrorCode::kInvalidArgument, kErrHostMissing));
  }
  return Result<UrlParts>::Ok(std::move(parts));
}

bool ReadWholeFile(const std::string& path, std::string* out) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) return false;
  std::ostringstream buffer;
  buffer << file.rdbuf();
  *out = buffer.str();
  return true;
}

}  // namespace

// Header contract keeps Impl opaque; it simply borrows the file-scope
// session object created in Connect().
struct WebSocket::Impl {
  ~Impl() {
    if (session != nullptr && session->context != nullptr) {
      lws_context_destroy(session->context);
      session->context = nullptr;
    }
  }

  std::shared_ptr<WsSession> session;
};

Result<WebSocket> WebSocket::Connect(const std::string& url,
                                     const Options& options) {
  auto parsed = ParseWsUrl(url);
  if (!parsed.ok()) return Result<WebSocket>::Err(parsed.error());
  const UrlParts parts = parsed.TakeValue();

  // TLS configuration: validated once up front for both channels; only the
  // secure channel actually consumes it (silent-ignore rule for ws://).
  const Tls& tls = options.tls();
  const Result<void> validation = tls.Validate();
  if (!validation.ok()) return Result<WebSocket>::Err(validation.error());

  auto session = std::make_shared<WsSession>();
  lws_context_creation_info info{};
  memset(&info, 0, sizeof(info));
  info.port = CONTEXT_PORT_NO_LISTEN;
  info.protocols = kProtocols;
  info.user = session.get();
  info.fd_limit_per_thread = 64;
  session->write_timeout_ms = options.write_timeout();
  session->read_timeout_ms = options.read_timeout();

  unsigned ssl_flags = 0;
  if (parts.secure) {
    info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    ssl_flags |= LCCSCF_USE_SSL;

    if (tls.verify_mode() == VerifyMode::kSkipVerification) {
      ssl_flags |= LCCSCF_ALLOW_SELFSIGNED |
                   LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK |
                   LCCSCF_ALLOW_INSECURE;
    } else if (tls.ca_pem().has_value()) {
      // Direct memory anchor. NOTE: libwebsockets' mem path decodes a
      // SINGLE PEM block; callers with concatenated stores should prefer
      // SetCaFile so the filepath variant below is used instead.
      session->ca_pem = *tls.ca_pem();
    } else if (tls.ca_file().has_value()) {
      // Filepath form supports multi-certificate bundles (OpenSSL
      // load_verify_locations semantics on the lws side), which is how the
      // merged Android system store gets consumed.
      session->ca_path = *tls.ca_file();
    }
    if (!session->ca_pem.empty()) {
      info.client_ssl_ca_mem = session->ca_pem.c_str();
      info.client_ssl_ca_mem_len = session->ca_pem.size();
    } else if (!session->ca_path.empty()) {
      info.client_ssl_ca_filepath = session->ca_path.c_str();
    }
    // No explicit anchor: rely on lws/OpenSSL system-store defaults, the
    // same posture the HTTP client takes by default.

    if (tls.client_cert().has_value() && tls.client_key().has_value()) {
      if (Tls::IsPemText(*tls.client_cert())) {
        session->client_cert_pem = *tls.client_cert();
        session->client_key_pem = *tls.client_key();
      } else if (!ReadWholeFile(*tls.client_cert(),
                                &session->client_cert_pem) ||
                 !ReadWholeFile(*tls.client_key(),
                                &session->client_key_pem)) {
        return Result<WebSocket>::Err(
            Error(ErrorCode::kInvalidArgument, kErrCertFiles));
      }
      info.client_ssl_cert_mem = session->client_cert_pem.c_str();
      info.client_ssl_cert_mem_len = session->client_cert_pem.size();
      info.client_ssl_key_mem = session->client_key_pem.c_str();
      info.client_ssl_key_mem_len = session->client_key_pem.size();
    }
  }
  lws_set_log_level(LLL_ERR | LLL_WARN, nullptr);

  session->context = lws_create_context(&info);
  if (session->context == nullptr) {
    return Result<WebSocket>::Err(
        Error(ErrorCode::kInternalError, kErrLwsContextFailed));
  }

  lws_client_connect_info cci{};
  memset(&cci, 0, sizeof(cci));
  cci.context = session->context;
  cci.address = parts.host.c_str();
  cci.port = parts.port;
  cci.path = parts.path.c_str();
  cci.host = parts.host.c_str();
  cci.origin = parts.host.c_str();
  cci.protocol = kProtocolName;
  cci.local_protocol_name = kProtocolName;
  cci.ssl_connection = ssl_flags;

  session->state = WsState::kConnecting;
  session->wsi = lws_client_connect_via_info(&cci);
  if (session->wsi == nullptr) {
    session->Fail(Error(ErrorCode::kDnsResolutionFailed,
                        kErrConnectDispatch + parts.host));
  }

  const auto deadline =
      std::chrono::steady_clock::now() +
      std::max<std::chrono::milliseconds>(options.connect_timeout(),
                                          std::chrono::milliseconds(1));
  while (session->state == WsState::kConnecting &&
         std::chrono::steady_clock::now() < deadline) {
    lws_service(session->context, 20);
  }
  if (session->state == WsState::kConnecting) {
    session->Fail(Error(ErrorCode::kConnectionTimeout, kErrConnectTimeout));
  }
  if (!session->failure.ok()) {
    return Result<WebSocket>::Err(session->failure);
  }

  auto impl = std::make_shared<WebSocket::Impl>();
  impl->session = std::move(session);
  return Result<WebSocket>::Ok(WebSocket(std::move(impl)));
}


// Pumps the event loop until pred() fires or the deadline passes. Returns
// false on deadline; failure/side-state remains in the session for callers.
bool PumpUntil(WsSession* session, const std::chrono::steady_clock::time_point& deadline,
               const std::function<bool()>& pred) {
  while (!pred()) {
    if (std::chrono::steady_clock::now() >= deadline) return false;
    if (session->context == nullptr || !session->failure.ok()) return true;
    lws_service(session->context, 10);
    if (session->state == WsState::kClosed && !pred()) {
      // Peer tore the connection down mid-wait.
      if (!session->failure.ok()) return true;
      if (session->peer_close_code != 0) {
        Error e(ErrorCode::kConnectionClosed, kErrClosedByPeer);
        e.set_close_code(session->peer_close_code);
        e.set_close_reason(session->peer_close_reason);
        session->Fail(e);
        return true;
      }
      session->Fail(Error(ErrorCode::kConnectionClosed,
                          kErrConnectionClosed));
      return true;
    }
  }
  return true;
}

// Shared closed-session error composer for Send/Receive/Close paths.
Error ClosedStateError(WsSession* session, const char* what) {
  if (session->peer_close_code != 0) {
    Error e(ErrorCode::kConnectionClosed,
            std::string(what) + ": " + kErrClosedByPeer);
    e.set_close_code(session->peer_close_code);
    e.set_close_reason(session->peer_close_reason);
    return e;
  }
  if (!session->failure.ok()) return session->failure;
  return Error(ErrorCode::kInvalidState, std::string(what));
}

bool WebSocket::IsOpen() const {
  return impl_->session->state == WsState::kOpen;
}

Result<void> WebSocket::Send(const WsMessage& msg) {
  auto* session = impl_->session.get();
  if (session->state != WsState::kOpen) {
    return Result<void>::Err(ClosedStateError(session, "Send"));
  }
  // Zero-length payloads are legal frames (edge-case decision); they flow
  // through the same slice writer with chunk==0.
  if (session->tx_active) {
    // Re-entrant send while a previous message is still being written can
    // only happen from another thread; the single-thread discipline makes
    // this a contract violation rather than a race we solve here.
    return Result<void>::Err(Error(ErrorCode::kInvalidState,
                                   kErrConcurrentSend));
  }
  session->tx_payload = msg.data;
  session->tx_sent = 0;
  session->tx_is_text = msg.is_text;
  session->tx_active = true;
  lws_callback_on_writable(session->wsi);

  const auto deadline =
      std::chrono::steady_clock::now() +
      std::max<std::chrono::milliseconds>(session->write_timeout_ms,
                                          std::chrono::milliseconds(1));
  PumpUntil(session, deadline, [&] {
    return !session->tx_active || !session->failure.ok();
  });
  session->tx_active = false;  // partial-write abandonment on timeout
  if (!session->tx_failure.ok()) {
    return Result<void>::Err(session->tx_failure);
  }
  if (!session->failure.ok()) {
    return Result<void>::Err(session->failure);
  }
  if (session->state != WsState::kOpen ||
      session->tx_sent < session->tx_payload.size()) {
    return Result<void>::Err(
        Error(ErrorCode::kWriteTimeout, kErrWriteTimeout));
  }
  return Result<void>::Ok();
}

Result<WsMessage> WebSocket::Receive() {
  auto* session = impl_->session.get();
  if (session->state != WsState::kOpen) {
    return Result<WsMessage>::Err(ClosedStateError(session, "Receive"));
  }
  const auto deadline =
      std::chrono::steady_clock::now() +
      std::max<std::chrono::milliseconds>(session->read_timeout_ms,
                                          std::chrono::milliseconds(1));
  PumpUntil(session, deadline,
            [&] { return !session->rx_queue.empty() ||
                         session->state != WsState::kOpen; });
  if (!session->rx_queue.empty()) {
    auto [data, is_text] = std::move(session->rx_queue.front());
    session->rx_queue.pop_front();
    WsMessage m;
    m.data = std::move(data);
    m.is_text = is_text;
    return Result<WsMessage>::Ok(std::move(m));
  }
  if (!session->failure.ok()) {
    return Result<WsMessage>::Err(session->failure);
  }
  if (session->state != WsState::kOpen) {
    return Result<WsMessage>::Err(ClosedStateError(session, "Receive"));
  }
  return Result<WsMessage>::Err(Error(ErrorCode::kReadTimeout,
                                      kErrReadTimeout));
}

Result<void> WebSocket::Close(WsCloseCode code, const std::string& reason) {
  auto* session = impl_->session.get();
  if (session->state == WsState::kClosed ||
      session->state == WsState::kClosing) {
    // FR-007: repeated Close is idempotent and never raises to the caller.
    return Result<void>::Ok();
  }
  if (session->state != WsState::kOpen) {
    return Result<void>::Err(ClosedStateError(session, "Close"));
  }

  // RFC 6455 §5.5.1 caps the reason at 123 bytes.
  session->close_code = static_cast<uint16_t>(code);
  session->close_reason = reason.substr(0, kMaxCloseReasonBytes);
  session->state = WsState::kClosing;
  session->close_pending = true;
  lws_callback_on_writable(session->wsi);

  const auto deadline =
      std::chrono::steady_clock::now() +
      std::max<std::chrono::milliseconds>(session->write_timeout_ms,
                                          std::chrono::milliseconds(1));
  PumpUntil(session, deadline, [&] {
    return session->state == WsState::kClosed || !session->failure.ok();
  });
  // Whether or not the peer acknowledged in time, the session is terminal
  // from the caller's perspective (FR-009 close wait bound).
  session->state = WsState::kClosed;
  return Result<void>::Ok();
}

WebSocket::~WebSocket() = default;

}  // namespace ws
}  // namespace cpp_network
