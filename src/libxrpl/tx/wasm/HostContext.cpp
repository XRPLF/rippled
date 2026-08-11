#include <xrpl/tx/wasm/HostContext.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <rust/cxx.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
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
    {
        std::memcpy(out.data(), value, size);
    }
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
    {
        return Asset{MPTID::fromVoid(bytes.data())};
    }

    if (bytes.size() == Currency::size())
    {
        auto const issue = Issue{Currency::fromVoid(bytes.data()), xrpAccount()};
        if (!issue.native())
        {
            return std::unexpected(HostFunctionError::InvalidParams);
        }
        return Asset{issue};
    }

    if (bytes.size() == Currency::size() + AccountID::size())
    {
        auto const issue = Issue{
            Currency::fromVoid(bytes.data()), AccountID::fromVoid(bytes.data() + Currency::size())};
        if (issue.native())
        {
            return std::unexpected(HostFunctionError::InvalidParams);
        }
        return Asset{issue};
    }

    return std::unexpected(HostFunctionError::InvalidParams);
}

// Decode a `uint64` from its eight wire bytes, in the wire's byte order. The region
// must be exactly eight bytes, mirroring `getDataUnsigned` in the C-ABI wrapper.
std::expected<std::uint64_t, HostFunctionError>
parseUint64(rust::Slice<std::uint8_t const> bytes)
{
    if (bytes.size() != sizeof(std::uint64_t))
    {
        return std::unexpected(HostFunctionError::InvalidParams);
    }

    auto x = std::uint64_t{};
    std::memcpy(&x, bytes.data(), sizeof(x));
    return adjustWasmEndianess(x);
}

// Deserialize an `ST` object from its wire bytes; `InvalidParams` if the bytes are not
// a well-formed one. Mirrors the try/catch around `SerialIter` in the C-ABI wrapper.
template <class T>
std::expected<T, HostFunctionError>
parseST(rust::Slice<std::uint8_t const> bytes)
{
    try
    {
        auto sit = SerialIter{Slice{bytes.data(), bytes.size()}};
        return T{sit, sfGeneric};
    }
    catch (std::exception const&)
    {
        return std::unexpected(HostFunctionError::InvalidParams);
    }
}

template <typename Functor>
std::int32_t
invokeWithLocator(
    rust::Slice<std::uint8_t const> locator,
    rust::Slice<std::uint8_t> out,
    Functor&& functor)
{
    if (locator.empty() || (locator.size() & 3) != 0)
    {
        return hfErrorToInt(HostFunctionError::LocatorMalformed);
    }

    std::uint32_t const steps = locator.size() / sizeof(std::int32_t);
    auto locBuf = std::vector<std::int32_t>(steps);
    std::memcpy(locBuf.data(), locator.data(), locator.size());
    auto const fl = FieldLocator{std::move(locBuf)};

    auto const value = functor(fl);
    if (!value)
    {
        return hfErrorToInt(value.error());
    }

    return answer(out, value->data(), value->size());
}

template <typename Functor>
std::int32_t
invokeWithLocator(rust::Slice<std::uint8_t const> locator, Functor&& functor)
{
    if (locator.empty() || (locator.size() & 3) != 0)
    {
        return hfErrorToInt(HostFunctionError::LocatorMalformed);
    }

    std::uint32_t const steps = locator.size() / sizeof(std::int32_t);
    auto locBuf = std::vector<std::int32_t>(steps);
    std::memcpy(locBuf.data(), locator.data(), locator.size());
    auto const fl = FieldLocator{std::move(locBuf)};

    auto const value = functor(fl);
    if (!value)
    {
        return hfErrorToInt(value.error());
    }

    return *value;
}

template <typename Functor>
std::int32_t
invokeWithField(std::int32_t field, rust::Slice<std::uint8_t> out, Functor&& functor)
{
    auto const& knownSFields = SField::getKnownCodeToField();
    auto const it = knownSFields.find(field);
    if (it == std::end(knownSFields))
    {
        return hfErrorToInt(HostFunctionError::InvalidField);
    }

    auto const value = functor(*it->second);
    if (!value)
    {
        return hfErrorToInt(value.error());
    }

    return answer(out, value->data(), value->size());
}

template <typename Functor>
std::int32_t
invokeWithField(std::int32_t field, Functor&& functor)
{
    auto const& knownSFields = SField::getKnownCodeToField();
    auto const it = knownSFields.find(field);
    if (it == std::end(knownSFields))
    {
        return hfErrorToInt(HostFunctionError::InvalidField);
    }

    auto const len = functor(*it->second);
    if (!len)
    {
        return hfErrorToInt(len.error());
    }

    return *len;
}

