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

#include <xrpld/app/tx/detail/Credentials.h>
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

struct HashUint256
{
    std::size_t
    operator()(uint256 const& x) const noexcept
    {
        std::size_t u = 0;
        std::size_t const* const b =
            reinterpret_cast<std::size_t const*>(x.data());
        std::size_t const* const e = b + 256 / 8 / sizeof(std::size_t);

        for (std::size_t const* c = b; c < e; ++c)
            boost::hash_combine(u, *c);

        return u;
    }
};

bool
DepositPreauth::credentialIDsRemoveExpired(
    ApplyView& view,
    STTx const& tx,
    beast::Journal const j)
{
    auto const closeTime = view.info().parentCloseTime;
    bool foundExpired = false;

    STVector256 const& arr(tx.getFieldV256(sfCredentialIDs));
    for (auto const& h : arr)
    {
        // Credentials already checked in preclaim. Look only for expired here.
        auto const k = keylet::credential(h);
        auto const sleCred = view.peek(k);

        if (credentialCheckExpired(sleCred, closeTime))
        {
            JLOG(j.trace())
                << "Credentials are expired. Cred: " << sleCred->getText();
            // delete expired credentials even if the transaction failed
            CredentialDelete::deleteSLE(view, sleCred, j);
            foundExpired = true;
        }
    }

    return foundExpired;
}

NotTEC
DepositPreauth::preauthPreflightCredentialsCheck(
    STVector256 const& credentials,
    beast::Journal const j)
{
    if (credentials.empty() || (credentials.size() > credentialsArrayMaxSize))
    {
        JLOG(j.trace())
            << "Malformed transaction: Credentials array size is invalid: "
            << credentials.size();
        return temMALFORMED;
    }

    return tesSUCCESS;
}

