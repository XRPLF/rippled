//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/app/tx/impl/SetAccount.h>
#include <ripple/basics/Log.h>
#include <ripple/core/Config.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/st.h>

namespace ripple {

TxConsequences
SetAccount::makeTxConsequences(PreflightContext const& ctx)
{
    // The SetAccount may be a blocker, but only if it sets or clears
    // specific account flags.
    auto getTxConsequencesCategory = [](STTx const& tx) {
        if (auto const uSetFlag = tx[~sfSetFlag]; uSetFlag &&
            (*uSetFlag == asfRequireAuth || *uSetFlag == asfDisableMaster ||
             *uSetFlag == asfAccountTxnID))
            return TxConsequences::blocker;

        if (auto const uClearFlag = tx[~sfClearFlag]; uClearFlag &&
            (*uClearFlag == asfRequireAuth || *uClearFlag == asfDisableMaster ||
             *uClearFlag == asfAccountTxnID))
            return TxConsequences::blocker;

        return TxConsequences::normal;
    };

    return TxConsequences{ctx.tx, getTxConsequencesCategory(ctx.tx)};
}

NotTEC
SetAccount::preflight(PreflightContext const& ctx)
{
    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto& tx = ctx.tx;
    auto& j = ctx.j;

    std::uint32_t const uTxFlags = tx.getFlags();

    if (uTxFlags & tfAccountSetMask)
    {
        JLOG(j.trace()) << "Malformed transaction: Invalid flags set.";
        return temINVALID_FLAG;
    }

    std::uint32_t const uSetFlag = tx.getFieldU32(sfSetFlag);
    std::uint32_t const uClearFlag = tx.getFieldU32(sfClearFlag);

    if ((uSetFlag != 0) && (uSetFlag == uClearFlag))
    {
        JLOG(j.trace()) << "Malformed transaction: Set and clear same flag.";
        return temINVALID_FLAG;
    }

    //
    // RequireDestTag
    //
    bool bSetRequireDest =
        (uTxFlags & tfRequireDestTag) || (uSetFlag == asfRequireDest);
    bool bClearRequireDest =
        (uTxFlags & tfOptionalDestTag) || (uClearFlag == asfRequireDest);

    if (bSetRequireDest && bClearRequireDest)
    {
        JLOG(j.trace()) << "Malformed transaction: Contradictory flags set.";
        return temINVALID_FLAG;
    }

    //
    // DisallowXRP
    //
    bool bSetDisallowXRP =
        (uTxFlags & tfDisallowXRP) || (uSetFlag == asfDisallowXRP);
    bool bClearDisallowXRP =
        (uTxFlags & tfAllowXRP) || (uClearFlag == asfDisallowXRP);

    if (bSetDisallowXRP && bClearDisallowXRP)
    {
        JLOG(j.trace()) << "Malformed transaction: Contradictory flags set.";
        return temINVALID_FLAG;
    }

//    // TransferRate
//    if (tx.isFieldPresent(sfTransferRate))
//    {
//        std::uint32_t uRate = tx.getFieldU32(sfTransferRate);
//
//        if (uRate && (uRate < QUALITY_ONE))
//        {
//            JLOG(j.trace())
//                << "Malformed transaction: Transfer rate too small.";
//            return temBAD_TRANSFER_RATE;
//        }
//
//        if (uRate > 2 * QUALITY_ONE)
//        {
//            JLOG(j.trace())
//                << "Malformed transaction: Transfer rate too large.";
//            return temBAD_TRANSFER_RATE;
//        }
//    }

    if (auto const mk = tx[~sfMessageKey])
    {
        if (mk->size() && !publicKeyType({mk->data(), mk->size()}))
        {
            JLOG(j.trace()) << "Invalid message key specified.";
            return telBAD_PUBLIC_KEY;
        }
    }

    if (auto const domain = tx[~sfDomain];
        domain && domain->size() > maxDomainLength)
    {
        JLOG(j.trace()) << "domain too long";
        return telBAD_DOMAIN;
    }

    return preflight2(ctx);
}

TER
SetAccount::preclaim(PreclaimContext const& ctx)
{
    auto const id = ctx.tx[sfAccount];

    std::uint32_t const uTxFlags = ctx.tx.getFlags();

    auto const sle = ctx.view.read(keylet::account(id));
    if (!sle)
        return terNO_ACCOUNT;

    std::uint32_t const uFlagsIn = sle->getFieldU32(sfFlags);

    std::uint32_t const uSetFlag = ctx.tx.getFieldU32(sfSetFlag);

    // legacy AccountSet flags
//    bool bSetRequireAuth =
//        (uTxFlags & tfRequireAuth) || (uSetFlag == asfRequireAuth);

    //
    // RequireAuth
    //
//    if (bSetRequireAuth
////        && !(uFlagsIn & lsfRequireAuth)
//        )
//    {
//        if (!dirIsEmpty(ctx.view, keylet::ownerDir(id)))
//        {
//            JLOG(ctx.j.trace()) << "Retry: Owner directory not empty.";
//            return (ctx.flags & tapRETRY) ? TER{terOWNERS} : TER{tecOWNERS};
//        }
//    }

    return tesSUCCESS;
}

TER
SetAccount::doApply(ApplyContext& ctx, XRPAmount mPriorBalance, XRPAmount mSourceBalance)
{
    AccountID const account_ = ctx.tx.getAccountID(sfAccount);
    auto const sle = ctx.view().peek(keylet::account(ctx.tx.getAccountID(sfAccount)));
    if (!sle)
        return tefINTERNAL;

    std::uint32_t const uFlagsIn = sle->getFieldU32(sfFlags);
    std::uint32_t uFlagsOut = uFlagsIn;

    STTx const& tx{ctx.tx};
    std::uint32_t const uSetFlag{tx.getFieldU32(sfSetFlag)};
    std::uint32_t const uClearFlag{tx.getFieldU32(sfClearFlag)};

    // legacy AccountSet flags
    std::uint32_t const uTxFlags{tx.getFlags()};
    bool const bSetRequireDest{
        (uTxFlags & tfRequireDestTag) || (uSetFlag == asfRequireDest)};
    bool const bClearRequireDest{
        (uTxFlags & tfOptionalDestTag) || (uClearFlag == asfRequireDest)};
    bool const bClearRequireAuth{
        (uTxFlags & tfOptionalAuth) || (uClearFlag == asfRequireAuth)};
    bool const bSetDisallowXRP{
        (uTxFlags & tfDisallowXRP) || (uSetFlag == asfDisallowXRP)};
    bool const bClearDisallowXRP{
        (uTxFlags & tfAllowXRP) || (uClearFlag == asfDisallowXRP)};

    bool const sigWithMaster{[&tx, &acct = account_]() {
        auto const spk = tx.getSigningPubKey();

        if (publicKeyType(makeSlice(spk)))
        {
            PublicKey const signingPubKey(makeSlice(spk));

            if (calcAccountID(signingPubKey) == acct)
                return true;
        }
        return false;
    }()};

    //
    // RequireDestTag
    //
    if (bSetRequireDest && !(uFlagsIn & lsfRequireDestTag))
    {
        JLOG(ctx.journal.trace()) << "Set lsfRequireDestTag.";
        uFlagsOut |= lsfRequireDestTag;
    }

    if (bClearRequireDest && (uFlagsIn & lsfRequireDestTag))
    {
        JLOG(ctx.journal.trace()) << "Clear lsfRequireDestTag.";
        uFlagsOut &= ~lsfRequireDestTag;
    }

    //
    // DisallowXRP
    //
    if (bSetDisallowXRP && !(uFlagsIn & lsfDisallowXRP))
    {
        JLOG(ctx.journal.trace()) << "Set lsfDisallowXRP.";
        uFlagsOut |= lsfDisallowXRP;
    }

    if (bClearDisallowXRP && (uFlagsIn & lsfDisallowXRP))
    {
        JLOG(ctx.journal.trace()) << "Clear lsfDisallowXRP.";
        uFlagsOut &= ~lsfDisallowXRP;
    }

    //
    // DisableMaster
    //
    if ((uSetFlag == asfDisableMaster) && !(uFlagsIn & lsfDisableMaster))
    {
        if (!sigWithMaster)
        {
            JLOG(ctx.journal.trace()) << "Must use master key to disable master key.";
            return tecNEED_MASTER_KEY;
        }

        if ((!sle->isFieldPresent(sfRegularKey)) &&
            (!ctx.view().peek(keylet::signers(ctx.tx.getAccountID(sfAccount)))))
        {
            // Account has no regular key or multi-signer signer list.
            return tecNO_ALTERNATIVE_KEY;
        }

        JLOG(ctx.journal.trace()) << "Set lsfDisableMaster.";
        uFlagsOut |= lsfDisableMaster;
    }

    if ((uClearFlag == asfDisableMaster) && (uFlagsIn & lsfDisableMaster))
    {
        JLOG(ctx.journal.trace()) << "Clear lsfDisableMaster.";
        uFlagsOut &= ~lsfDisableMaster;
    }

    //
    // Track transaction IDs signed by this account in its root
    //
    if ((uSetFlag == asfAccountTxnID) && !sle->isFieldPresent(sfAccountTxnID))
    {
        JLOG(ctx.journal.trace()) << "Set AccountTxnID.";
        sle->makeFieldPresent(sfAccountTxnID);
    }

    if ((uClearFlag == asfAccountTxnID) && sle->isFieldPresent(sfAccountTxnID))
    {
        JLOG(ctx.journal.trace()) << "Clear AccountTxnID.";
        sle->makeFieldAbsent(sfAccountTxnID);
    }

    //
    // EmailHash
    //
    if (tx.isFieldPresent(sfEmailHash))
    {
        uint128 const uHash = tx.getFieldH128(sfEmailHash);

        if (!uHash)
        {
            JLOG(ctx.journal.trace()) << "unset email hash";
            sle->makeFieldAbsent(sfEmailHash);
        }
        else
        {
            JLOG(ctx.journal.trace()) << "set email hash";
            sle->setFieldH128(sfEmailHash, uHash);
        }
    }

    //
    // WalletLocator
    //
    if (tx.isFieldPresent(sfWalletLocator))
    {
        uint256 const uHash = tx.getFieldH256(sfWalletLocator);

        if (!uHash)
        {
            JLOG(ctx.journal.trace()) << "unset wallet locator";
            sle->makeFieldAbsent(sfWalletLocator);
        }
        else
        {
            JLOG(ctx.journal.trace()) << "set wallet locator";
            sle->setFieldH256(sfWalletLocator, uHash);
        }
    }

    //
    // MessageKey
    //
    if (tx.isFieldPresent(sfMessageKey))
    {
        Blob const messageKey = tx.getFieldVL(sfMessageKey);

        if (messageKey.empty())
        {
            JLOG(ctx.journal.debug()) << "set message key";
            sle->makeFieldAbsent(sfMessageKey);
        }
        else
        {
            JLOG(ctx.journal.debug()) << "set message key";
            sle->setFieldVL(sfMessageKey, messageKey);
        }
    }

    //
    // Domain
    //
    if (tx.isFieldPresent(sfDomain))
    {
        Blob const domain = tx.getFieldVL(sfDomain);

        if (domain.empty())
        {
            JLOG(ctx.journal.trace()) << "unset domain";
            sle->makeFieldAbsent(sfDomain);
        }
        else
        {
            JLOG(ctx.journal.trace()) << "set domain";
            sle->setFieldVL(sfDomain, domain);
        }
    }

    if (uFlagsIn != uFlagsOut)
        sle->setFieldU32(sfFlags, uFlagsOut);

    return tesSUCCESS;
}

}  // namespace ripple
