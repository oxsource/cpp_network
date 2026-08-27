#ifndef CPP_NETWORK_HTTP_METHOD_H_
#define CPP_NETWORK_HTTP_METHOD_H_

#include "comm/export.h"

namespace cpp_network {
namespace http {

enum class CPP_NETWORK_HTTP_EXPORT Method {
  kGet,
  kPost,
  kPut,
  kDelete,
  kPatch,
  kHead,
  kOptions,
};

}  // namespace http
}  // namespace cpp_network

#endif  // CPP_NETWORK_HTTP_METHOD_H_
