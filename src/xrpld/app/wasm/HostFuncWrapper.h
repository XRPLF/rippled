#pragma once

#include <xrpld/app/wasm/WasmiVM.h>

#include <cstdint>

namespace xrpl {

#pragma push_macro("INT32_PARAM")
#pragma push_macro("INT64_PARAM")
#pragma push_macro("UINT32_PARAM")
#pragma push_macro("UINT64_PARAM")
#pragma push_macro("SFIELD_PARAM")
#pragma push_macro("SLICE_PARAM")
#pragma push_macro("ACCOUNT_PARAM")
#pragma push_macro("UINT256_PARAM")
#pragma push_macro("ASSET_PARAM")
#pragma push_macro("CURRENCY_PARAM")
#pragma push_macro("MPTID_PARAM")
#pragma push_macro("STRING_VIEW_PARAM")
#pragma push_macro("STAMOUNT_PARAM")
#pragma push_macro("BOOL_PARAM")
#pragma push_macro("BYTES_PARAM")
#pragma push_macro("HOST_FUNCTION_BYTES_RETURN")
#pragma push_macro("HOST_FUNCTION_HASH_RETURN")
#pragma push_macro("HOST_FUNCTION_NO_RETURN")
#pragma push_macro("HOST_FUNCTION_INT_RETURN")
#pragma push_macro("HOST_FUNCTION_UINT_RETURN")

#define INT32_PARAM int32_t
#define INT64_PARAM int64_t
#define UINT32_PARAM uint8_t const*, int32_t
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
#define DECLARE_WRAP(NAME) wasm_trap_t* NAME##_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)

#define HOST_FUNCTION_BYTES_RETURN(NAME, ...)                                   \
    using NAME##_proto = int32_t(__VA_ARGS__ __VA_OPT__(, ) uint8_t*, int32_t); \
    DECLARE_WRAP(NAME);
#define HOST_FUNCTION_HASH_RETURN(NAME, ...)                                    \
    using NAME##_proto = int32_t(__VA_ARGS__ __VA_OPT__(, ) uint8_t*, int32_t); \
    DECLARE_WRAP(NAME);
#define HOST_FUNCTION_NO_RETURN(NAME, ...)     \
    using NAME##_proto = int32_t(__VA_ARGS__); \
    DECLARE_WRAP(NAME);
#define HOST_FUNCTION_INT_RETURN(NAME, ...)    \
    using NAME##_proto = int32_t(__VA_ARGS__); \
    DECLARE_WRAP(NAME);
#define HOST_FUNCTION_UINT_RETURN(NAME, ...)                                    \
    using NAME##_proto = int32_t(__VA_ARGS__ __VA_OPT__(, ) uint8_t*, int32_t); \
    DECLARE_WRAP(NAME);

#include <xrpld/app/wasm/host_functions.macro>

#undef DECLARE_WRAP

#pragma pop_macro("HOST_FUNCTION_UINT_RETURN")
#pragma pop_macro("HOST_FUNCTION_INT_RETURN")
#pragma pop_macro("HOST_FUNCTION_NO_RETURN")
#pragma pop_macro("HOST_FUNCTION_HASH_RETURN")
#pragma pop_macro("HOST_FUNCTION_BYTES_RETURN")
#pragma pop_macro("BYTES_PARAM")
#pragma pop_macro("BOOL_PARAM")
#pragma pop_macro("STAMOUNT_PARAM")
#pragma pop_macro("STRING_VIEW_PARAM")
#pragma pop_macro("MPTID_PARAM")
#pragma pop_macro("CURRENCY_PARAM")
#pragma pop_macro("ASSET_PARAM")
#pragma pop_macro("UINT256_PARAM")
#pragma pop_macro("ACCOUNT_PARAM")
#pragma pop_macro("SLICE_PARAM")
#pragma pop_macro("SFIELD_PARAM")
#pragma pop_macro("UINT64_PARAM")
#pragma pop_macro("UINT32_PARAM")
#pragma pop_macro("INT64_PARAM")
#pragma pop_macro("INT32_PARAM")

}  // namespace xrpl
