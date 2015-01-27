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

#include <BeastConfig.h>
#include <ripple/app/tx/impl/SetAccount.h>
#include <ripple/basics/Log.h>
#include <ripple/core/Config.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

TER
SetAccount::preCheck ()
{
    std::uint32_t const uTxFlags = mTxn->getFlags ();

    if (uTxFlags & tfAccountSetMask)
    {
        j_.trace << "Malformed transaction: Invalid flags set.";
        return temINVALID_FLAG;
    }

    std::uint32_t const uSetFlag = mTxn->getFieldU32 (sfSetFlag);
    std::uint32_t const uClearFlag = mTxn->getFieldU32 (sfClearFlag);

    if ((uSetFlag != 0) && (uSetFlag == uClearFlag))
    {
        j_.trace << "Malformed transaction: Set and clear same flag.";
        return temINVALID_FLAG;
    }

    //
    // RequireAuth
    //
    bool bSetRequireAuth   = (uTxFlags & tfRequireAuth) || (uSetFlag == asfRequireAuth);
    bool bClearRequireAuth = (uTxFlags & tfOptionalAuth) || (uClearFlag == asfRequireAuth);

    if (bSetRequireAuth && bClearRequireAuth)
    {
        j_.trace << "Malformed transaction: Contradictory flags set.";
        return temINVALID_FLAG;
    }

    //
    // RequireDestTag
    //
    bool bSetRequireDest   = (uTxFlags & TxFlag::requireDestTag) || (uSetFlag == asfRequireDest);
    bool bClearRequireDest = (uTxFlags & tfOptionalDestTag) || (uClearFlag == asfRequireDest);

    if (bSetRequireDest && bClearRequireDest)
    {
        j_.trace << "Malformed transaction: Contradictory flags set.";
        return temINVALID_FLAG;
    }

    //
    // DisallowXRP
    //
    bool bSetDisallowXRP   = (uTxFlags & tfDisallowXRP) || (uSetFlag == asfDisallowXRP);
    bool bClearDisallowXRP = (uTxFlags & tfAllowXRP) || (uClearFlag == asfDisallowXRP);

    if (bSetDisallowXRP && bClearDisallowXRP)
    {
        j_.trace << "Malformed transaction: Contradictory flags set.";
        return temINVALID_FLAG;
    }

    // TransferRate
    if (mTxn->isFieldPresent (sfTransferRate))
    {
        std::uint32_t uRate = mTxn->getFieldU32 (sfTransferRate);

        if (uRate && (uRate < QUALITY_ONE))
        {
            j_.trace << "Malformed transaction: Bad transfer rate.";
            return temBAD_TRANSFER_RATE;
        }
    }

    return Transactor::preCheck ();
}

