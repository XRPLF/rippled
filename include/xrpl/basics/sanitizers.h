#pragma once

// Helper to disable ASan/HwASan for specific functions
#if defined(__GNUC__) || defined(__clang__)
#define XRPL_NO_SANITIZE_ADDRESS __attribute__((no_sanitize("address", "hwaddress")))
#else
#define XRPL_NO_SANITIZE_ADDRESS
#endif
