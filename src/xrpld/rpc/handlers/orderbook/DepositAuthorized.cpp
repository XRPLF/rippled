#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace xrpl {

/** @file
 *  Implements the `deposit_authorized` RPC handler.
 *
 *  Answers whether a payment from a source account to a destination account
 *  would currently be permitted under the destination's DepositAuth rules,
 *  optionally considering a caller-supplied set of credential IDs. This is a
 *  pure read-only query — no ledger state is mutated.
 */

/** Determine whether a source account may deposit to a destination account.
 *
 *  Resolves the requested ledger, verifies that both accounts exist, inspects
 *  the destination's `lsfDepositAuth` flag, and evaluates one or both of two
 *  authorization paths:
 *
 *  1. **Account preauthorization** — the destination has posted a
 *     `DepositPreauth` object keyed by `(dstAcct, srcAcct)`.
 *  2. **Credential preauthorization** — the caller supplies credential IDs
 *     (`credentials` array of hex-encoded `uint256` hashes), each credential
 *     is valid for `source_account`, and the destination has posted a
 *     `DepositPreauth` object keyed by the canonical sorted set of
 *     `(issuer, credentialType)` pairs derived from those credentials.
 *
 *  Either path is sufficient; they are OR-ed together. Self-deposits (source
 *  == destination) are always authorized regardless of the DepositAuth flag,
 *  mirroring the rule enforced at transaction-apply time.
 *
 *  **Credential validation** (when `credentials` is present): each credential
 *  must exist in the ledger (`lsfAccepted` flag set), must not be expired as
 *  of `parentCloseTime`, and must belong to `source_account`. Duplicate
 *  `(issuer, credentialType)` pairs are rejected. The array is capped at
 *  `kMAX_CREDENTIALS_ARRAY_SIZE` (8) entries.
 *
 *  **`lifeExtender` invariant**: the `sorted` set holds `Slice` views into
 *  `sfCredentialType` data inside SLE objects. `lifeExtender` keeps those
 *  SLEs alive for the lifetime of `sorted` so the `Slice` references remain
 *  valid through the `keylet::depositPreauth` call.
 *
 *  @note Results reflect the state of the selected ledger using
 *      `parentCloseTime`. A credential that is valid at query time may expire
 *      before the actual transaction is submitted; callers should re-query or
 *      submit promptly. This handler uses a `ReadView` and cannot delete
 *      expired credentials as `verifyDepositPreauth` (on an `ApplyView`) does.
 *
 *  @param context  RPC dispatch context; `context.params` must contain
 *      `source_account` (Base58 account address string),
 *      `destination_account` (Base58 account address string), and optionally
 *      `ledger_hash`/`ledger_index` for ledger selection and `credentials`
 *      (non-empty array of hex-encoded credential object IDs, at most
 *      `kMAX_CREDENTIALS_ARRAY_SIZE` entries).
 *  @return JSON object containing `source_account`, `destination_account`,
 *      `deposit_authorized` (bool), and `credentials` (if supplied). On
 *      error, returns a JSON error object with one of: `rpcINVALID_PARAMS`,
 *      `rpcACT_MALFORMED`, `rpcSRC_ACT_NOT_FOUND`, `rpcDST_ACT_NOT_FOUND`,
 *      or `rpcBAD_CREDENTIALS`.
 */