template <typename Functor>
std::int32_t
invokeWithAccount(
    rust::Slice<std::uint8_t const> account,
    rust::Slice<std::uint8_t> out,
    Functor&& functor)
{
    if (account.size() != AccountID::size())
    {
        return hfErrorToInt(HostFunctionError::InvalidParams);
    }

    auto const value = functor(AccountID::fromVoid(account.data()));
    if (!value)
    {
        return hfErrorToInt(value.error());
    }

    return answer(out, value->data(), value->size());
}

template <typename Functor>
std::int32_t
invokeWithAccounts(
    rust::Slice<std::uint8_t const> account1,
    rust::Slice<std::uint8_t const> account2,
    rust::Slice<std::uint8_t> out,
    Functor&& functor)
{
    if (account1.size() != AccountID::size() || account2.size() != AccountID::size())
    {
        return hfErrorToInt(HostFunctionError::InvalidParams);
    }

    auto const value =
        functor(AccountID::fromVoid(account1.data()), AccountID::fromVoid(account2.data()));
    if (!value)
    {
        return hfErrorToInt(value.error());
    }

    return answer(out, value->data(), value->size());
}

template <bool Scalar, typename Functor>
std::int32_t
invokeNFT(rust::Slice<std::uint8_t const> nftId, rust::Slice<std::uint8_t> out, Functor&& functor)
{
    if (nftId.size() != uint256::size())
    {
        return hfErrorToInt(HostFunctionError::InvalidParams);
    }

    auto const value = functor(uint256::fromVoid(nftId.data()));
    if (!value)
    {
        return hfErrorToInt(value.error());
    }

    if constexpr (Scalar)
    {
        return answerScalar(out, *value);
    }
    else
    {
        return answer(out, value->data(), value->size());
    }
}

template <typename Functor>
std::int32_t
invokeNFT(rust::Slice<std::uint8_t const> nftId, Functor&& functor)
{
    if (nftId.size() != uint256::size())
    {
        return hfErrorToInt(HostFunctionError::InvalidParams);
    }

    auto const value = functor(uint256::fromVoid(nftId.data()));
    if (!value)
    {
        return hfErrorToInt(value.error());
    }

    return *value;
}

template <bool Scalar, typename Functor>
std::int32_t
invoke(rust::Slice<std::uint8_t> out, Functor&& functor)
{
    auto const value = functor();
    if (!value)
    {
        return hfErrorToInt(value.error());
    }

    if constexpr (Scalar)
    {
        return answerScalar(out, *value);
    }
    else
    {
        return answer(out, value->data(), value->size());
    }
}

template <typename Functor>
std::int32_t
invoke(Functor&& functor)
{
    auto const value = functor();
    if (!value)
    {
        return hfErrorToInt(value.error());
    }

    return *value;
}

}  // namespace

HostContext::HostContext(HostFunctions& hostFunctions) : hostFunctions_{hostFunctions}
{
}

std::int32_t
HostContext::getLedgerSqn(rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invoke<true>(out, [&] { return hostFunctions_.getLedgerSqn(); });
    });
}

std::int32_t
HostContext::getParentLedgerTime(rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invoke<true>(out, [&] { return hostFunctions_.getParentLedgerTime(); });
    });
}

std::int32_t
HostContext::getParentLedgerHash(rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invoke<false>(out, [&] { return hostFunctions_.getParentLedgerHash(); });
    });
}

std::int32_t
HostContext::getBaseFee(rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invoke<true>(out, [&] { return hostFunctions_.getBaseFee(); });
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
            {
                return *enabled;
            }
        }

        static constexpr auto kMaxAmendmentSize = 64UZ;
        if (amendment.size() > kMaxAmendmentSize)
        {
            return hfErrorToInt(HostFunctionError::DataFieldTooLarge);
        }

        auto const name =
            std::string_view{reinterpret_cast<char const*>(amendment.data()), amendment.size()};
        return invoke([&] { return hostFunctions_.isAmendmentEnabled(name); });
    });
}

std::int32_t
HostContext::cacheLedgerObj(rust::Slice<std::uint8_t const> objId, std::int32_t cacheIdx)
    const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        if (objId.size() != uint256::size())
        {
            return hfErrorToInt(HostFunctionError::InvalidParams);
        }
        return invoke([&] {
            return hostFunctions_.cacheLedgerObj(uint256::fromVoid(objId.data()), cacheIdx);
        });
    });
}

