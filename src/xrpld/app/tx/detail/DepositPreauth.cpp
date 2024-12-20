//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpld/app/misc/CredentialHelpers.h>
#include <xrpld/app/tx/detail/DepositPreauth.h>
#include <xrpld/ledger/View.h>
#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/st.h>

#include <optional>
#include <unordered_set>

namespace ripple {

NotTEC
DepositPreauth::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureDepositPreauth))
        return temDISABLED;

    bool const authArrPresent = ctx.tx.isFieldPresent(sfAuthorizeCredentials);
    bool const unauthArrPresent =
        ctx.tx.isFieldPresent(sfUnauthorizeCredentials);
    int const authCredPresent =
        static_cast<int>(authArrPresent) + static_cast<int>(unauthArrPresent);

    if (authCredPresent && !ctx.rules.enabled(featureCredentials))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto& tx = ctx.tx;

    if (tx.getFlags() & tfUniversalMask)
    {
        JLOG(ctx.j.trace()) << "Malformed transaction: Invalid flags set.";
        return temINVALID_FLAG;
    }

    auto const optAuth = ctx.tx[~sfAuthorize];
    auto const optUnauth = ctx.tx[~sfUnauthorize];
    int const authPresent = static_cast<int>(optAuth.has_value()) +
        static_cast<int>(optUnauth.has_value());

    if (authPresent + authCredPresent != 1)
    {
        // There can only be 1 field out of 4 or the transaction is malformed.
        JLOG(ctx.j.trace())
            << "Malformed transaction: "
               "Invalid Authorize and Unauthorize field combination.";
        return temMALFORMED;
    }

    if (authPresent)
    {
        // Make sure that the passed account is valid.
        AccountID const& target(optAuth ? *optAuth : *optUnauth);
        if (!target)
        {
            JLOG(ctx.j.trace())
                << "Malformed transaction: Authorized or Unauthorized "
                   "field zeroed.";
            return temINVALID_ACCOUNT_ID;
        }

        // An account may not preauthorize itself.
        if (optAuth && (target == ctx.tx[sfAccount]))
        {
            JLOG(ctx.j.trace())
                << "Malformed transaction: Attempting to DepositPreauth self.";
            return temCANNOT_PREAUTH_SELF;
        }
    }
    else
    {
        STArray const& arr(ctx.tx.getFieldArray(
            authArrPresent ? sfAuthorizeCredentials
                           : sfUnauthorizeCredentials));
        if (arr.empty() || (arr.size() > maxCredentialsArraySize))
        {
            JLOG(ctx.j.trace()) << "Malformed transaction: "
                                   "Invalid AuthorizeCredentials size: "
                                << arr.size();
            return temMALFORMED;
        }

        std::unordered_set<uint256> duplicates;
        for (auto const& o : arr)
        {
            auto const& issuer(o[sfIssuer]);
            if (!issuer)
            {
                JLOG(ctx.j.trace())
                    << "Malformed transaction: "
                       "AuthorizeCredentials Issuer account is invalid.";
                return temINVALID_ACCOUNT_ID;
            }

            auto const ct = o[sfCredentialType];
            if (ct.empty() || (ct.size() > maxCredentialTypeLength))
            {
                JLOG(ctx.j.trace())
                    << "Malformed transaction: invalid size of CredentialType.";
                return temMALFORMED;
            }

            auto [it, ins] = duplicates.insert(sha512Half(issuer, ct));
            if (!ins)
            {
                JLOG(ctx.j.trace())
                    << "Malformed transaction: duplicates in credentials.";
                return temMALFORMED;
            }
        }
    }

    return preflight2(ctx);
}

TER
DepositPreauth::preclaim(PreclaimContext const& ctx)
{
    AccountID const account(ctx.tx[sfAccount]);

    // Determine which operation we're performing: authorizing or unauthorizing.
    if (ctx.tx.isFieldPresent(sfAuthorize))
    {
        // Verify that the Authorize account is present in the ledger.
        AccountID const auth{ctx.tx[sfAuthorize]};
        if (!ctx.view.exists(keylet::account(auth)))
            return tecNO_TARGET;

        // Verify that the Preauth entry they asked to add is not already
        // in the ledger.
        if (ctx.view.exists(keylet::depositPreauth(account, auth)))
            return tecDUPLICATE;
    }
    else if (ctx.tx.isFieldPresent(sfUnauthorize))
    {
        // Verify that the Preauth entry they asked to remove is in the ledger.
        if (!ctx.view.exists(
                keylet::depositPreauth(account, ctx.tx[sfUnauthorize])))
            return tecNO_ENTRY;
    }
    else if (ctx.tx.isFieldPresent(sfAuthorizeCredentials))
    {
        STArray const& authCred(ctx.tx.getFieldArray(sfAuthorizeCredentials));
        std::set<std::pair<AccountID, Slice>> sorted;
        for (auto const& o : authCred)
        {
            auto const& issuer = o[sfIssuer];
            if (!ctx.view.exists(keylet::account(issuer)))
                return tecNO_ISSUER;
            auto [it, ins] = sorted.emplace(issuer, o[sfCredentialType]);
            if (!ins)
                return tefINTERNAL;
        }

        // Verify that the Preauth entry they asked to add is not already
        // in the ledger.
        if (ctx.view.exists(keylet::depositPreauth(account, sorted)))
            return tecDUPLICATE;
    }
    else if (ctx.tx.isFieldPresent(sfUnauthorizeCredentials))
    {
        // Verify that the Preauth entry is in the ledger.
        if (!ctx.view.exists(keylet::depositPreauth(
                account,
                credentials::makeSorted(
                    ctx.tx.getFieldArray(sfUnauthorizeCredentials)))))
            return tecNO_ENTRY;
    }
    return tesSUCCESS;
}

