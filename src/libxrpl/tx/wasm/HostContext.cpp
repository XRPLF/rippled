#include <xrpl/tx/wasm/HostContext.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <rust/cxx.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace xrpl {

namespace {

// What a host call answers when it could not be served at all: every method below hands it
// to `guarded` as the answer for a body that throws. The engine reads -1 as its fatal
// `Internal`, stops the run and reports `tecINTERNAL`.
//
// `HostFunctionError` spells -1 `Unimplemented`, so the two share a code. They also share
// a meaning worth keeping together - "the host could not serve this call, and the contract
// has no business interpreting why" - and they must share a fate. Named here so a call
// site reads as what it is rather than as "unimplemented".
constexpr std::int32_t kHostInternal = hfErrorToInt(HostFunctionError::Unimplemented);

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
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        auto const sqn = hostFunctions_.getLedgerSqn();
        if (!sqn)
            return hfErrorToInt(sqn.error());

        return answerScalar(out, *sqn);
    });
}

std::int32_t
HostContext::getParentLedgerTime(rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        auto const time = hostFunctions_.getParentLedgerTime();
        if (!time)
            return hfErrorToInt(time.error());

        return answerScalar(out, *time);
    });
}

std::int32_t
HostContext::getParentLedgerHash(rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        auto const hash = hostFunctions_.getParentLedgerHash();
        if (!hash)
            return hfErrorToInt(hash.error());

        return answer(out, hash->data(), hash->size());
    });
}

std::int32_t
HostContext::getBaseFee(rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        auto const fee = hostFunctions_.getBaseFee();
        if (!fee)
            return hfErrorToInt(fee.error());

        return answerScalar(out, *fee);
    });
}

std::int32_t
HostContext::isAmendmentEnabled(rust::Slice<std::uint8_t const> amendment) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        // A 32-byte input may be an amendment id; try that first and fall through to
        // a name lookup if it is not an enabled amendment - the 32 bytes could spell
        // a name instead.
        if (amendment.size() == uint256::size())
        {
            auto const enabled =
                hostFunctions_.isAmendmentEnabled(uint256::fromVoid(amendment.data()));
            if (enabled && *enabled == 1)
                return *enabled;
        }

        if (amendment.size() > 64)
            return hfErrorToInt(HostFunctionError::DataFieldTooLarge);

        auto const name =
            std::string_view(reinterpret_cast<char const*>(amendment.data()), amendment.size());
        auto const enabled = hostFunctions_.isAmendmentEnabled(name);
        if (!enabled)
            return hfErrorToInt(enabled.error());

        return *enabled;
    });
}

std::int32_t
HostContext::cacheLedgerObj(rust::Slice<std::uint8_t const> objId, std::int32_t cacheIdx)
    const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        if (objId.size() != uint256::size())
            return hfErrorToInt(HostFunctionError::InvalidParams);

        auto const slot = hostFunctions_.cacheLedgerObj(uint256::fromVoid(objId.data()), cacheIdx);
        if (!slot)
            return hfErrorToInt(slot.error());

        return *slot;
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

std::int32_t
HostContext::trace(rust::Str msg, rust::Slice<std::uint8_t const> data, bool asHex) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        auto const status = hostFunctions_.trace(
            std::string_view{msg.data(), msg.size()}, Slice{data.data(), data.size()}, asHex);
        if (!status)
            return hfErrorToInt(status.error());

        return *status;
    });
}

std::int32_t
HostContext::traceNum(rust::Str msg, std::int64_t number) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        auto const status =
            hostFunctions_.traceNum(std::string_view{msg.data(), msg.size()}, number);
        if (!status)
            return hfErrorToInt(status.error());

        return *status;
    });
}

}  // namespace xrpl
