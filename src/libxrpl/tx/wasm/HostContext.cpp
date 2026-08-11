#include <xrpl/tx/wasm/HostContext.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <rust/cxx.h>
// For `TraceDataType`, which the bridge declares and this header defines.
#include <xrpl_wasm_vm_ffi_cxxbridge/lib.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace xrpl {

namespace {

// What a host call answers when it could not be served at all: every method below hands it
// to `guarded` as the answer for a body that throws.
constexpr std::int32_t kHostInternal = hfErrorToInt(HostFunctionError::InternalFatal);

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

// A traced integer, which the guest sends as bytes rather than as a wasm scalar so that one
// import serves every type. `std::nullopt` if the buffer is not the width the type needs.
//
// `memcpy` regardless of alignment, and no `reinterpret_cast` fast path: a trace must cost
// the same whatever address the guest chose for its buffer.
template <class T>
std::optional<T>
traceInt(Slice const& data)
{
    static_assert(std::is_integral_v<T>);
    if (data.size() != sizeof(T))
        return std::nullopt;

    T x;
    std::memcpy(&x, data.data(), sizeof(T));
    return adjustWasmEndianess(x);
}

// The guest's bytes as the text a log line carries, or `std::nullopt` when they do not hold
// the type they claim.
//
// The engine refuses a code that names no type before it crosses, so `type` is always one of
// the variants; the trailing `return` is what the `switch` owes a scoped enum, not a case
// this can meet.
//
// May throw: `STAmount`'s deserializer rejects malformed input that way.
std::optional<std::string>
traceFormat(TraceDataType type, Slice const& data)
{
    switch (type)
    {
        case TraceDataType::Int64:
            if (auto const x = traceInt<std::int64_t>(data))
                return std::to_string(*x);
            return std::nullopt;

        case TraceDataType::Uint64:
            if (auto const x = traceInt<std::uint64_t>(data))
                return std::to_string(*x);
            return std::nullopt;

        case TraceDataType::Xfloat:
            return wasm_float::floatToString(data);

        case TraceDataType::Account:
            if (data.size() != AccountID::size())
                return std::nullopt;
            return toBase58(AccountID::fromVoid(data.data()));

        case TraceDataType::Amount: {
            SerialIter iter(data);
            STAmount const amount(iter, sfGeneric);
            return amount.getFullText();
        }

        case TraceDataType::AsHex:
            return strHex(data);

        case TraceDataType::AsText:
            // An empty Slice has a null data(), which std::string may not be handed.
            if (data.empty())
                return std::string();
            return std::string(reinterpret_cast<char const*>(data.data()), data.size());
    }

    return std::nullopt;
}

}  // namespace

HostContext::HostContext(HostFunctions& hostFunctions) : hostFunctions_(hostFunctions)
{
}

std::int32_t
HostContext::getLedgerSqn(rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        auto const sqn = hostFunctions_.getLedgerSqn();
        if (!sqn)
            return hfErrorToInt(sqn.error());

        return answerScalar(out, *sqn);
    });
}

std::int32_t
HostContext::getCurrentLedgerObjField(std::int32_t field, rust::Slice<std::uint8_t> out)
    const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
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
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        auto const digest = hostFunctions_.computeSha512HalfHash(Slice{data.data(), data.size()});
        if (!digest)
            return hfErrorToInt(digest.error());

        return answer(out, digest->data(), digest->size());
    });
}

void
HostContext::trace(rust::Str msg, rust::Slice<std::uint8_t const> data, TraceDataType dataType)
    const noexcept
{
    auto const journal = hostFunctions_.getJournal();

    // Not `guarded`: a buffer that does not hold what it claims is an ordinary contract
    // mistake, so it belongs in the log the contract is writing to rather than in the error
    // log as an internal failure - and it must not become one, since there is nothing to
    // report it to.
    try
    {
        if (msg.size() + data.size() > kMaxWasmDataLength)
        {
            JLOG(journal.trace()) << "WasmTrace: message and data too long";
            return;
        }

        // Rendered whatever the log level: the level decides what is written, never whether
        // the host is called, so a run costs the same on every node.
        auto const text = traceFormat(dataType, Slice{data.data(), data.size()});
        if (!text)
        {
            JLOG(journal.trace()) << "WasmTrace: data does not hold the type it names";
            return;
        }

        hostFunctions_.trace(std::string_view{msg.data(), msg.size()}, *text);
    }
    catch (std::exception const& e)
    {
        JLOG(journal.trace()) << "WasmTrace: threw: " << e.what();
    }
    catch (...)
    {
        JLOG(journal.trace()) << "WasmTrace: threw";
    }
}

}  // namespace xrpl
