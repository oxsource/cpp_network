#include "curl_mapping.h"

#include <curl/curl.h>

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

Error ApplyEasyOptions(CURL* easy, const Request& req, const Options& options,
                       curl_slist** headers_out) {
  *headers_out = nullptr;
  CURLcode rc;

  rc = curl_easy_setopt(easy, CURLOPT_URL, req.url().c_str());
  if (rc != CURLE_OK) {
    return Error(ErrorCode::kInvalidArgument,
                 std::string("curl: failed to set URL: ") + curl_easy_strerror(rc));
  }

  rc = curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, MethodToString(req.method()));
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
  // Total timeout bounds the whole transfer.
  curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS,
                   static_cast<long>(options.total_timeout().count()));
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
#ifdef CURLOPT_CAINFO_BLOB
    rc = curl_easy_setopt(easy, CURLOPT_CAINFO_BLOB, tls.ca_pem()->c_str());
#else
    rc = CURLE_FAILED_INIT;
#endif
    if (rc != CURLE_OK) {
      return Error(ErrorCode::kInvalidArgument,
                   std::string("curl: failed to set CA blob (needs curl >= 7.77): ") +
                       curl_easy_strerror(rc));
    }
  }
  if (tls.client_cert().has_value() && tls.client_key().has_value()) {
    rc = curl_easy_setopt(easy, CURLOPT_SSLCERT, tls.client_cert()->c_str());
    if (rc != CURLE_OK) {
      return Error(ErrorCode::kInvalidArgument,
                   std::string("curl: failed to set client cert: ") + curl_easy_strerror(rc));
    }
    rc = curl_easy_setopt(easy, CURLOPT_SSLKEY, tls.client_key()->c_str());
    if (rc != CURLE_OK) {
      return Error(ErrorCode::kInvalidArgument,
                   std::string("curl: failed to set client key: ") + curl_easy_strerror(rc));
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
