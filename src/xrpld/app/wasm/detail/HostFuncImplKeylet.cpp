#include <xrpld/app/wasm/HostFuncImpl.h>

#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/digest.h>

namespace xrpl {

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::accountKeylet(AccountID const& account)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    auto const keylet = keylet::account(account);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::ammKeylet(Asset const& issue1, Asset const& issue2)
{
    if (issue1 == issue2)
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    // note: this should be removed with the MPT DEX amendment
    if (issue1.holds<MPTIssue>() || issue2.holds<MPTIssue>())
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    auto const keylet = keylet::amm(issue1, issue2);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::checkKeylet(AccountID const& account, std::uint32_t seq)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    auto const keylet = keylet::check(account, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::credentialKeylet(AccountID const& subject, AccountID const& issuer, Slice const& credentialType)
{
    if (!subject || !issuer)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);

    if (credentialType.empty() || credentialType.size() > maxCredentialTypeLength)
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    auto const keylet = keylet::credential(subject, issuer, credentialType);

    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::didKeylet(AccountID const& account)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    auto const keylet = keylet::did(account);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::delegateKeylet(AccountID const& account, AccountID const& authorize)
{
    if (!account || !authorize)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    if (account == authorize)
        return Unexpected(HostFunctionError::INVALID_PARAMS);
    auto const keylet = keylet::delegate(account, authorize);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::depositPreauthKeylet(AccountID const& account, AccountID const& authorize)
{
    if (!account || !authorize)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    if (account == authorize)
        return Unexpected(HostFunctionError::INVALID_PARAMS);
    auto const keylet = keylet::depositPreauth(account, authorize);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::escrowKeylet(AccountID const& account, std::uint32_t seq)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    auto const keylet = keylet::escrow(account, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::lineKeylet(AccountID const& account1, AccountID const& account2, Currency const& currency)
{
    if (!account1 || !account2)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    if (account1 == account2)
        return Unexpected(HostFunctionError::INVALID_PARAMS);
    if (currency.isZero())
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    auto const keylet = keylet::line(account1, account2, currency);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::mptIssuanceKeylet(AccountID const& issuer, std::uint32_t seq)
{
    if (!issuer)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);

    auto const keylet = keylet::mptIssuance(seq, issuer);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::mptokenKeylet(MPTID const& mptid, AccountID const& holder)
{
    if (!mptid)
        return Unexpected(HostFunctionError::INVALID_PARAMS);
    if (!holder)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);

    auto const keylet = keylet::mptoken(mptid, holder);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::nftOfferKeylet(AccountID const& account, std::uint32_t seq)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    auto const keylet = keylet::nftoffer(account, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::offerKeylet(AccountID const& account, std::uint32_t seq)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    auto const keylet = keylet::offer(account, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::oracleKeylet(AccountID const& account, std::uint32_t documentId)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    auto const keylet = keylet::oracle(account, documentId);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::paychanKeylet(AccountID const& account, AccountID const& destination, std::uint32_t seq)
{
    if (!account || !destination)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    if (account == destination)
        return Unexpected(HostFunctionError::INVALID_PARAMS);
    auto const keylet = keylet::payChan(account, destination, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::permissionedDomainKeylet(AccountID const& account, std::uint32_t seq)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    auto const keylet = keylet::permissionedDomain(account, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::signersKeylet(AccountID const& account)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    auto const keylet = keylet::signers(account);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::ticketKeylet(AccountID const& account, std::uint32_t seq)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    auto const keylet = keylet::ticket(account, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::vaultKeylet(AccountID const& account, std::uint32_t seq)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    auto const keylet = keylet::vault(account, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

}  // namespace xrpl
