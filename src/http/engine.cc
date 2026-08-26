#include "engine.h"

#include <curl/curl.h>

#include <string>
#include <vector>

#include "detail/curl_mapping.h"
#include "http/error.h"

namespace cpp_network {
namespace http {

namespace {

struct WriteBuffer {
  std::string data;
};

size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* buf = static_cast<WriteBuffer*>(userdata);
  buf->data.append(ptr, size * nmemb);
  return size * nmemb;
}

size_t HeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
  auto* headers = static_cast<Headers*>(userdata);
  std::string line(buffer, size * nitems);
  const std::size_t colon = line.find(':');
  if (colon != std::string::npos) {
    std::string name = line.substr(0, colon);
    std::string value = line.substr(colon + 1);
    // Trim leading whitespace.
    while (!value.empty() && (value[0] == ' ' || value[0] == '\t')) {
      value.erase(0, 1);
    }
    // Strip trailing CR/LF.
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
      value.pop_back();
    }
    headers->emplace_back(std::move(name), std::move(value));
  }
  return size * nitems;
}

Error MapCurlError(CURLcode rc, const std::string& strerror,
                   bool has_total_timeout) {
  switch (rc) {
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_RESOLVE_PROXY:
      return Error(ErrorCode::kDnsResolutionFailed, strerror);
    case CURLE_COULDNT_CONNECT:
      return Error(ErrorCode::kConnectionRefused, strerror);
    case CURLE_OPERATION_TIMEDOUT:
      // libcurl reports the same code for connect/total timeouts. When no total
      // timeout is configured, a timeout must come from the connect phase.
      if (!has_total_timeout) {
        return Error(ErrorCode::kConnectionTimeout, strerror);
      }
      return Error(ErrorCode::kTotalTimeout, strerror);
    case CURLE_SSL_CONNECT_ERROR:
      return Error(ErrorCode::kTlsHandshakeFailed, strerror);
    case CURLE_PEER_FAILED_VERIFICATION:
      return Error(ErrorCode::kCertificateVerificationFailed, strerror);
    case CURLE_TOO_MANY_REDIRECTS:
      return Error(ErrorCode::kTooManyRedirects, strerror);
    case CURLE_UNSUPPORTED_PROTOCOL:
      return Error(ErrorCode::kUnsupportedProtocol, strerror);
    case CURLE_OUT_OF_MEMORY:
      return Error(ErrorCode::kOutOfMemory, strerror);
    case CURLE_READ_ERROR:
    case CURLE_WRITE_ERROR:
      return Error(ErrorCode::kProtocolError, strerror);
    default:
      return Error(ErrorCode::kProtocolError, strerror);
  }
}

}  // namespace

Engine::Engine(const Options& options)
    : multi_(curl_multi_init()), options_(options), closed_(false) {
  if (multi_) {
    detail::ApplyMultiOptions(multi_, options_);
  }
}

Engine::~Engine() {
  Close();
}

Result<Response> Engine::Send(const Request& req) {
  std::lock_guard<std::mutex> lock(mu_);
  if (closed_ || !multi_) {
    return Result<Response>::Err(
        Error(ErrorCode::kInvalidState, "client is closed or engine failed to init"));
  }
  return PerformSingle(req);
}

Result<Response> Engine::PerformSingle(const Request& req) {
  CURL* easy = curl_easy_init();
  if (!easy) {
    return Result<Response>::Err(
        Error(ErrorCode::kOutOfMemory, "curl_easy_init failed"));
  }

  WriteBuffer body;
  Headers headers;
  curl_slist* header_list = nullptr;
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, HeaderCallback);
  curl_easy_setopt(easy, CURLOPT_HEADERDATA, &headers);

  Error map_error =
      detail::ApplyEasyOptions(easy, req, options_, &header_list);
  if (!map_error.ok()) {
    if (header_list) curl_slist_free_all(header_list);
    curl_easy_cleanup(easy);
    return Result<Response>::Err(map_error);
  }

  CURLMcode mrc = curl_multi_add_handle(multi_, easy);
  if (mrc != CURLM_OK) {
    if (header_list) curl_slist_free_all(header_list);
    curl_easy_cleanup(easy);
    return Result<Response>::Err(
        Error(ErrorCode::kInternalError, "curl_multi_add_handle failed"));
  }

  int running = 0;
  do {
    int numfds = 0;
    mrc = curl_multi_poll(multi_, nullptr, 0, 1000, &numfds);
    if (mrc != CURLM_OK) {
      break;
    }
    mrc = curl_multi_perform(multi_, &running);
    if (mrc != CURLM_OK) {
      break;
    }
  } while (running > 0);

  CURLcode rc = CURLE_OK;
  bool done = false;
  CURLMsg* msg;
  int msgs_left = 0;
  while ((msg = curl_multi_info_read(multi_, &msgs_left)) != nullptr) {
    if (msg->msg == CURLMSG_DONE && msg->easy_handle == easy) {
      rc = msg->data.result;
      done = true;
      break;
    }
  }

  curl_multi_remove_handle(multi_, easy);

  if (!done || rc != CURLE_OK) {
    const char* strerr = done ? curl_easy_strerror(rc) : "transfer interrupted";
    if (header_list) curl_slist_free_all(header_list);
    curl_easy_cleanup(easy);
    return Result<Response>::Err(
        MapCurlError(rc, strerr, options_.total_timeout().count() > 0));
  }

  long status_code = 0;
  curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status_code);
  char* effective_url = nullptr;
  curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &effective_url);

  Response response(static_cast<int>(status_code), "", std::move(headers),
                    std::move(body.data), !body.data.empty(),
                    effective_url ? effective_url : req.url(), nullptr);
  if (header_list) curl_slist_free_all(header_list);
  curl_easy_cleanup(easy);
  return Result<Response>::Ok(std::move(response));
}

void Engine::Close() {
  std::lock_guard<std::mutex> lock(mu_);
  if (closed_) return;
  if (multi_) {
    curl_multi_cleanup(multi_);
    multi_ = nullptr;
  }
  closed_ = true;
}

}  // namespace http
}  // namespace cpp_network
