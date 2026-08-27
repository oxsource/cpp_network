#ifndef CPP_NETWORK_COMM_EXPORT_H_
#define CPP_NETWORK_COMM_EXPORT_H_

// Export macros for the cpp_network shared core (comm/). Public API types are
// annotated with the module-specific macro (CPP_NETWORK_HTTP_EXPORT /
// CPP_NETWORK_WS_EXPORT).
//
// On Apple/Linux the annotation forces DEFAULT visibility per symbol: the
// whole tree is compiled with `-fvisibility=hidden` to bury third-party
// implementation symbols, while the public API must survive that setting in
// both static archives and shared-library builds. Windows keeps an empty
// decoration until the platform enters scope.
//
// CPP_NETWORK_HTTP_SHARED_LIBRARY (set by the shared target) is kept as a
// build-status marker for tooling; visibility above already covers both.

#define CPP_NETWORK_HTTP_EXPORT __attribute__((visibility("default")))

// Export macro for the cpp_network ws module (peer of http; shared-library
// build marker above also gates this module's visibility).
#define CPP_NETWORK_WS_EXPORT __attribute__((visibility("default")))

#endif  // CPP_NETWORK_COMM_EXPORT_H_
