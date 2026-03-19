#pragma once

// Helper to disable ASan/HwASan for specific functions.
// ASAN flags some false positives with sudden jumps in control flow, like
// exceptions, or when encountering coroutine stack switches. This macro can
// be used to disable ASAN instrumentation for specific functions.
#if defined(__GNUC__) || defined(__clang__)
#define XRPL_NO_SANITIZE_ADDRESS __attribute__((no_sanitize("address", "hwaddress")))
#else
#define XRPL_NO_SANITIZE_ADDRESS
#endif

// Detect whether a memory sanitizer (TSAN or ASAN) is active at compile time.
// GCC defines __SANITIZE_THREAD__ / __SANITIZE_ADDRESS__ directly.
// Clang uses __has_feature(thread_sanitizer) / __has_feature(address_sanitizer).
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
#define XRPL_SANITIZER_ACTIVE 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
#define XRPL_SANITIZER_ACTIVE 1
#else
#define XRPL_SANITIZER_ACTIVE 0
#endif
#else
#define XRPL_SANITIZER_ACTIVE 0
#endif
