#include <xrpl/tx/wasm/HostContext.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <source_location>
#include <string_view>

namespace xrpl {

namespace {

// What a host call answers when it could not be served at all. The engine reads -1 as its
// fatal `Internal`, stops the run and reports `tecINTERNAL`.
//
// `HostFunctionError` spells -1 `Unimplemented`, so the two share a code. They also share
// a meaning worth keeping together - "the host could not serve this call, and the contract
// has no business interpreting why" - and they must share a fate. Named here so a call
// site reads as what it is rather than as "unimplemented".
constexpr std::int32_t kHostInternal = hfErrorToInt(HostFunctionError::Unimplemented);

// Nothing may unwind out of a host call: the frames that called it are Rust, which cannot
// run a C++ landing pad. Every method below goes through here, so the catch is not a thing
// any one of them can forget.
//
// The caller names itself: the default argument is evaluated at the call site, so the log
// line gets the enclosing method without anyone passing a string that could drift from the
// method it labels. `__func__` would expand to `operator()` inside the lambda, which is why
// this is a defaulted parameter rather than something the body reads.
template <class Body>
std::int32_t
guarded(
    beast::Journal journal,
    Body&& body,
    std::source_location const location = std::source_location::current()) noexcept
{
    try
    {
        return body();
    }
    catch (std::exception const& e)
    {
        JLOG(journal.warn()) << "wasm host call threw in " << location.function_name() << ": "
                             << e.what();
    }
    catch (...)
    {
        JLOG(journal.warn())
            << "wasm host call threw a non-exception in " << location.function_name();
    }

    return kHostInternal;
}

// Copy `value` into `out` only if the whole of it fits, and answer its true length either
// way. A value too large for the guest's buffer must reach it in no part: a prefix would
// be a wrong answer where a length is a usable one.
std::int32_t
answer(rust::Slice<std::uint8_t> out, std::uint8_t const* value, std::size_t size)
{
    if (size <= out.size())
        std::memcpy(out.data(), value, size);
    return static_cast<std::int32_t>(size);
}

// A scalar the ABI carries as bytes, in the wire's byte order.
//
// `adjustWasmEndianess` is the one place that order is decided for the whole wasm boundary,
// and it is `constexpr` with the swap under `if constexpr (std::endian::native ==
// std::endian::big)` - so this costs nothing on a little-endian host and is correct on a
// big-endian one, which a hand-written shift sequence per call site would have to get right
// each time.
template <class T>
std::int32_t
answerScalar(rust::Slice<std::uint8_t> out, T value)
{
    auto const wire = adjustWasmEndianess(value);
    return answer(out, reinterpret_cast<std::uint8_t const*>(&wire), sizeof(wire));
}

}  // namespace

HostContext::HostContext(HostFunctions& hostFunctions) : hostFunctions_(hostFunctions)
{
}

std::int32_t
HostContext::getLedgerSqn(rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), [&] {
        auto const sqn = hostFunctions_.getLedgerSqn();
        if (!sqn)
            return hfErrorToInt(sqn.error());

        // Four bytes the guest reads back with `u32::from_le_bytes`.
        return answerScalar(out, *sqn);
    });
}

std::int32_t
HostContext::getCurrentLedgerObjField(std::int32_t field, rust::Slice<std::uint8_t> out)
    const noexcept
{
    return guarded(hostFunctions_.getJournal(), [&] {
        auto const& knownSFields = SField::getKnownCodeToField();
        auto const it = knownSFields.find(field);
        if (it == knownSFields.end())
            return hfErrorToInt(HostFunctionError::InvalidField);

        auto const value = hostFunctions_.getCurrentLedgerObjField(*it->second);
        if (!value)
            return hfErrorToInt(value.error());

        return answer(out, value->data(), value->size());
    });
}

std::int32_t
HostContext::sha512Half(rust::Slice<std::uint8_t const> data, rust::Slice<std::uint8_t> out)
    const noexcept
{
    return guarded(hostFunctions_.getJournal(), [&] {
        auto const digest = hostFunctions_.computeSha512HalfHash(Slice(data.data(), data.size()));
        if (!digest)
            return hfErrorToInt(digest.error());

        return answer(out, digest->data(), digest->size());
    });
}

std::int32_t
HostContext::trace(rust::Str msg, rust::Slice<std::uint8_t const> data, bool asHex) const noexcept
{
    return guarded(hostFunctions_.getJournal(), [&] {
        auto const status = hostFunctions_.trace(
            std::string_view(msg.data(), msg.size()), Slice(data.data(), data.size()), asHex);
        if (!status)
            return hfErrorToInt(status.error());

        return *status;
    });
}

std::int32_t
HostContext::traceNum(rust::Str msg, std::int64_t number) const noexcept
{
    return guarded(hostFunctions_.getJournal(), [&] {
        auto const status =
            hostFunctions_.traceNum(std::string_view(msg.data(), msg.size()), number);
        if (!status)
            return hfErrorToInt(status.error());

        return *status;
    });
}

}  // namespace xrpl