std::int32_t
HostContext::getTxField(std::int32_t field, rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithField(field, out, [&](auto const& innerField) {
            return hostFunctions_.getTxField(innerField);
        });
    });
}

std::int32_t
HostContext::getCurrentLedgerObjField(std::int32_t field, rust::Slice<std::uint8_t> out)
    const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithField(field, out, [&](auto const& innerField) {
            return hostFunctions_.getCurrentLedgerObjField(innerField);
        });
    });
}

std::int32_t
HostContext::getLedgerObjField(
    std::int32_t cacheIdx,
    std::int32_t field,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithField(field, out, [&](auto const& innerField) {
            return hostFunctions_.getLedgerObjField(cacheIdx, innerField);
        });
    });
}

std::int32_t
HostContext::getTxNestedField(
    rust::Slice<std::uint8_t const> locator,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithLocator(locator, out, [&](FieldLocator const& fl) {
            return hostFunctions_.getTxNestedField(fl);
        });
    });
}

std::int32_t
HostContext::getCurrentLedgerObjNestedField(
    rust::Slice<std::uint8_t const> locator,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithLocator(locator, out, [&](FieldLocator const& fl) {
            return hostFunctions_.getCurrentLedgerObjNestedField(fl);
        });
    });
}

std::int32_t
HostContext::getLedgerObjNestedField(
    std::int32_t cacheIdx,
    rust::Slice<std::uint8_t const> locator,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithLocator(locator, out, [&](FieldLocator const& fl) {
            return hostFunctions_.getLedgerObjNestedField(cacheIdx, fl);
        });
    });
}

std::int32_t
HostContext::getTxArrayLen(std::int32_t field) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithField(field, [&](auto const& innerField) {
            return hostFunctions_.getTxArrayLen(innerField);
        });
    });
}

std::int32_t
HostContext::getCurrentLedgerObjArrayLen(std::int32_t field) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithField(field, [&](auto const& innerField) {
            return hostFunctions_.getCurrentLedgerObjArrayLen(innerField);
        });
    });
}

std::int32_t
HostContext::getLedgerObjArrayLen(std::int32_t cacheIdx, std::int32_t field) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithField(field, [&](auto const& innerField) {
            return hostFunctions_.getLedgerObjArrayLen(cacheIdx, innerField);
        });
    });
}

std::int32_t
HostContext::getTxNestedArrayLen(rust::Slice<std::uint8_t const> locator) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithLocator(locator, [&](FieldLocator const& fl) {
            return hostFunctions_.getTxNestedArrayLen(fl);
        });
    });
}

std::int32_t
HostContext::getCurrentLedgerObjNestedArrayLen(
    rust::Slice<std::uint8_t const> locator) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithLocator(locator, [&](FieldLocator const& fl) {
            return hostFunctions_.getCurrentLedgerObjNestedArrayLen(fl);
        });
    });
}

std::int32_t
HostContext::getLedgerObjNestedArrayLen(
    std::int32_t cacheIdx,
    rust::Slice<std::uint8_t const> locator) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithLocator(locator, [&](FieldLocator const& fl) {
            return hostFunctions_.getLedgerObjNestedArrayLen(cacheIdx, fl);
        });
    });
}

std::int32_t
HostContext::checkSignature(
    rust::Slice<std::uint8_t const> message,
    rust::Slice<std::uint8_t const> signature,
    rust::Slice<std::uint8_t const> pubkey) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invoke([&] {
            return hostFunctions_.checkSignature(
                Slice{message.data(), message.size()},
                Slice{signature.data(), signature.size()},
                Slice{pubkey.data(), pubkey.size()});
        });
    });
}

std::int32_t
HostContext::accountKeylet(rust::Slice<std::uint8_t const> account, rust::Slice<std::uint8_t> out)
    const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithAccount(account, out, [&](auto const& accountId) {
            return hostFunctions_.accountKeylet(accountId);
        });
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
        {
            return hfErrorToInt(a1.error());
        }

        auto const a2 = parseAsset(asset2);
        if (!a2)
        {
            return hfErrorToInt(a2.error());
        }
        return invoke<false>(out, [&] { return hostFunctions_.ammKeylet(*a1, *a2); });
    });
}

