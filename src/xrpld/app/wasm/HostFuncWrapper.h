#pragma once

#include <xrpld/app/wasm/WasmiVM.h>

#include <cstdint>

namespace xrpl {

#define INT32_PARAM int32_t
#define INT64_PARAM int64_t
#define UINT32_PARAM int32_t
#define UINT64_PARAM uint8_t const*, int32_t
#define SFIELD_PARAM int32_t
#define SLICE_PARAM uint8_t const*, int32_t
#define ACCOUNT_PARAM uint8_t const*, int32_t
#define UINT256_PARAM uint8_t const*, int32_t
#define ASSET_PARAM uint8_t const*, int32_t
#define CURRENCY_PARAM uint8_t const*, int32_t
#define MPTID_PARAM uint8_t const*, int32_t
#define STRING_VIEW_PARAM uint8_t const*, int32_t
#define STAMOUNT_PARAM uint8_t const*, int32_t
#define BOOL_PARAM int32_t
#define BYTES_PARAM uint8_t const*, int32_t

// Declare wrapper function for each host function
#define DECLARE_WRAP(NAME)    \
    wasm_trap_t* NAME##_wrap( \
        void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)

// clang-format off
#define HOST_FUNCTION_BYTES_RETURN(NAME, ...) \
    using NAME##_proto = int32_t(__VA_ARGS__ __VA_OPT__(,) uint8_t*, int32_t); \
    DECLARE_WRAP(NAME);
#define HOST_FUNCTION_HASH_RETURN(NAME, ...) \
    using NAME##_proto = int32_t(__VA_ARGS__ __VA_OPT__(,) uint8_t*, int32_t); \
    DECLARE_WRAP(NAME);
#define HOST_FUNCTION_NO_RETURN(NAME, ...) \
    using NAME##_proto = int32_t(__VA_ARGS__); \
    DECLARE_WRAP(NAME);
#define HOST_FUNCTION_INT_RETURN(NAME, ...) \
    using NAME##_proto = int32_t(__VA_ARGS__); \
    DECLARE_WRAP(NAME);

#include <xrpld/app/wasm/host_functions.macro>

#undef HOST_FUNCTION_BYTES_RETURN
#undef HOST_FUNCTION_HASH_RETURN
#undef HOST_FUNCTION_NO_RETURN
#undef HOST_FUNCTION_INT_RETURN
#undef DECLARE_WRAP

}  // namespace xrpl
