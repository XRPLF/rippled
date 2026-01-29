#pragma once

#include <xrpld/app/wasm/ParamsHelper.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

enum class HostFunctionError : int32_t {
    INTERNAL = -1,
    FIELD_NOT_FOUND = -2,
    BUFFER_TOO_SMALL = -3,
    NO_ARRAY = -4,
    NOT_LEAF_FIELD = -5,
    LOCATOR_MALFORMED = -6,
    SLOT_OUT_RANGE = -7,
    SLOTS_FULL = -8,
    EMPTY_SLOT = -9,
    LEDGER_OBJ_NOT_FOUND = -10,
    DECODING = -11,
    DATA_FIELD_TOO_LARGE = -12,
    POINTER_OUT_OF_BOUNDS = -13,
    NO_MEM_EXPORTED = -14,
    INVALID_PARAMS = -15,
    INVALID_ACCOUNT = -16,
    INVALID_FIELD = -17,
    INDEX_OUT_OF_BOUNDS = -18,
    FLOAT_INPUT_MALFORMED = -19,
    FLOAT_COMPUTATION_ERROR = -20,
    NO_RUNTIME = -21,
    OUT_OF_GAS = -22,
};

inline int32_t
HfErrorToInt(HostFunctionError e)
{
    return static_cast<int32_t>(e);
}

namespace wasm_float {

std::string
floatToString(Slice const& data);

Expected<Bytes, HostFunctionError>
floatFromIntImpl(int64_t x, int32_t mode);

Expected<Bytes, HostFunctionError>
floatFromUintImpl(uint64_t x, int32_t mode);

Expected<Bytes, HostFunctionError>
floatSetImpl(int32_t exponent, int64_t mantissa, int32_t mode);

Expected<int32_t, HostFunctionError>
floatCompareImpl(Slice const& x, Slice const& y);

Expected<Bytes, HostFunctionError>
floatAddImpl(Slice const& x, Slice const& y, int32_t mode);

Expected<Bytes, HostFunctionError>
floatSubtractImpl(Slice const& x, Slice const& y, int32_t mode);

Expected<Bytes, HostFunctionError>
floatMultiplyImpl(Slice const& x, Slice const& y, int32_t mode);

Expected<Bytes, HostFunctionError>
floatDivideImpl(Slice const& x, Slice const& y, int32_t mode);

Expected<Bytes, HostFunctionError>
floatRootImpl(Slice const& x, int32_t n, int32_t mode);

Expected<Bytes, HostFunctionError>
floatPowerImpl(Slice const& x, int32_t n, int32_t mode);

Expected<Bytes, HostFunctionError>
floatLogImpl(Slice const& x, int32_t mode);

}  // namespace wasm_float

struct HostFunctions
{
    beast::Journal j_;

    HostFunctions(beast::Journal j = beast::Journal{beast::Journal::getNullSink()}) : j_(j)
    {
    }

    // LCOV_EXCL_START
    virtual void
    setRT(void const*)
    {
    }

    virtual void const*
    getRT() const
    {
        return nullptr;
    }

    std::int64_t
    getGas()
    {
        return -1;
    }

    void
    setGas(std::int64_t)
    {
        return;
    }

    beast::Journal
    getJournal()
    {
        return j_;
    }

#pragma push_macro("HOST_FUNCTION_BYTES_RETURN")
#pragma push_macro("HOST_FUNCTION_HASH_RETURN")
#pragma push_macro("HOST_FUNCTION_NO_RETURN")
#pragma push_macro("HOST_FUNCTION_INT_RETURN")
#pragma push_macro("HOST_FUNCTION_UINT_RETURN")

#define HOST_FUNCTION_BYTES_RETURN(NAME, ...)                    \
    virtual Expected<Bytes, HostFunctionError> NAME(__VA_ARGS__) \
    {                                                            \
        return Unexpected(HostFunctionError::INTERNAL);          \
    }

#define HOST_FUNCTION_HASH_RETURN(NAME, ...)                    \
    virtual Expected<Hash, HostFunctionError> NAME(__VA_ARGS__) \
    {                                                           \
        return Unexpected(HostFunctionError::INTERNAL);         \
    }

#define HOST_FUNCTION_NO_RETURN(NAME, ...)                         \
    virtual Expected<int32_t, HostFunctionError> NAME(__VA_ARGS__) \
    {                                                              \
        return Unexpected(HostFunctionError::INTERNAL);            \
    }

#define HOST_FUNCTION_INT_RETURN(NAME, ...)                        \
    virtual Expected<int32_t, HostFunctionError> NAME(__VA_ARGS__) \
    {                                                              \
        return Unexpected(HostFunctionError::INTERNAL);            \
    }

#define HOST_FUNCTION_UINT_RETURN(NAME, ...)                             \
    virtual Expected<std::uint32_t, HostFunctionError> NAME(__VA_ARGS__) \
    {                                                                    \
        return Unexpected(HostFunctionError::INTERNAL);                  \
    }

#include <xrpld/app/wasm/host_functions.macro>

#pragma pop_macro("HOST_FUNCTION_UINT_RETURN")
#pragma pop_macro("HOST_FUNCTION_INT_RETURN")
#pragma pop_macro("HOST_FUNCTION_NO_RETURN")
#pragma pop_macro("HOST_FUNCTION_HASH_RETURN")
#pragma pop_macro("HOST_FUNCTION_BYTES_RETURN")

    virtual ~HostFunctions() = default;
    // LCOV_EXCL_STOP
};

}  // namespace xrpl