std::int32_t
HostContext::checkKeylet(
    rust::Slice<std::uint8_t const> account,
    std::int32_t seq,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithAccount(account, out, [&](auto const& accountId) {
            return hostFunctions_.checkKeylet(accountId, static_cast<std::uint32_t>(seq));
        });
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
        return invokeWithAccounts(
            subject, issuer, out, [&](auto const& account1, auto const& account2) {
                return hostFunctions_.credentialKeylet(
                    account1, account2, Slice{credentialType.data(), credentialType.size()});
            });
    });
}

std::int32_t
HostContext::delegateKeylet(
    rust::Slice<std::uint8_t const> account,
    rust::Slice<std::uint8_t const> authorize,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithAccounts(
            account, authorize, out, [&](auto const& account1, auto const& account2) {
                return hostFunctions_.delegateKeylet(account1, account2);
            });
    });
}

std::int32_t
HostContext::depositPreauthKeylet(
    rust::Slice<std::uint8_t const> account,
    rust::Slice<std::uint8_t const> authorize,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithAccounts(
            account, authorize, out, [&](auto const& account1, auto const& account2) {
                return hostFunctions_.depositPreauthKeylet(account1, account2);
            });
    });
}

std::int32_t
HostContext::didKeylet(rust::Slice<std::uint8_t const> account, rust::Slice<std::uint8_t> out)
    const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithAccount(account, out, [&](auto const& accountId) {
            return hostFunctions_.didKeylet(accountId);
        });
    });
}

std::int32_t
HostContext::escrowKeylet(
    rust::Slice<std::uint8_t const> account,
    std::int32_t seq,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithAccount(account, out, [&](auto const& accountId) {
            return hostFunctions_.escrowKeylet(accountId, static_cast<std::uint32_t>(seq));
        });
    });
}

std::int32_t
HostContext::trustLineKeylet(
    rust::Slice<std::uint8_t const> account1,
    rust::Slice<std::uint8_t const> account2,
    rust::Slice<std::uint8_t const> currency,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        if (currency.size() != Currency::size())
        {
            return hfErrorToInt(HostFunctionError::InvalidParams);
        }

        return invokeWithAccounts(
            account1, account2, out, [&](auto const& innerAccount1, auto const& innerAccount2) {
                return hostFunctions_.trustLineKeylet(
                    innerAccount1, innerAccount2, Currency::fromVoid(currency.data()));
            });
    });
}

std::int32_t
HostContext::mptokenIssuanceKeylet(
    rust::Slice<std::uint8_t const> issuer,
    std::int32_t seq,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithAccount(issuer, out, [&](auto const& accountId) {
            return hostFunctions_.mptokenIssuanceKeylet(accountId, static_cast<std::uint32_t>(seq));
        });
    });
}

std::int32_t
HostContext::mptokenKeylet(
    rust::Slice<std::uint8_t const> mptid,
    rust::Slice<std::uint8_t const> holder,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        if (mptid.size() != MPTID::size() || holder.size() != AccountID::size())
        {
            return hfErrorToInt(HostFunctionError::InvalidParams);
        }
        return invoke<false>(out, [&] {
            return hostFunctions_.mptokenKeylet(
                MPTID::fromVoid(mptid.data()), AccountID::fromVoid(holder.data()));
        });
    });
}

std::int32_t
HostContext::nftokenOfferKeylet(
    rust::Slice<std::uint8_t const> account,
    std::int32_t seq,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithAccount(account, out, [&](auto const& accountId) {
            return hostFunctions_.nftokenOfferKeylet(accountId, static_cast<std::uint32_t>(seq));
        });
    });
}

std::int32_t
HostContext::offerKeylet(
    rust::Slice<std::uint8_t const> account,
    std::int32_t seq,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithAccount(account, out, [&](auto const& accountId) {
            return hostFunctions_.offerKeylet(accountId, static_cast<std::uint32_t>(seq));
        });
    });
}

std::int32_t
HostContext::oracleKeylet(
    rust::Slice<std::uint8_t const> account,
    std::int32_t docId,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithAccount(account, out, [&](auto const& accountId) {
            return hostFunctions_.oracleKeylet(accountId, static_cast<std::uint32_t>(docId));
        });
    });
}

std::int32_t
HostContext::paychannelKeylet(
    rust::Slice<std::uint8_t const> account,
    rust::Slice<std::uint8_t const> destination,
    std::int32_t seq,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithAccounts(
            account, destination, out, [&](auto const& account1, auto const& account2) {
                return hostFunctions_.paychannelKeylet(
                    account1, account2, static_cast<std::uint32_t>(seq));
            });
    });
}

