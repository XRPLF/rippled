#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/wasm/HostFuncImpl.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <cstdint>
#include <expected>

namespace xrpl {

std::expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::accountKeylet(AccountID const& account) const
{
    if (!account)
        return std::unexpected(HostFunctionError::InvalidAccount);
    auto const keylet = keylet::account(account);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

std::expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::ammKeylet(Asset const& issue1, Asset const& issue2) const
{
    if (issue1 == issue2)
        return std::unexpected(HostFunctionError::InvalidParams);

    // note: this should be removed with the MPT DEX amendment
    if (issue1.holds<MPTIssue>() || issue2.holds<MPTIssue>())
        return std::unexpected(HostFunctionError::InvalidParams);

    auto const keylet = keylet::amm(issue1, issue2);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

std::expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::checkKeylet(AccountID const& account, std::uint32_t seq) const
{
    if (!account)
        return std::unexpected(HostFunctionError::InvalidAccount);
    auto const keylet = keylet::check(account, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

std::expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::credentialKeylet(
    AccountID const& subject,
    AccountID const& issuer,
    Slice const& credentialType) const
{
    if (!subject || !issuer)
        return std::unexpected(HostFunctionError::InvalidAccount);

    if (credentialType.empty() || credentialType.size() > kMaxCredentialTypeLength)
        return std::unexpected(HostFunctionError::InvalidParams);

    auto const keylet = keylet::credential(subject, issuer, credentialType);

    return Bytes{keylet.key.begin(), keylet.key.end()};
}

std::expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::didKeylet(AccountID const& account) const
{
    if (!account)
        return std::unexpected(HostFunctionError::InvalidAccount);
    auto const keylet = keylet::did(account);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

std::expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::delegateKeylet(AccountID const& account, AccountID const& authorize) const
{
    if (!account || !authorize)
        return std::unexpected(HostFunctionError::InvalidAccount);
    if (account == authorize)
        return std::unexpected(HostFunctionError::InvalidParams);
    auto const keylet = keylet::delegate(account, authorize);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

std::expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::depositPreauthKeylet(AccountID const& account, AccountID const& authorize)
    const
{
    if (!account || !authorize)
        return std::unexpected(HostFunctionError::InvalidAccount);
    if (account == authorize)
        return std::unexpected(HostFunctionError::InvalidParams);
    auto const keylet = keylet::depositPreauth(account, authorize);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

std::expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::escrowKeylet(AccountID const& account, std::uint32_t seq) const
{
    if (!account)
        return std::unexpected(HostFunctionError::InvalidAccount);
    auto const keylet = keylet::escrow(account, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

std::expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::trustLineKeylet(
    AccountID const& account1,
    AccountID const& account2,
    Currency const& currency) const
{
    if (!account1 || !account2)
        return std::unexpected(HostFunctionError::InvalidAccount);
    if (account1 == account2)
        return std::unexpected(HostFunctionError::InvalidParams);
    if (currency.isZero())
        return std::unexpected(HostFunctionError::InvalidParams);

    auto const keylet = keylet::trustLine(account1, account2, currency);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

std::expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::mptokenIssuanceKeylet(AccountID const& issuer, std::uint32_t seq) const
{
    if (!issuer)
        return std::unexpected(HostFunctionError::InvalidAccount);

    auto const keylet = keylet::mptokenIssuance(seq, issuer);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

std::expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::mptokenKeylet(MPTID const& mptid, AccountID const& holder) const
{
    if (!mptid)
        return std::unexpected(HostFunctionError::InvalidParams);
    if (!holder)
        return std::unexpected(HostFunctionError::InvalidAccount);

    auto const keylet = keylet::mptoken(mptid, holder);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

std::expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::nftokenOfferKeylet(AccountID const& account, std::uint32_t seq) const
{
    if (!account)
        return std::unexpected(HostFunctionError::InvalidAccount);
    auto const keylet = keylet::nftokenOffer(account, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

std::expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::offerKeylet(AccountID const& account, std::uint32_t seq) const
{
    if (!account)
        return std::unexpected(HostFunctionError::InvalidAccount);
    auto const keylet = keylet::offer(account, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

std::expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::oracleKeylet(AccountID const& account, std::uint32_t documentId) const
{
    if (!account)
        return std::unexpected(HostFunctionError::InvalidAccount);
    auto const keylet = keylet::oracle(account, documentId);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

std::expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::paychannelKeylet(
    AccountID const& account,
    AccountID const& destination,
    std::uint32_t seq) const
{
    if (!account || !destination)
        return std::unexpected(HostFunctionError::InvalidAccount);
    if (account == destination)
        return std::unexpected(HostFunctionError::InvalidParams);
    auto const keylet = keylet::payChannel(account, destination, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

std::expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::permissionedDomainKeylet(AccountID const& account, std::uint32_t seq) const
{
    if (!account)
        return std::unexpected(HostFunctionError::InvalidAccount);
    auto const keylet = keylet::permissionedDomain(account, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

std::expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::signerListKeylet(AccountID const& account) const
{
    if (!account)
        return std::unexpected(HostFunctionError::InvalidAccount);
    auto const keylet = keylet::signerList(account);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

std::expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::ticketKeylet(AccountID const& account, std::uint32_t seq) const
{
    if (!account)
        return std::unexpected(HostFunctionError::InvalidAccount);
    auto const keylet = keylet::ticket(account, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

std::expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::vaultKeylet(AccountID const& account, std::uint32_t seq) const
{
    if (!account)
        return std::unexpected(HostFunctionError::InvalidAccount);
    auto const keylet = keylet::vault(account, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

}  // namespace xrpl
