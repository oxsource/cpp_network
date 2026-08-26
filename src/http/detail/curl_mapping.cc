#include "curl_mapping.h"

#include <curl/curl.h>

#include <fcntl.h>
#include <unistd.h>

#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace cpp_network {
namespace http {
namespace detail {

namespace {

const char* MethodToString(Method method) {
  switch (method) {
    case Method::kGet:
      return "GET";
    case Method::kPost:
      return "POST";
    case Method::kPut:
      return "PUT";
    case Method::kDelete:
      return "DELETE";
    case Method::kPatch:
      return "PATCH";
    case Method::kHead:
      return "HEAD";
    case Method::kOptions:
      return "OPTIONS";
  }
  return "GET";
}

}  // namespace

namespace {

// Some system libcurl builds (e.g. macOS) expose the *_BLOB options in their
// headers but reject them at runtime with CURLE_FAILED_INIT. As a fallback,
// inline PEM material is materialized into a temp file (cached per content)
// and passed via path-based options instead. Temp files persist for the
// process lifetime so paths stay valid across transfers.
const char* MaterializePem(const std::string& pem) {
  static std::mutex mutex;
  static std::map<std::string, std::string> cache;
  std::lock_guard<std::mutex> lock(mutex);
  auto it = cache.find(pem);
  if (it != cache.end()) {
    return it->second.c_str();
  }

  const char* tmpdir = std::getenv("TMPDIR");
  std::string tmpl = (tmpdir != nullptr && tmpdir[0] != '\0' ? std::string(tmpdir) : std::string("/tmp")) +
                     "/netlib_pem_XXXXXX";
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back('\0');
  int fd = ::mkstemp(buf.data());
  if (fd < 0) {
    return nullptr;
  }
  ssize_t written = ::write(fd, pem.data(), pem.size());
  ::close(fd);
  if (written < 0 || static_cast<std::size_t>(written) != pem.size()) {
    ::unlink(buf.data());
    return nullptr;
  }
  // The map node keeps a stable copy; the returned pointer remains valid.
  return cache.emplace(pem, std::string(buf.data())).first->second.c_str();
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

  for (const auto& [name, value] : req.headers()) {
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
    bool applied = false;
#ifdef CURLOPT_CAINFO_BLOB
    // Needs runtime curl >= 7.77 even when the header declares the option.
    rc = curl_easy_setopt(easy, CURLOPT_CAINFO_BLOB, tls.ca_pem()->c_str());
    applied = (rc == CURLE_OK);
#endif
    if (!applied) {
      const char* ca_path = MaterializePem(*tls.ca_pem());
      if (ca_path == nullptr) {
        return Error(ErrorCode::kInvalidArgument,
                     "failed to materialize inline CA PEM for curl without "
                     "CAINFO_BLOB support");
      }
      rc = curl_easy_setopt(easy, CURLOPT_CAINFO, ca_path);
      if (rc != CURLE_OK) {
        return Error(ErrorCode::kInvalidArgument,
                     std::string("curl: failed to set CA file: ") +
                         curl_easy_strerror(rc));
      }
    }
  }
  if (tls.client_cert().has_value() && tls.client_key().has_value()) {
    // Inline PEM material is detected by the "-----BEGIN" marker and passed
    // via *_BLOB options (runtime curl >= 7.71) with a temp-file fallback;
    // anything else is treated as a file path.
    const bool cert_is_pem =
        tls.client_cert()->find("-----BEGIN") != std::string::npos;
    const bool key_is_pem =
        tls.client_key()->find("-----BEGIN") != std::string::npos;

    bool cert_applied = false;
    bool key_applied = false;
    if (cert_is_pem) {
#ifdef CURLOPT_SSLCERT_BLOB
      struct curl_blob cert_blob;
      cert_blob.data = tls.client_cert()->data();
      cert_blob.len = tls.client_cert()->size();
      cert_blob.flags = CURLBLOB_NOMEMORY;
      rc = curl_easy_setopt(easy, CURLOPT_SSLCERT_BLOB, &cert_blob);
      cert_applied = (rc == CURLE_OK);
#endif
    }
    if (key_is_pem) {
#ifdef CURLOPT_SSLKEY_BLOB
      struct curl_blob key_blob;
      key_blob.data = tls.client_key()->data();
      key_blob.len = tls.client_key()->size();
      key_blob.flags = CURLBLOB_NOMEMORY;
      rc = curl_easy_setopt(easy, CURLOPT_SSLKEY_BLOB, &key_blob);
      key_applied = (rc == CURLE_OK);
#endif
    }

    const char* cert_path =
        cert_is_pem ? MaterializePem(*tls.client_cert())
                    : tls.client_cert()->c_str();
    const char* key_path = key_is_pem ? MaterializePem(*tls.client_key())
                                      : tls.client_key()->c_str();
    if (cert_path == nullptr || key_path == nullptr) {
      return Error(ErrorCode::kInvalidArgument,
                   "failed to materialize inline client certificate/key PEM "
                   "for curl without BLOB support");
    }
    if (!cert_applied) {
      rc = curl_easy_setopt(easy, CURLOPT_SSLCERT, cert_path);
      if (rc != CURLE_OK) {
        return Error(ErrorCode::kInvalidArgument,
                     std::string("curl: failed to set client cert: ") +
                         curl_easy_strerror(rc));
      }
    }
    if (!key_applied) {
      rc = curl_easy_setopt(easy, CURLOPT_SSLKEY, key_path);
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