std::int32_t
HostContext::permissionedDomainKeylet(
    rust::Slice<std::uint8_t const> account,
    std::int32_t seq,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithAccount(account, out, [&](auto const& accountId) {
            return hostFunctions_.permissionedDomainKeylet(
                accountId, static_cast<std::uint32_t>(seq));
        });
    });
}

std::int32_t
HostContext::signerListKeylet(
    rust::Slice<std::uint8_t const> account,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithAccount(account, out, [&](auto const& accountId) {
            return hostFunctions_.signerListKeylet(accountId);
        });
    });
}

std::int32_t
HostContext::ticketKeylet(
    rust::Slice<std::uint8_t const> account,
    std::int32_t seq,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithAccount(account, out, [&](auto const& accountId) {
            return hostFunctions_.ticketKeylet(accountId, static_cast<std::uint32_t>(seq));
        });
    });
}

std::int32_t
HostContext::vaultKeylet(
    rust::Slice<std::uint8_t const> account,
    std::int32_t seq,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeWithAccount(account, out, [&](auto const& accountId) {
            return hostFunctions_.vaultKeylet(accountId, static_cast<std::uint32_t>(seq));
        });
    });
}

std::int32_t
HostContext::sha512Half(rust::Slice<std::uint8_t const> data, rust::Slice<std::uint8_t> out)
    const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invoke<false>(out, [&] {
            return hostFunctions_.computeSha512HalfHash(Slice{data.data(), data.size()});
        });
    });
}

std::int32_t
HostContext::trace(rust::Str msg, rust::Slice<std::uint8_t const> data, bool asHex) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invoke([&] {
            return hostFunctions_.trace(
                std::string_view{msg.data(), msg.size()}, Slice{data.data(), data.size()}, asHex);
        });
    });
}

std::int32_t
HostContext::traceNum(rust::Str msg, std::int64_t number) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invoke([&] {
            return hostFunctions_.traceNum(std::string_view{msg.data(), msg.size()}, number);
        });
    });
}

std::int32_t
HostContext::updateData(rust::Slice<std::uint8_t const> data) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invoke([&] { return hostFunctions_.updateData(Slice{data.data(), data.size()}); });
    });
}

std::int32_t
HostContext::getNFT(
    rust::Slice<std::uint8_t const> account,
    rust::Slice<std::uint8_t const> nftId,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        if (account.size() != AccountID::size())
        {
            return hfErrorToInt(HostFunctionError::InvalidParams);
        }
        return invokeNFT<false>(nftId, out, [&](auto const& nft) {
            return hostFunctions_.getNFT(AccountID::fromVoid(account.data()), nft);
        });
    });
}

std::int32_t
HostContext::getNFTIssuer(rust::Slice<std::uint8_t const> nftId, rust::Slice<std::uint8_t> out)
    const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeNFT<false>(
            nftId, out, [&](auto const& nft) { return hostFunctions_.getNFTIssuer(nft); });
    });
}

std::int32_t
HostContext::getNFTTaxon(rust::Slice<std::uint8_t const> nftId, rust::Slice<std::uint8_t> out)
    const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeNFT<true>(
            nftId, out, [&](auto const& nft) { return hostFunctions_.getNFTTaxon(nft); });
    });
}

std::int32_t
HostContext::getNFTFlags(rust::Slice<std::uint8_t const> nftId) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeNFT(nftId, [&](auto const& nft) { return hostFunctions_.getNFTFlags(nft); });
    });
}

std::int32_t
HostContext::getNFTTransferFee(rust::Slice<std::uint8_t const> nftId) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeNFT(
            nftId, [&](auto const& nft) { return hostFunctions_.getNFTTransferFee(nft); });
    });
}

std::int32_t
HostContext::getNFTSequence(rust::Slice<std::uint8_t const> nftId, rust::Slice<std::uint8_t> out)
    const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invokeNFT<true>(
            nftId, out, [&](auto const& nft) { return hostFunctions_.getNFTSequence(nft); });
    });
}

std::int32_t
HostContext::floatFromInt(std::int64_t x, std::int32_t mode, rust::Slice<std::uint8_t> out)
    const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invoke<false>(out, [&] { return hostFunctions_.floatFromInt(x, mode); });
    });
}

std::int32_t
HostContext::floatFromUint(
    rust::Slice<std::uint8_t const> x,
    std::int32_t mode,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        auto const parsed = parseUint64(x);
        if (!parsed)
        {
            return hfErrorToInt(parsed.error());
        }
        return invoke<false>(out, [&] { return hostFunctions_.floatFromUint(*parsed, mode); });
    });
}

