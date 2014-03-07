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

namespace ripple {

SETUP_LOG (AccountSetTransactor)

TER AccountSetTransactor::doApply ()
{
    WriteLog (lsINFO, AccountSetTransactor) << "AccountSet>";

    const beast::uint32 uTxFlags    = mTxn.getFlags ();

    const beast::uint32 uFlagsIn    = mTxnAccount->getFieldU32 (sfFlags);
    beast::uint32       uFlagsOut   = uFlagsIn;

    const beast::uint32 uSetFlag    = mTxn.getFieldU32 (sfSetFlag);
    const beast::uint32 uClearFlag  = mTxn.getFieldU32 (sfClearFlag);

    // legacy AccountSet flags
    bool      bSetRequireDest   = (uTxFlags & TxFlag::requireDestTag) || (uSetFlag == asfRequireDest);
    bool      bClearRequireDest = (uTxFlags & tfOptionalDestTag) || (uClearFlag == asfRequireDest);
    bool      bSetRequireAuth   = (uTxFlags & tfRequireAuth) || (uSetFlag == asfRequireAuth);
    bool      bClearRequireAuth = (uTxFlags & tfOptionalAuth) || (uClearFlag == asfRequireAuth);
    bool      bSetDisallowXRP   = (uTxFlags & tfDisallowXRP) || (uSetFlag == asfDisallowXRP);
    bool      bClearDisallowXRP = (uTxFlags & tfAllowXRP) || (uClearFlag == asfDisallowXRP);

    if (uTxFlags & tfAccountSetMask)
    {
        WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Malformed transaction: Invalid flags set.";

        return temINVALID_FLAG;
    }

    //
    // RequireAuth
    //

    if (bSetRequireAuth && bClearRequireAuth)
    {
        WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Malformed transaction: Contradictory flags set.";

        return temINVALID_FLAG;
    }

    if (bSetRequireAuth && !isSetBit (uFlagsIn, lsfRequireAuth))
    {
        if (!mEngine->getNodes ().dirIsEmpty (Ledger::getOwnerDirIndex (mTxnAccountID)))
        {
            WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Retry: Owner directory not empty.";

            return isSetBit(mParams, tapRETRY) ? terOWNERS : tecOWNERS;
        }

        WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Set RequireAuth.";

        uFlagsOut   |= lsfRequireAuth;
    }

    if (bClearRequireAuth && isSetBit (uFlagsIn, lsfRequireAuth))
    {
        WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Clear RequireAuth.";

        uFlagsOut   &= ~lsfRequireAuth;
    }

    //
    // RequireDestTag
    //

    if (bSetRequireDest && bClearRequireDest)
    {
        WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Malformed transaction: Contradictory flags set.";

        return temINVALID_FLAG;
    }

    if (bSetRequireDest && !isSetBit (uFlagsIn, lsfRequireDestTag))
    {
        WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Set lsfRequireDestTag.";

        uFlagsOut   |= lsfRequireDestTag;
    }

    if (bClearRequireDest && isSetBit (uFlagsIn, lsfRequireDestTag))
    {
        WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Clear lsfRequireDestTag.";

        uFlagsOut   &= ~lsfRequireDestTag;
    }

    //
    // DisallowXRP
    //

    if (bSetDisallowXRP && bClearDisallowXRP)
    {
        WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Malformed transaction: Contradictory flags set.";

        return temINVALID_FLAG;
    }

    if (bSetDisallowXRP && !isSetBit (uFlagsIn, lsfDisallowXRP))
    {
        WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Set lsfDisallowXRP.";

        uFlagsOut   |= lsfDisallowXRP;
    }

    if (bClearDisallowXRP && isSetBit (uFlagsIn, lsfDisallowXRP))
    {
        WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Clear lsfDisallowXRP.";

        uFlagsOut   &= ~lsfDisallowXRP;
    }

    //
    // DisableMaster
    //

    if ((uSetFlag == asfDisableMaster) && (uClearFlag == asfDisableMaster))
    {
        WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Malformed transaction: Contradictory flags set.";

        return temINVALID_FLAG;
    }

    if ((uSetFlag == asfDisableMaster) && !isSetBit (uFlagsIn, lsfDisableMaster))
    {
        if (!mTxnAccount->isFieldPresent (sfRegularKey))
            return tecNO_REGULAR_KEY;

        WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Set lsfDisableMaster.";

        uFlagsOut   |= lsfDisableMaster;
    }

    if ((uClearFlag == asfDisableMaster) && isSetBit (uFlagsIn, lsfDisableMaster))
    {
        WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Clear lsfDisableMaster.";

        uFlagsOut   &= ~lsfDisableMaster;
    }

    //
    // Track transaction IDs signed by this account in its root
    //

    if ((uSetFlag == asfAccountTxnID) && (uClearFlag != asfAccountTxnID) && !mTxnAccount->isFieldPresent (sfAccountTxnID))
    {
        WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Set AccountTxnID";

        mTxnAccount->makeFieldPresent (sfAccountTxnID);
     }

    if ((uClearFlag == asfAccountTxnID) && (uSetFlag != asfAccountTxnID) && mTxnAccount->isFieldPresent (sfAccountTxnID))
    {
        WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: Clear AccountTxnID";

        mTxnAccount->makeFieldAbsent (sfAccountTxnID);
    }

    //
    // EmailHash
    //

    if (mTxn.isFieldPresent (sfEmailHash))
    {
        uint128     uHash   = mTxn.getFieldH128 (sfEmailHash);

        if (!uHash)
        {
            WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: unset email hash";

            mTxnAccount->makeFieldAbsent (sfEmailHash);
        }
        else
        {
            WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: set email hash";

            mTxnAccount->setFieldH128 (sfEmailHash, uHash);
        }
    }

    //
    // WalletLocator
    //

    if (mTxn.isFieldPresent (sfWalletLocator))
    {
        uint256     uHash   = mTxn.getFieldH256 (sfWalletLocator);

        if (!uHash)
        {
            WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: unset wallet locator";

            mTxnAccount->makeFieldAbsent (sfEmailHash);
        }
        else
        {
            WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: set wallet locator";

            mTxnAccount->setFieldH256 (sfWalletLocator, uHash);
        }
    }

    //
    // MessageKey
    //

    if (mTxn.isFieldPresent (sfMessageKey))
    {
        Blob    vucPublic   = mTxn.getFieldVL (sfMessageKey);

        if (vucPublic.empty ())
        {
            WriteLog (lsDEBUG, AccountSetTransactor) << "AccountSet: set message key";

            mTxnAccount->makeFieldAbsent (sfMessageKey);
        }
        if (vucPublic.size () > PUBLIC_BYTES_MAX)
        {
            WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: message key too long";

            return telBAD_PUBLIC_KEY;
        }
        else
        {
            WriteLog (lsDEBUG, AccountSetTransactor) << "AccountSet: set message key";

            mTxnAccount->setFieldVL (sfMessageKey, vucPublic);
        }
    }

    //
    // Domain
    //

    if (mTxn.isFieldPresent (sfDomain))
    {
        Blob    vucDomain   = mTxn.getFieldVL (sfDomain);

        if (vucDomain.empty ())
        {
            WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: unset domain";

            mTxnAccount->makeFieldAbsent (sfDomain);
        }
        else if (vucDomain.size () > DOMAIN_BYTES_MAX)
        {
            WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: domain too long";

            return telBAD_DOMAIN;
        }
        else
        {
            WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: set domain";

            mTxnAccount->setFieldVL (sfDomain, vucDomain);
        }
    }

    //
    // TransferRate
    //

    if (mTxn.isFieldPresent (sfTransferRate))
    {
        beast::uint32 uRate   = mTxn.getFieldU32 (sfTransferRate);

        if (!uRate || uRate == QUALITY_ONE)
        {
            WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: unset transfer rate";

            mTxnAccount->makeFieldAbsent (sfTransferRate);
        }
        else if (uRate > QUALITY_ONE)
        {
            WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: set transfer rate";

            mTxnAccount->setFieldU32 (sfTransferRate, uRate);
        }
        else
        {
            WriteLog (lsINFO, AccountSetTransactor) << "AccountSet: bad transfer rate";

            return temBAD_TRANSFER_RATE;
        }
    }

    if (uFlagsIn != uFlagsOut)
        mTxnAccount->setFieldU32 (sfFlags, uFlagsOut);

    WriteLog (lsINFO, AccountSetTransactor) << "AccountSet<";

    return tesSUCCESS;
}

}
