#ifndef NETLIB_NETLIB_EXPORT_H_
#define NETLIB_NETLIB_EXPORT_H_

// Export macro for the netlib shared library. Mirrors graph_runtime's
// GRAPH_RUNTIME_API pattern: public symbols are annotated with NETLIB_API;
// the shared library build compiles with -fvisibility=hidden so only these
// symbols are exported.

#if defined(_WIN32)
#if defined(NETLIB_SHARED_LIBRARY)
#define NETLIB_API __declspec(dllexport)
#else
#define NETLIB_API __declspec(dllimport)
#endif
#else
#if defined(NETLIB_SHARED_LIBRARY)
#define NETLIB_API __attribute__((visibility("default")))
#else
#define NETLIB_API
#endif
#endif

#endif  // NETLIB_NETLIB_EXPORT_H_