std::int32_t
HostContext::floatFromSTAmount(
    rust::Slice<std::uint8_t const> amount,
    std::int32_t mode,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        auto const parsed = parseST<STAmount>(amount);
        if (!parsed)
        {
            return hfErrorToInt(parsed.error());
        }
        return invoke<false>(out, [&] { return hostFunctions_.floatFromSTAmount(*parsed, mode); });
    });
}

std::int32_t
HostContext::floatFromSTNumber(
    rust::Slice<std::uint8_t const> number,
    std::int32_t mode,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        auto const parsed = parseST<STNumber>(number);
        if (!parsed)
        {
            return hfErrorToInt(parsed.error());
        }
        return invoke<false>(out, [&] { return hostFunctions_.floatFromSTNumber(*parsed, mode); });
    });
}

std::int32_t
HostContext::floatToInt(
    rust::Slice<std::uint8_t const> x,
    std::int32_t mode,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invoke<true>(
            out, [&] { return hostFunctions_.floatToInt(Slice{x.data(), x.size()}, mode); });
    });
}

std::int32_t
HostContext::floatToMantExp(
    rust::Slice<std::uint8_t const> x,
    rust::Slice<std::uint8_t> mantissaOut,
    rust::Slice<std::uint8_t> exponentOut) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        auto const value = hostFunctions_.floatToMantExp(Slice{x.data(), x.size()});
        if (!value)
        {
            return hfErrorToInt(value.error());
        }

        // The engine copies each region only if the whole value fits, so writing the
        // true lengths here and summing them matches its accounting.
        auto const r1 = answerScalar(mantissaOut, value->first);
        auto const r2 = answerScalar(exponentOut, value->second);
        return r1 + r2;
    });
}

std::int32_t
HostContext::floatFromMantExp(
    std::int64_t mantissa,
    std::int32_t exponent,
    std::int32_t mode,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invoke<false>(
            out, [&] { return hostFunctions_.floatFromMantExp(mantissa, exponent, mode); });
    });
}

std::int32_t
HostContext::floatCompare(rust::Slice<std::uint8_t const> x, rust::Slice<std::uint8_t const> y)
    const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invoke([&] {
            return hostFunctions_.floatCompare(
                Slice{x.data(), x.size()}, Slice{y.data(), y.size()});
        });
    });
}

std::int32_t
HostContext::floatAdd(
    rust::Slice<std::uint8_t const> x,
    rust::Slice<std::uint8_t const> y,
    std::int32_t mode,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invoke<false>(out, [&] {
            return hostFunctions_.floatAdd(
                Slice{x.data(), x.size()}, Slice{y.data(), y.size()}, mode);
        });
    });
}

std::int32_t
HostContext::floatSubtract(
    rust::Slice<std::uint8_t const> x,
    rust::Slice<std::uint8_t const> y,
    std::int32_t mode,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invoke<false>(out, [&] {
            return hostFunctions_.floatSubtract(
                Slice{x.data(), x.size()}, Slice{y.data(), y.size()}, mode);
        });
    });
}

std::int32_t
HostContext::floatMultiply(
    rust::Slice<std::uint8_t const> x,
    rust::Slice<std::uint8_t const> y,
    std::int32_t mode,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invoke<false>(out, [&] {
            return hostFunctions_.floatMultiply(
                Slice{x.data(), x.size()}, Slice{y.data(), y.size()}, mode);
        });
    });
}

std::int32_t
HostContext::floatDivide(
    rust::Slice<std::uint8_t const> x,
    rust::Slice<std::uint8_t const> y,
    std::int32_t mode,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invoke<false>(out, [&] {
            return hostFunctions_.floatDivide(
                Slice{x.data(), x.size()}, Slice{y.data(), y.size()}, mode);
        });
    });
}

std::int32_t
HostContext::floatRoot(
    rust::Slice<std::uint8_t const> x,
    std::int32_t n,
    std::int32_t mode,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invoke<false>(
            out, [&] { return hostFunctions_.floatRoot(Slice{x.data(), x.size()}, n, mode); });
    });
}

std::int32_t
HostContext::floatPower(
    rust::Slice<std::uint8_t const> x,
    std::int32_t n,
    std::int32_t mode,
    rust::Slice<std::uint8_t> out) const noexcept
{
    return guarded(hostFunctions_.getJournal(), kHostInternal, [&] {
        return invoke<false>(
            out, [&] { return hostFunctions_.floatPower(Slice{x.data(), x.size()}, n, mode); });
    });
}

}  // namespace xrpl
