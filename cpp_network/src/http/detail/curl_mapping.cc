#include "curl_mapping.h"

#include "http_constants.h"
#include "comm/tls.h"

#include <curl/curl.h>

#include <string>
#include <vector>


#include "comm/error.h"
#include "comm/options.h"

#include "comm/result.h"
#include "comm/url.h"
namespace cpp_network {
namespace http {

// Shared core types (canonical home: cpp_network::comm).
using cpp_network::comm::Error;
using cpp_network::comm::ErrorCode;
using cpp_network::comm::ErrorCodeToString;
using cpp_network::comm::Result;
using cpp_network::comm::Tls;
using cpp_network::comm::VerifyMode;
using cpp_network::comm::Options;
using cpp_network::comm::Proxy;
using cpp_network::comm::Url;



namespace detail {

namespace {

const char* MethodToString(Method method) {
  switch (method) {
    case Method::kGet:
      return HttpConstants::kMethodGet;
    case Method::kPost:
      return HttpConstants::kMethodPost;
    case Method::kPut:
      return HttpConstants::kMethodPut;
    case Method::kDelete:
      return HttpConstants::kMethodDelete;
    case Method::kPatch:
      return HttpConstants::kMethodPatch;
    case Method::kHead:
      return HttpConstants::kMethodHead;
    case Method::kOptions:
      return HttpConstants::kMethodOptions;
  }
  return HttpConstants::kMethodGet;
}

}  // namespace

ErrorCode EffectiveHardTimeout(const Request& req, const Options& options,
                               std::chrono::milliseconds* out) {
  if (req.timeout().has_value()) {
    *out = *req.timeout();
    return ErrorCode::kTotalTimeout;
  }
  if (options.total_timeout().count() > 0) {
    *out = options.total_timeout();
    return ErrorCode::kTotalTimeout;
  }
  if (options.write_timeout().count() > 0) {
    *out = options.write_timeout();
    return ErrorCode::kWriteTimeout;
  }
  return ErrorCode::kNone;
}

Error ApplyEasyOptions(CURL* easy, const Request& req, const Options& options,
                       curl_slist** headers_out) {
  *headers_out = nullptr;
  CURLcode rc;

  rc = curl_easy_setopt(easy, CURLOPT_URL, req.url().c_str());
  if (rc != CURLE_OK) {
    return Error(ErrorCode::kInvalidArgument,
                 std::string("curl: failed to set URL: ") + curl_easy_strerror(rc));
  }

  if (req.method() == Method::kHead) {
    // NOBODY makes libcurl send a real HEAD request and skip the body phase,
    // instead of relying on server-side HEAD semantics via CUSTOMREQUEST.
    rc = curl_easy_setopt(easy, CURLOPT_NOBODY, 1L);
  } else {
    rc = curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST,
                          MethodToString(req.method()));
  }
  if (rc != CURLE_OK) {
    return Error(ErrorCode::kInvalidArgument,
                 std::string("curl: failed to set method: ") + curl_easy_strerror(rc));
  }

  if (req.has_body()) {
    rc = curl_easy_setopt(easy, CURLOPT_POSTFIELDS, req.body().data());
    if (rc != CURLE_OK) {
      return Error(ErrorCode::kInvalidArgument,
                   std::string("curl: failed to set body: ") + curl_easy_strerror(rc));
    }
    rc = curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE,
                          static_cast<curl_off_t>(req.body().size()));
    if (rc != CURLE_OK) {
      return Error(ErrorCode::kInvalidArgument,
                   std::string("curl: failed to set body size: ") + curl_easy_strerror(rc));
    }
  }

  for (const auto& [name, value] : req.headers().fields()) {
    std::string line = name + ": " + value;
    curl_slist* item = curl_slist_append(*headers_out, line.c_str());
    if (!item) {
      return Error(ErrorCode::kOutOfMemory, "curl: failed to append header");
    }
    *headers_out = item;
  }
  if (*headers_out) {
    rc = curl_easy_setopt(easy, CURLOPT_HTTPHEADER, *headers_out);
    if (rc != CURLE_OK) {
      return Error(ErrorCode::kInvalidArgument,
                   std::string("curl: failed to set headers: ") + curl_easy_strerror(rc));
    }
  }