TER
SetAccount::doApply ()
{
    std::uint32_t const uTxFlags = mTxn->getFlags ();

    std::uint32_t const uFlagsIn = mTxnAccount->getFieldU32 (sfFlags);
    std::uint32_t uFlagsOut = uFlagsIn;

    std::uint32_t const uSetFlag = mTxn->getFieldU32 (sfSetFlag);
    std::uint32_t const uClearFlag = mTxn->getFieldU32 (sfClearFlag);

    // legacy AccountSet flags
    bool bSetRequireDest   = (uTxFlags & TxFlag::requireDestTag) || (uSetFlag == asfRequireDest);
    bool bClearRequireDest = (uTxFlags & tfOptionalDestTag) || (uClearFlag == asfRequireDest);
    bool bSetRequireAuth   = (uTxFlags & tfRequireAuth) || (uSetFlag == asfRequireAuth);
    bool bClearRequireAuth = (uTxFlags & tfOptionalAuth) || (uClearFlag == asfRequireAuth);
    bool bSetDisallowXRP   = (uTxFlags & tfDisallowXRP) || (uSetFlag == asfDisallowXRP);
    bool bClearDisallowXRP = (uTxFlags & tfAllowXRP) || (uClearFlag == asfDisallowXRP);

    //
    // RequireAuth
    //
    if (bSetRequireAuth && !(uFlagsIn & lsfRequireAuth))
    {
        if (! dirIsEmpty (view(),
            keylet::ownerDir(mTxnAccountID)))
        {
            j_.trace << "Retry: Owner directory not empty.";
            return (view().flags() & tapRETRY) ? terOWNERS : tecOWNERS;
        }

        j_.trace << "Set RequireAuth.";
        uFlagsOut |= lsfRequireAuth;
    }

    if (bClearRequireAuth && (uFlagsIn & lsfRequireAuth))
    {
        j_.trace << "Clear RequireAuth.";
        uFlagsOut &= ~lsfRequireAuth;
    }

    //
    // RequireDestTag
    //
    if (bSetRequireDest && !(uFlagsIn & lsfRequireDestTag))
    {
        j_.trace << "Set lsfRequireDestTag.";
        uFlagsOut |= lsfRequireDestTag;
    }

    if (bClearRequireDest && (uFlagsIn & lsfRequireDestTag))
    {
        j_.trace << "Clear lsfRequireDestTag.";
        uFlagsOut &= ~lsfRequireDestTag;
    }

    //
    // DisallowXRP
    //
    if (bSetDisallowXRP && !(uFlagsIn & lsfDisallowXRP))
    {
        j_.trace << "Set lsfDisallowXRP.";
        uFlagsOut |= lsfDisallowXRP;
    }

    if (bClearDisallowXRP && (uFlagsIn & lsfDisallowXRP))
    {
        j_.trace << "Clear lsfDisallowXRP.";
        uFlagsOut &= ~lsfDisallowXRP;
    }

    //
    // DisableMaster
    //
    if ((uSetFlag == asfDisableMaster) && !(uFlagsIn & lsfDisableMaster))
    {
        if (!mSigMaster)
        {
            j_.trace << "Must use master key to disable master key.";
            return tecNEED_MASTER_KEY;
        }

        if (!mTxnAccount->isFieldPresent (sfRegularKey))
            return tecNO_REGULAR_KEY;

        j_.trace << "Set lsfDisableMaster.";
        uFlagsOut |= lsfDisableMaster;
    }

    if ((uClearFlag == asfDisableMaster) && (uFlagsIn & lsfDisableMaster))
    {
        j_.trace << "Clear lsfDisableMaster.";
        uFlagsOut &= ~lsfDisableMaster;
    }

    //
    // DefaultRipple
    //
    if (uSetFlag == asfDefaultRipple)
    {
        uFlagsOut   |= lsfDefaultRipple;
    }
    else if (uClearFlag == asfDefaultRipple)
    {
        uFlagsOut   &= ~lsfDefaultRipple;
    }

    //
    // NoFreeze
    //
    if (uSetFlag == asfNoFreeze)
    {
        if (!mSigMaster && !(uFlagsIn & lsfDisableMaster))
        {
            j_.trace << "Can't use regular key to set NoFreeze.";
            return tecNEED_MASTER_KEY;
        }

        j_.trace << "Set NoFreeze flag";
        uFlagsOut |= lsfNoFreeze;
    }

    // Anyone may set global freeze
    if (uSetFlag == asfGlobalFreeze)
    {
        j_.trace << "Set GlobalFreeze flag";
        uFlagsOut |= lsfGlobalFreeze;
    }

    // If you have set NoFreeze, you may not clear GlobalFreeze
    // This prevents those who have set NoFreeze from using
    // GlobalFreeze strategically.
    if ((uSetFlag != asfGlobalFreeze) && (uClearFlag == asfGlobalFreeze) &&
        ((uFlagsOut & lsfNoFreeze) == 0))
    {
        j_.trace << "Clear GlobalFreeze flag";
        uFlagsOut &= ~lsfGlobalFreeze;
    }

    //
    // Track transaction IDs signed by this account in its root
    //
    if ((uSetFlag == asfAccountTxnID) && !mTxnAccount->isFieldPresent (sfAccountTxnID))
    {
        j_.trace << "Set AccountTxnID";
        mTxnAccount->makeFieldPresent (sfAccountTxnID);
        }

    if ((uClearFlag == asfAccountTxnID) && mTxnAccount->isFieldPresent (sfAccountTxnID))
    {
        j_.trace << "Clear AccountTxnID";
        mTxnAccount->makeFieldAbsent (sfAccountTxnID);
    }

    //
    // EmailHash
    //
    if (mTxn->isFieldPresent (sfEmailHash))
    {
        uint128 const uHash = mTxn->getFieldH128 (sfEmailHash);

        if (!uHash)
        {
            j_.trace << "unset email hash";
            mTxnAccount->makeFieldAbsent (sfEmailHash);
        }
        else
        {
            j_.trace << "set email hash";
            mTxnAccount->setFieldH128 (sfEmailHash, uHash);
        }
    }

    //
    // WalletLocator
    //
    if (mTxn->isFieldPresent (sfWalletLocator))
    {
        uint256 const uHash = mTxn->getFieldH256 (sfWalletLocator);

        if (!uHash)
        {
            j_.trace << "unset wallet locator";
            mTxnAccount->makeFieldAbsent (sfWalletLocator);
        }
        else
        {
            j_.trace << "set wallet locator";
            mTxnAccount->setFieldH256 (sfWalletLocator, uHash);
        }
    }

    //
    // MessageKey
    //
    if (mTxn->isFieldPresent (sfMessageKey))
    {
        Blob const messageKey = mTxn->getFieldVL (sfMessageKey);

        if (messageKey.size () > PUBLIC_BYTES_MAX)
        {
            j_.trace << "message key too long";
            return telBAD_PUBLIC_KEY;
        }

        if (messageKey.empty ())
        {
            j_.debug << "set message key";
            mTxnAccount->makeFieldAbsent (sfMessageKey);
        }
        else
        {
            j_.debug << "set message key";
            mTxnAccount->setFieldVL (sfMessageKey, messageKey);
        }
    }

    //
    // Domain
    //
    if (mTxn->isFieldPresent (sfDomain))
    {
        Blob const domain = mTxn->getFieldVL (sfDomain);

        if (domain.size () > DOMAIN_BYTES_MAX)
        {
            j_.trace << "domain too long";
            return telBAD_DOMAIN;
        }

        if (domain.empty ())
        {
            j_.trace << "unset domain";
            mTxnAccount->makeFieldAbsent (sfDomain);
        }
        else
        {
            j_.trace << "set domain";
            mTxnAccount->setFieldVL (sfDomain, domain);
        }
    }

    //
    // TransferRate
    //
    if (mTxn->isFieldPresent (sfTransferRate))
    {
        std::uint32_t uRate = mTxn->getFieldU32 (sfTransferRate);

        if (uRate == 0 || uRate == QUALITY_ONE)
        {
            j_.trace << "unset transfer rate";
            mTxnAccount->makeFieldAbsent (sfTransferRate);
        }
        else if (uRate > QUALITY_ONE)
        {
            j_.trace << "set transfer rate";
            mTxnAccount->setFieldU32 (sfTransferRate, uRate);
        }
    }

    if (uFlagsIn != uFlagsOut)
        mTxnAccount->setFieldU32 (sfFlags, uFlagsOut);

    return tesSUCCESS;
}

}
