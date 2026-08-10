#include <xrpl/tx/wasm/HostContext.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <rust/cxx.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <string_view>
#include <utility>
#include <vector>

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

// Decode an asset from its wire bytes, whose length selects the kind: an MPT id, a
// bare currency (which must be XRP), or a currency followed by an issuer (which must
// not be XRP). Any other length is malformed. This mirrors `getDataAsset` in the
// C-ABI wrapper the wasm engine replaces.
std::expected<Asset, HostFunctionError>
parseAsset(rust::Slice<std::uint8_t const> bytes)
{
    if (bytes.size() == MPTID::size())
        return Asset{MPTID::fromVoid(bytes.data())};

    if (bytes.size() == Currency::size())
    {
        auto const issue = Issue{Currency::fromVoid(bytes.data()), xrpAccount()};
        if (!issue.native())
            return std::unexpected(HostFunctionError::InvalidParams);
        return Asset{issue};
    }

    if (bytes.size() == Currency::size() + AccountID::size())
    {
        auto const issue = Issue(
            Currency::fromVoid(bytes.data()), AccountID::fromVoid(bytes.data() + Currency::size()));
        if (issue.native())
            return std::unexpected(HostFunctionError::InvalidParams);
        return Asset{issue};
    }

    return std::unexpected(HostFunctionError::InvalidParams);
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
HostContext::getTxField(std::int32_t field, rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        auto const& knownSFields = SField::getKnownCodeToField();
        auto const it = knownSFields.find(field);
        if (it == knownSFields.end())
            return hfErrorToInt(HostFunctionError::InvalidField);

        auto const value = hostFunctions_.getTxField(*it->second);
        if (!value)
            return hfErrorToInt(value.error());

        return answer(out, value->data(), value->size());
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
HostContext::getLedgerObjField(
    std::int32_t cacheIdx,
    std::int32_t field,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        auto const& knownSFields = SField::getKnownCodeToField();
        auto const it = knownSFields.find(field);
        if (it == knownSFields.end())
            return hfErrorToInt(HostFunctionError::InvalidField);

        auto const value = hostFunctions_.getLedgerObjField(cacheIdx, *it->second);
        if (!value)
            return hfErrorToInt(value.error());

        return answer(out, value->data(), value->size());
    });
}

std::int32_t
HostContext::getTxNestedField(
    rust::Slice<std::uint8_t const> locator,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        // A path of i32 steps: non-empty and a whole number of them.
        if (locator.empty() || (locator.size() & 3) != 0)
            return hfErrorToInt(HostFunctionError::LocatorMalformed);

        // Copy into an aligned int32 buffer rather than aliasing the slice, whose
        // bytes carry no int32 alignment guarantee. The wire byte order is kept; the
        // field getters below apply `adjustWasmEndianess` when they read a step.
        std::uint32_t const steps = locator.size() / sizeof(std::int32_t);
        std::vector<std::int32_t> locBuf(steps);
        std::memcpy(locBuf.data(), locator.data(), locator.size());
        FieldLocator const fl(std::move(locBuf));

        auto const value = hostFunctions_.getTxNestedField(fl);
        if (!value)
            return hfErrorToInt(value.error());

        return answer(out, value->data(), value->size());
    });
}

std::int32_t
HostContext::getCurrentLedgerObjNestedField(
    rust::Slice<std::uint8_t const> locator,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        if (locator.empty() || (locator.size() & 3) != 0)
            return hfErrorToInt(HostFunctionError::LocatorMalformed);

        std::uint32_t const steps = locator.size() / sizeof(std::int32_t);
        std::vector<std::int32_t> locBuf(steps);
        std::memcpy(locBuf.data(), locator.data(), locator.size());
        FieldLocator const fl(std::move(locBuf));

        auto const value = hostFunctions_.getCurrentLedgerObjNestedField(fl);
        if (!value)
            return hfErrorToInt(value.error());

        return answer(out, value->data(), value->size());
    });
}

std::int32_t
HostContext::getLedgerObjNestedField(
    std::int32_t cacheIdx,
    rust::Slice<std::uint8_t const> locator,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        if (locator.empty() || (locator.size() & 3) != 0)
            return hfErrorToInt(HostFunctionError::LocatorMalformed);

        std::uint32_t const steps = locator.size() / sizeof(std::int32_t);
        std::vector<std::int32_t> locBuf(steps);
        std::memcpy(locBuf.data(), locator.data(), locator.size());
        FieldLocator const fl(std::move(locBuf));

        auto const value = hostFunctions_.getLedgerObjNestedField(cacheIdx, fl);
        if (!value)
            return hfErrorToInt(value.error());

        return answer(out, value->data(), value->size());
    });
}

std::int32_t
HostContext::getTxArrayLen(std::int32_t field) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        auto const& knownSFields = SField::getKnownCodeToField();
        auto const it = knownSFields.find(field);
        if (it == knownSFields.end())
            return hfErrorToInt(HostFunctionError::InvalidField);

        auto const len = hostFunctions_.getTxArrayLen(*it->second);
        if (!len)
            return hfErrorToInt(len.error());

        return *len;
    });
}

std::int32_t
HostContext::getCurrentLedgerObjArrayLen(std::int32_t field) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        auto const& knownSFields = SField::getKnownCodeToField();
        auto const it = knownSFields.find(field);
        if (it == knownSFields.end())
            return hfErrorToInt(HostFunctionError::InvalidField);

        auto const len = hostFunctions_.getCurrentLedgerObjArrayLen(*it->second);
        if (!len)
            return hfErrorToInt(len.error());

        return *len;
    });
}