  // Timeouts.
  curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS,
                   static_cast<long>(options.connect_timeout().count()));
  // Hard timeout bounds the whole transfer (request-level > total >
  // write_timeout fallback); 0 = no hard cap.
  std::chrono::milliseconds hard_timeout{0};
  if (EffectiveHardTimeout(req, options, &hard_timeout) != ErrorCode::kNone) {
    rc = curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS,
                          static_cast<long>(hard_timeout.count()));
    if (rc != CURLE_OK) {
      return Error(ErrorCode::kInvalidArgument,
                   std::string("curl: failed to set total timeout: ") +
                       curl_easy_strerror(rc));
    }
  }
  // Read/write idle timeout approximated via libcurl's low-speed detection:
  // if no data moves for read_timeout seconds, the transfer times out.
  if (options.read_timeout().count() > 0) {
    long low_speed_time =
        (options.read_timeout().count() + 999) / 1000;  // round up to seconds
    if (low_speed_time < 1) low_speed_time = 1;
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, low_speed_time);
  }
  // Redirects.
  curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION,
                   options.follow_redirects() ? 1L : 0L);
  curl_easy_setopt(easy, CURLOPT_MAXREDIRS,
                   static_cast<long>(options.max_redirects()));

  // Interface / local address / local port.
  if (options.interface().has_value()) {
    rc = curl_easy_setopt(easy, CURLOPT_INTERFACE, options.interface()->c_str());
    if (rc != CURLE_OK) {
      return Error(ErrorCode::kInvalidArgument,
                   std::string("curl: failed to bind interface: ") + curl_easy_strerror(rc));
    }
  } else if (options.local_address().has_value()) {
    rc = curl_easy_setopt(easy, CURLOPT_INTERFACE, options.local_address()->c_str());
    if (rc != CURLE_OK) {
      return Error(ErrorCode::kInvalidArgument,
                   std::string("curl: failed to bind local address: ") + curl_easy_strerror(rc));
    }
  }
  if (options.local_port().has_value()) {
    rc = curl_easy_setopt(easy, CURLOPT_LOCALPORT,
                          static_cast<long>(*options.local_port()));
    if (rc != CURLE_OK) {
      return Error(ErrorCode::kInvalidArgument,
                   std::string("curl: failed to set local port: ") + curl_easy_strerror(rc));
    }
  }

  // Proxy.
  if (options.proxy().has_value()) {
    std::string proxy = options.proxy()->host + ":" +
                        std::to_string(options.proxy()->port);
    rc = curl_easy_setopt(easy, CURLOPT_PROXY, proxy.c_str());
    if (rc != CURLE_OK) {
      return Error(ErrorCode::kInvalidArgument,
                   std::string("curl: failed to set proxy: ") + curl_easy_strerror(rc));
    }
  }

  // Keep-alive: enable TCP keepalive probes with the configured idle window
  // (rounded up to whole seconds). HTTP keep-alive/connection reuse itself is
  // handled by libcurl's connection cache.
  if (options.keep_alive().count() > 0) {
    long keep_idle = (options.keep_alive().count() + 999) / 1000;
    if (keep_idle < 1) keep_idle = 1;
    rc = curl_easy_setopt(easy, CURLOPT_TCP_KEEPALIVE, 1L);
    if (rc == CURLE_OK) {
      rc = curl_easy_setopt(easy, CURLOPT_TCP_KEEPIDLE, keep_idle);
    }
    if (rc != CURLE_OK) {
      return Error(ErrorCode::kInvalidArgument,
                   std::string("curl: failed to set TCP keep-alive: ") +
                       curl_easy_strerror(rc));
    }
  }

  // TLS.
  const Tls& tls = options.tls();
  if (tls.verify_mode() == VerifyMode::kSkipVerification) {
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 0L);
  } else {
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
  }
  if (tls.ca_file().has_value()) {
    rc = curl_easy_setopt(easy, CURLOPT_CAINFO, tls.ca_file()->c_str());
    if (rc != CURLE_OK) {
      return Error(ErrorCode::kInvalidArgument,
                   std::string("curl: failed to set CA file: ") + curl_easy_strerror(rc));
    }
  } else if (tls.ca_pem().has_value()) {
    // Inline CA PEM flows via CURLOPT_CAINFO_BLOB (backend pins curl >= 7.77,
    // so the option is always available; no temp-file fallback). Blobs are
    // passed with CURL_BLOB_NOCOPY (NOMEMORY semantics): curl does not copy,
    // so the Tls-owned string must outlive the transfer, which it does.
    struct curl_blob ca_blob;
    ca_blob.data = const_cast<char*>(tls.ca_pem()->data());
    ca_blob.len = tls.ca_pem()->size();
    ca_blob.flags = CURL_BLOB_NOCOPY;
    rc = curl_easy_setopt(easy, CURLOPT_CAINFO_BLOB, &ca_blob);
    if (rc != CURLE_OK) {
      return Error(ErrorCode::kInvalidArgument,
                   std::string("curl: failed to set inline CA PEM: ") +
                       curl_easy_strerror(rc));
    }
  }
  if (tls.client_cert().has_value() && tls.client_key().has_value()) {
    // Inline PEM material (detected by Tls) is passed via *_BLOB options
    // (backend pins curl >= 7.71); anything else is treated as a file path.
    // Blobs are not copied by curl (CURL_BLOB_NOCOPY), so the Tls-owned
    // strings must outlive the transfer, which they do.
    const bool cert_is_pem = Tls::IsPemText(*tls.client_cert());
    const bool key_is_pem = Tls::IsPemText(*tls.client_key());

    if (cert_is_pem) {
      // curl_blob lives in easy.h; CURL_BLOB_NOCOPY tells libcurl not to
      // copy, so the Tls-owned string must outlive the transfer, which it
      // does. const_cast is safe: libcurl only reads the buffer.
      struct curl_blob cert_blob;
      cert_blob.data = const_cast<void*>(
          static_cast<const void*>(tls.client_cert()->data()));
      cert_blob.len = tls.client_cert()->size();
      cert_blob.flags = CURL_BLOB_NOCOPY;
      rc = curl_easy_setopt(easy, CURLOPT_SSLCERT_BLOB, &cert_blob);
      if (rc != CURLE_OK) {
        return Error(ErrorCode::kInvalidArgument,
                     std::string("curl: failed to set client cert blob: ") +
                         curl_easy_strerror(rc));
      }
    } else {
      rc = curl_easy_setopt(easy, CURLOPT_SSLCERT, tls.client_cert()->c_str());
      if (rc != CURLE_OK) {
        return Error(ErrorCode::kInvalidArgument,
                     std::string("curl: failed to set client cert: ") +
                         curl_easy_strerror(rc));
      }
    }
    if (key_is_pem) {
      struct curl_blob key_blob;
      key_blob.data = const_cast<void*>(
          static_cast<const void*>(tls.client_key()->data()));
      key_blob.len = tls.client_key()->size();
      key_blob.flags = CURL_BLOB_NOCOPY;
      rc = curl_easy_setopt(easy, CURLOPT_SSLKEY_BLOB, &key_blob);
      if (rc != CURLE_OK) {
        return Error(ErrorCode::kInvalidArgument,
                     std::string("curl: failed to set client key blob: ") +
                         curl_easy_strerror(rc));
      }
    } else {
      rc = curl_easy_setopt(easy, CURLOPT_SSLKEY, tls.client_key()->c_str());
      if (rc != CURLE_OK) {
        return Error(ErrorCode::kInvalidArgument,
                     std::string("curl: failed to set client key: ") +
                         curl_easy_strerror(rc));
      }
    }
  }
  if (tls.sni().has_value()) {
#ifdef CURLOPT_SNI_HOSTNAME
    rc = curl_easy_setopt(easy, CURLOPT_SNI_HOSTNAME, tls.sni()->c_str());
    if (rc != CURLE_OK) {
      return Error(ErrorCode::kInvalidArgument,
                   std::string("curl: failed to set SNI: ") + curl_easy_strerror(rc));
    }
#else
    (void)rc;
#endif
  }

  return Error();
}

void ApplyMultiOptions(CURLM* multi, const Options& options) {
  curl_multi_setopt(multi, CURLMOPT_MAX_HOST_CONNECTIONS,
                    static_cast<long>(options.max_connections_per_host()));
}

}  // namespace detail
}  // namespace http
}  // namespace cpp_network