json::Value
doDepositAuthorized(RPC::JsonContext& context)
{
    json::Value const& params = context.params;

    if (!params.isMember(jss::source_account))
        return RPC::missingFieldError(jss::source_account);
    if (!params[jss::source_account].isString())
    {
        return RPC::makeError(
            RpcInvalidParams, RPC::expectedFieldMessage(jss::source_account, "a string"));
    }

    auto srcID = parseBase58<AccountID>(params[jss::source_account].asString());
    if (!srcID)
        return rpcError(RpcActMalformed);
    auto const srcAcct{srcID.value()};

    if (!params.isMember(jss::destination_account))
        return RPC::missingFieldError(jss::destination_account);
    if (!params[jss::destination_account].isString())
    {
        return RPC::makeError(
            RpcInvalidParams, RPC::expectedFieldMessage(jss::destination_account, "a string"));
    }

    auto dstID = parseBase58<AccountID>(params[jss::destination_account].asString());
    if (!dstID)
        return rpcError(RpcActMalformed);
    auto const dstAcct{dstID.value()};

    std::shared_ptr<ReadView const> ledger;
    json::Value result = RPC::lookupLedger(ledger, context);

    if (!ledger)
        return result;

    if (!ledger->exists(keylet::account(srcAcct)))
    {
        RPC::injectError(RpcSrcActNotFound, result);
        return result;
    }

    auto const sleDest = ledger->read(keylet::account(dstAcct));
    if (!sleDest)
    {
        RPC::injectError(RpcDstActNotFound, result);
        return result;
    }

    bool const reqAuth = ((sleDest->getFlags() & lsfDepositAuth) != 0u) && (srcAcct != dstAcct);
    bool const credentialsPresent = params.isMember(jss::credentials);

    std::set<std::pair<AccountID, Slice>> sorted;
    std::vector<std::shared_ptr<SLE const>> lifeExtender;
    if (credentialsPresent)
    {
        auto const& creds(params[jss::credentials]);
        if (!creds.isArray() || !creds)
        {
            return RPC::makeError(
                RpcInvalidParams,
                RPC::expectedFieldMessage(
                    jss::credentials, "is non-empty array of CredentialID(hash256)"));
        }
        if (creds.size() > kMAX_CREDENTIALS_ARRAY_SIZE)
        {
            return RPC::makeError(
                RpcInvalidParams, RPC::expectedFieldMessage(jss::credentials, "array too long"));
        }

        lifeExtender.reserve(creds.size());
        for (auto const& jo : creds)
        {
            if (!jo.isString())
            {
                return RPC::makeError(
                    RpcInvalidParams,
                    RPC::expectedFieldMessage(
                        jss::credentials, "an array of CredentialID(hash256)"));
            }

            uint256 credH;
            auto const credS = jo.asString();
            if (!credH.parseHex(credS))
            {
                return RPC::makeError(
                    RpcInvalidParams,
                    RPC::expectedFieldMessage(
                        jss::credentials, "an array of CredentialID(hash256)"));
            }

            std::shared_ptr<SLE const> sleCred = ledger->read(keylet::credential(credH));
            if (!sleCred)
            {
                RPC::injectError(RpcBadCredentials, "credentials don't exist", result);
                return result;
            }

            if ((sleCred->getFlags() & lsfAccepted) == 0u)
            {
                RPC::injectError(RpcBadCredentials, "credentials aren't accepted", result);
                return result;
            }

            if (credentials::checkExpired(*sleCred, ledger->header().parentCloseTime))
            {
                RPC::injectError(RpcBadCredentials, "credentials are expired", result);
                return result;
            }

            if ((*sleCred)[sfSubject] != srcAcct)
            {
                RPC::injectError(
                    RpcBadCredentials, "credentials doesn't belong to the root account", result);
                return result;
            }

            auto [it, ins] = sorted.emplace((*sleCred)[sfIssuer], (*sleCred)[sfCredentialType]);
            if (!ins)
            {
                RPC::injectError(RpcBadCredentials, "duplicates in credentials", result);
                return result;
            }
            lifeExtender.push_back(std::move(sleCred));
        }
    }

    bool depositAuthorized = true;
    if (reqAuth)
    {
        depositAuthorized = ledger->exists(keylet::depositPreauth(dstAcct, srcAcct)) ||
            (credentialsPresent && ledger->exists(keylet::depositPreauth(dstAcct, sorted)));
    }

    result[jss::source_account] = params[jss::source_account].asString();
    result[jss::destination_account] = params[jss::destination_account].asString();
    if (credentialsPresent)
        result[jss::credentials] = params[jss::credentials];

    result[jss::deposit_authorized] = depositAuthorized;
    return result;
}

}  // namespace xrpl