TER
DepositPreauth::preauthPreclaimCredentialsCheck(
    ReadView const& view,
    AccountID const& src,
    AccountID const& dst,
    STVector256 const& credentials,
    beast::Journal const j)
{
    STArray authCreds;

    for (auto const& h : credentials)
    {
        auto const sleCred = view.read(keylet::credential(h));
        if (!sleCred)
        {
            JLOG(j.trace()) << "Credential doesn't exists. Cred: " << h;
            return temBAD_CREDENTIALS;
        }

        if (sleCred->getAccountID(sfSubject) != src)
        {
            JLOG(j.trace())
                << "Credential not for current account. Cred: " << h;
            return temBAD_CREDENTIALS;
        }

        if (!(sleCred->getFlags() & lsfAccepted))
        {
            JLOG(j.trace()) << "Credential not accepted. Cred: " << h;
            return temBAD_CREDENTIALS;
        }

        auto o = STObject::makeInnerObject(sfCredential);
        o.setAccountID(sfIssuer, sleCred->getAccountID(sfIssuer));
        o.setFieldVL(sfCredentialType, sleCred->getFieldVL(sfCredentialType));
        authCreds.push_back(std::move(o));
    }

    if (!view.exists(keylet::depositPreauth(dst, authCreds)))
    {
        JLOG(j.trace()) << "DepositPreauth doesn't exists";
        return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

NotTEC
DepositPreauth::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureDepositPreauth))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto const& tx = ctx.tx;
    auto& j = ctx.j;

    if (tx.getFlags() & tfUniversalMask)
    {
        JLOG(j.trace()) << "Malformed transaction: Invalid flags set.";
        return temINVALID_FLAG;
    }

    auto const optAuth = tx[~sfAuthorize];
    auto const optUnauth = tx[~sfUnauthorize];
    int const authPresent = static_cast<int>(optAuth.has_value()) +
        static_cast<int>(optUnauth.has_value());

    bool const authArrPresent = tx.isFieldPresent(sfAuthorizeCredentials);
    bool const unauthArrPresent = tx.isFieldPresent(sfUnauthorizeCredentials);
    int const authCredPresent =
        static_cast<int>(authArrPresent) + static_cast<int>(unauthArrPresent);

    if (authPresent + authCredPresent != 1)
    {
        // Either both fields are present or neither field is present.  In
        // either case the transaction is malformed.
        JLOG(j.trace())
            << "Malformed transaction: "
               "Invalid Authorize and Unauthorize field combination.";
        return temMALFORMED;
    }

    // Make sure that the passed account is valid.
    AccountID const account(tx[sfAccount]);
    if (authPresent)
    {
        AccountID const& target(optAuth ? *optAuth : *optUnauth);
        if (!target)
        {
            JLOG(j.trace())
                << "Malformed transaction: Authorized or Unauthorized "
                   "field zeroed.";
            return temINVALID_ACCOUNT_ID;
        }

        // An account may not preauthorize itself.
        if (target == account)
        {
            JLOG(j.trace())
                << "Malformed transaction: Attempting to DepositPreauth self.";
            return temCANNOT_PREAUTH_SELF;
        }
    }
    else
    {
        if (!ctx.rules.enabled(featureCredentials))
        {
            JLOG(j.trace()) << "Credentials rule is disabled.";
            return temDISABLED;
        }

        STArray const& arr(tx.getFieldArray(
            authArrPresent ? sfAuthorizeCredentials
                           : sfUnauthorizeCredentials));
        if (arr.empty() || (arr.size() > credentialsArrayMaxSize))
        {
            JLOG(j.trace()) << "Malformed transaction: "
                               "Invalid AuthorizeCredentials size: "
                            << arr.size();
            return temMALFORMED;
        }

        for (auto const& o : arr)
        {
            AccountID const target(o[sfIssuer]);
            if (!target)
            {
                JLOG(j.trace())
                    << "Malformed transaction: "
                       "AuthorizeCredentials Issuer account is invalid.";
                return temINVALID_ACCOUNT_ID;
            }

            // An account may not preauthorize itself.
            if (target == account)
            {
                JLOG(j.trace()) << "Malformed transaction: Attempting to "
                                   "DepositPreauth self.";
                return temCANNOT_PREAUTH_SELF;
            }

            auto const ct = o[sfCredentialType];
            if (ct.empty())
            {
                JLOG(j.trace())
                    << "Malformed transaction: empty credential type.";
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
        AccountID const unauth{ctx.tx[sfUnauthorize]};
        if (!ctx.view.exists(keylet::depositPreauth(account, unauth)))
            return tecNO_ENTRY;
    }
    else if (ctx.tx.isFieldPresent(sfAuthorizeCredentials))
    {
        STArray const& authCred(ctx.tx.getFieldArray(sfAuthorizeCredentials));
        // Verify that the Preauth entry they asked to add is not already
        // in the ledger.
        if (ctx.view.exists(keylet::depositPreauth(account, authCred)))
            return tecDUPLICATE;
    }
    else if (ctx.tx.isFieldPresent(sfUnauthorizeCredentials))
    {
        auto const& unauthCred = ctx.tx.getFieldArray(sfUnauthorizeCredentials);

        // Verify that the Preauth entry is in the ledger.
        if (!ctx.view.exists(keylet::depositPreauth(account, unauthCred)))
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

        STArray const& authCred(ctx_.tx.getFieldArray(sfAuthorizeCredentials));
        Keylet const kPreauth = keylet::depositPreauth(account_, authCred);
        auto slePreauth = std::make_shared<SLE>(kPreauth);
        slePreauth->setAccountID(sfAccount, account_);
        auto* pArr = slePreauth->makeFieldPresent(sfAuthorizeCredentials);
        if (!pArr)
            return tefINTERNAL;
        STArray& arr = *static_cast<STArray*>(pArr);
        arr.reserve(authCred.size());

        // eleminate duplicates
        std::unordered_set<uint256, HashUint256> hashes;
        for (auto const& o : authCred)
        {
            auto [it, ins] = hashes.insert(sha512Half(
                o.getAccountID(sfIssuer), o.getFieldVL(sfCredentialType)));
            if (ins)
            {
                arr.push_back(o);
            }
            else
            {
                JLOG(j_.trace())
                    << "DepositPreauth duplicate removed: " << o.getText();
            }
        }

        view().insert(slePreauth);

        auto const page = view().dirInsert(
            keylet::ownerDir(account_), kPreauth, describeOwnerDir(account_));

        JLOG(j_.trace()) << "Adding DepositPreauth to owner directory "
                         << to_string(kPreauth.key) << ": "
                         << (page ? "success" : "failure");

        if (!page)
            return tecDIR_FULL;

        slePreauth->setFieldU64(sfOwnerNode, *page);

        // If we succeeded, the new entry counts against the creator's reserve.
        adjustOwnerCount(view(), sleOwner, 1, j_);
    }
    else if (ctx_.tx.isFieldPresent(sfUnauthorizeCredentials))
    {
        auto const kPreauth = keylet::depositPreauth(
            account_, ctx_.tx.getFieldArray(sfUnauthorizeCredentials));
        return DepositPreauth::removeFromLedger(view(), kPreauth.key, j_);
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
