#ifndef CPP_NETWORK_HTTP_EXPORT_H_
#define CPP_NETWORK_HTTP_EXPORT_H_

// Export macro for the cpp_network HTTP library. Public API types are
// annotated with CPP_NETWORK_HTTP_EXPORT, which always marks the symbol with
// default visibility so it is reachable from a -fvisibility=hidden shared
// library build.

#if defined(_WIN32)
#define CPP_NETWORK_HTTP_EXPORT
#else
#define CPP_NETWORK_HTTP_EXPORT
#endif

#endif  // CPP_NETWORK_HTTP_EXPORT_H_