TER
DepositPreauth::doApply()
{
    if (ctx_.tx.isFieldPresent(sfAuthorize))
    {
        auto const sleOwner = view().peek(keylet::account(account_));
        if (!sleOwner)
            return {tefINTERNAL};

        // A preauth counts against the reserve of the issuing account, but we
        // check the starting balance because we want to allow dipping into the
        // reserve to pay fees.
        {
            STAmount const reserve{view().fees().accountReserve(
                sleOwner->getFieldU32(sfOwnerCount) + 1)};

            if (mPriorBalance < reserve)
                return tecINSUFFICIENT_RESERVE;
        }

        // Preclaim already verified that the Preauth entry does not yet exist.
        // Create and populate the Preauth entry.
        AccountID const auth{ctx_.tx[sfAuthorize]};
        Keylet const preauthKeylet = keylet::depositPreauth(account_, auth);
        auto slePreauth = std::make_shared<SLE>(preauthKeylet);

        slePreauth->setAccountID(sfAccount, account_);
        slePreauth->setAccountID(sfAuthorize, auth);
        view().insert(slePreauth);

        auto const page = view().dirInsert(
            keylet::ownerDir(account_),
            preauthKeylet,
            describeOwnerDir(account_));

        JLOG(j_.trace()) << "Adding DepositPreauth to owner directory "
                         << to_string(preauthKeylet.key) << ": "
                         << (page ? "success" : "failure");

        if (!page)
            return tecDIR_FULL;

        slePreauth->setFieldU64(sfOwnerNode, *page);

        // If we succeeded, the new entry counts against the creator's reserve.
        adjustOwnerCount(view(), sleOwner, 1, j_);
    }
    else if (ctx_.tx.isFieldPresent(sfUnauthorize))
    {
        auto const preauth =
            keylet::depositPreauth(account_, ctx_.tx[sfUnauthorize]);

        return DepositPreauth::removeFromLedger(view(), preauth.key, j_);
    }
    else if (ctx_.tx.isFieldPresent(sfAuthorizeCredentials))
    {
        auto const sleOwner = view().peek(keylet::account(account_));
        if (!sleOwner)
            return tefINTERNAL;

        // A preauth counts against the reserve of the issuing account, but we
        // check the starting balance because we want to allow dipping into the
        // reserve to pay fees.
        {
            STAmount const reserve{view().fees().accountReserve(
                sleOwner->getFieldU32(sfOwnerCount) + 1)};

            if (mPriorBalance < reserve)
                return tecINSUFFICIENT_RESERVE;
        }

        // Preclaim already verified that the Preauth entry does not yet exist.
        // Create and populate the Preauth entry.

        auto const sortedTX = credentials::makeSorted(
            ctx_.tx.getFieldArray(sfAuthorizeCredentials));
        STArray sortedLE(sfAuthorizeCredentials, sortedTX.size());
        for (auto const& p : sortedTX)
        {
            auto cred = STObject::makeInnerObject(sfCredential);
            cred.setAccountID(sfIssuer, p.first);
            cred.setFieldVL(sfCredentialType, p.second);
            sortedLE.push_back(std::move(cred));
        }

        Keylet const preauthKey = keylet::depositPreauth(account_, sortedTX);
        auto slePreauth = std::make_shared<SLE>(preauthKey);
        if (!slePreauth)
            return tefINTERNAL;

        slePreauth->setAccountID(sfAccount, account_);
        slePreauth->peekFieldArray(sfAuthorizeCredentials) =
            std::move(sortedLE);

        view().insert(slePreauth);

        auto const page = view().dirInsert(
            keylet::ownerDir(account_), preauthKey, describeOwnerDir(account_));

        JLOG(j_.trace()) << "Adding DepositPreauth to owner directory "
                         << to_string(preauthKey.key) << ": "
                         << (page ? "success" : "failure");

        if (!page)
            return tecDIR_FULL;

        slePreauth->setFieldU64(sfOwnerNode, *page);

        // If we succeeded, the new entry counts against the creator's reserve.
        adjustOwnerCount(view(), sleOwner, 1, j_);
    }
    else if (ctx_.tx.isFieldPresent(sfUnauthorizeCredentials))
    {
        auto const preauthKey = keylet::depositPreauth(
            account_,
            credentials::makeSorted(
                ctx_.tx.getFieldArray(sfUnauthorizeCredentials)));
        return DepositPreauth::removeFromLedger(view(), preauthKey.key, j_);
    }

    return tesSUCCESS;
}

TER
DepositPreauth::removeFromLedger(
    ApplyView& view,
    uint256 const& preauthIndex,
    beast::Journal j)
{
    // Existence already checked in preclaim and DeleteAccount
    auto const slePreauth{view.peek(keylet::depositPreauth(preauthIndex))};
    if (!slePreauth)
    {
        JLOG(j.warn()) << "Selected DepositPreauth does not exist.";
        return tecNO_ENTRY;
    }

    AccountID const account{(*slePreauth)[sfAccount]};
    std::uint64_t const page{(*slePreauth)[sfOwnerNode]};
    if (!view.dirRemove(keylet::ownerDir(account), page, preauthIndex, false))
    {
        JLOG(j.fatal()) << "Unable to delete DepositPreauth from owner.";
        return tefBAD_LEDGER;
    }

    // If we succeeded, update the DepositPreauth owner's reserve.
    auto const sleOwner = view.peek(keylet::account(account));
    if (!sleOwner)
        return tefINTERNAL;

    adjustOwnerCount(view, sleOwner, -1, j);

    // Remove DepositPreauth from ledger.
    view.erase(slePreauth);

    return tesSUCCESS;
}

}  // namespace ripple