std::int32_t
HostContext::getLedgerObjArrayLen(std::int32_t cacheIdx, std::int32_t field) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        auto const& knownSFields = SField::getKnownCodeToField();
        auto const it = knownSFields.find(field);
        if (it == knownSFields.end())
            return hfErrorToInt(HostFunctionError::InvalidField);

        auto const len = hostFunctions_.getLedgerObjArrayLen(cacheIdx, *it->second);
        if (!len)
            return hfErrorToInt(len.error());

        return *len;
    });
}

std::int32_t
HostContext::getTxNestedArrayLen(rust::Slice<std::uint8_t const> locator) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        if (locator.empty() || (locator.size() & 3) != 0)
            return hfErrorToInt(HostFunctionError::LocatorMalformed);

        std::uint32_t const steps = locator.size() / sizeof(std::int32_t);
        std::vector<std::int32_t> locBuf(steps);
        std::memcpy(locBuf.data(), locator.data(), locator.size());
        FieldLocator const fl(std::move(locBuf));

        auto const len = hostFunctions_.getTxNestedArrayLen(fl);
        if (!len)
            return hfErrorToInt(len.error());

        return *len;
    });
}

std::int32_t
HostContext::getCurrentLedgerObjNestedArrayLen(
    rust::Slice<std::uint8_t const> locator) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        if (locator.empty() || (locator.size() & 3) != 0)
            return hfErrorToInt(HostFunctionError::LocatorMalformed);

        std::uint32_t const steps = locator.size() / sizeof(std::int32_t);
        std::vector<std::int32_t> locBuf(steps);
        std::memcpy(locBuf.data(), locator.data(), locator.size());
        FieldLocator const fl(std::move(locBuf));

        auto const len = hostFunctions_.getCurrentLedgerObjNestedArrayLen(fl);
        if (!len)
            return hfErrorToInt(len.error());

        return *len;
    });
}

std::int32_t
HostContext::getLedgerObjNestedArrayLen(
    std::int32_t cacheIdx,
    rust::Slice<std::uint8_t const> locator) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        if (locator.empty() || (locator.size() & 3) != 0)
            return hfErrorToInt(HostFunctionError::LocatorMalformed);

        std::uint32_t const steps = locator.size() / sizeof(std::int32_t);
        std::vector<std::int32_t> locBuf(steps);
        std::memcpy(locBuf.data(), locator.data(), locator.size());
        FieldLocator const fl(std::move(locBuf));

        auto const len = hostFunctions_.getLedgerObjNestedArrayLen(cacheIdx, fl);
        if (!len)
            return hfErrorToInt(len.error());

        return *len;
    });
}

std::int32_t
HostContext::checkSignature(
    rust::Slice<std::uint8_t const> message,
    rust::Slice<std::uint8_t const> signature,
    rust::Slice<std::uint8_t const> pubkey) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        auto const valid = hostFunctions_.checkSignature(
            Slice{message.data(), message.size()},
            Slice{signature.data(), signature.size()},
            Slice{pubkey.data(), pubkey.size()});
        if (!valid)
            return hfErrorToInt(valid.error());

        return *valid;
    });
}

std::int32_t
HostContext::accountKeylet(rust::Slice<std::uint8_t const> account, rust::Slice<std::uint8_t> out)
    const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        if (account.size() != AccountID::size())
            return hfErrorToInt(HostFunctionError::InvalidParams);

        auto const value = hostFunctions_.accountKeylet(AccountID::fromVoid(account.data()));
        if (!value)
            return hfErrorToInt(value.error());

        return answer(out, value->data(), value->size());
    });
}

std::int32_t
HostContext::ammKeylet(
    rust::Slice<std::uint8_t const> asset1,
    rust::Slice<std::uint8_t const> asset2,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        auto const a1 = parseAsset(asset1);
        if (!a1)
            return hfErrorToInt(a1.error());

        auto const a2 = parseAsset(asset2);
        if (!a2)
            return hfErrorToInt(a2.error());

        auto const value = hostFunctions_.ammKeylet(*a1, *a2);
        if (!value)
            return hfErrorToInt(value.error());

        return answer(out, value->data(), value->size());
    });
}

std::int32_t
HostContext::checkKeylet(
    rust::Slice<std::uint8_t const> account,
    std::int32_t seq,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        if (account.size() != AccountID::size())
            return hfErrorToInt(HostFunctionError::InvalidParams);

        // The guest's u32 seq arrives as its i32 bit pattern; recover it.
        auto const value = hostFunctions_.checkKeylet(
            AccountID::fromVoid(account.data()), static_cast<std::uint32_t>(seq));
        if (!value)
            return hfErrorToInt(value.error());

        return answer(out, value->data(), value->size());
    });
}

std::int32_t
HostContext::credentialKeylet(
    rust::Slice<std::uint8_t const> subject,
    rust::Slice<std::uint8_t const> issuer,
    rust::Slice<std::uint8_t const> credentialType,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        if (subject.size() != AccountID::size() || issuer.size() != AccountID::size())
            return hfErrorToInt(HostFunctionError::InvalidParams);

        auto const value = hostFunctions_.credentialKeylet(
            AccountID::fromVoid(subject.data()),
            AccountID::fromVoid(issuer.data()),
            Slice{credentialType.data(), credentialType.size()});
        if (!value)
            return hfErrorToInt(value.error());

        return answer(out, value->data(), value->size());
    });
}

std::int32_t
HostContext::delegateKeylet(
    rust::Slice<std::uint8_t const> account,
    rust::Slice<std::uint8_t const> authorize,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        if (account.size() != AccountID::size() || authorize.size() != AccountID::size())
            return hfErrorToInt(HostFunctionError::InvalidParams);

        auto const value = hostFunctions_.delegateKeylet(
            AccountID::fromVoid(account.data()), AccountID::fromVoid(authorize.data()));
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
