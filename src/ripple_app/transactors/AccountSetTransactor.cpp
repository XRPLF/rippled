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

TER AccountSetTransactor::doApply ()
{
    std::uint32_t const uTxFlags = mTxn.getFlags ();

    std::uint32_t const uFlagsIn = mTxnAccount->getFieldU32 (sfFlags);
    std::uint32_t uFlagsOut = uFlagsIn;

    std::uint32_t const uSetFlag = mTxn.getFieldU32 (sfSetFlag);
    std::uint32_t const uClearFlag  = mTxn.getFieldU32 (sfClearFlag);

    // legacy AccountSet flags
    bool bSetRequireDest   = (uTxFlags & TxFlag::requireDestTag) || (uSetFlag == asfRequireDest);
    bool bClearRequireDest = (uTxFlags & tfOptionalDestTag) || (uClearFlag == asfRequireDest);
    bool bSetRequireAuth   = (uTxFlags & tfRequireAuth) || (uSetFlag == asfRequireAuth);
    bool bClearRequireAuth = (uTxFlags & tfOptionalAuth) || (uClearFlag == asfRequireAuth);
    bool bSetDisallowXRP   = (uTxFlags & tfDisallowXRP) || (uSetFlag == asfDisallowXRP);
    bool bClearDisallowXRP = (uTxFlags & tfAllowXRP) || (uClearFlag == asfDisallowXRP);

    if (uTxFlags & tfAccountSetMask)
    {
        m_journal.info << "Malformed transaction: Invalid flags set.";
        return temINVALID_FLAG;
    }

    //
    // RequireAuth
    //

    if (bSetRequireAuth && bClearRequireAuth)
    {
        m_journal.info << "Malformed transaction: Contradictory flags set.";
        return temINVALID_FLAG;
    }

    if (bSetRequireAuth && !isSetBit (uFlagsIn, lsfRequireAuth))
    {
        if (!mEngine->getNodes ().dirIsEmpty (Ledger::getOwnerDirIndex (mTxnAccountID)))
        {
            m_journal.info << "Retry: Owner directory not empty.";

            return isSetBit(mParams, tapRETRY) ? terOWNERS : tecOWNERS;
        }

        m_journal.info << "Set RequireAuth.";
        uFlagsOut   |= lsfRequireAuth;
    }

    if (bClearRequireAuth && isSetBit (uFlagsIn, lsfRequireAuth))
    {
        m_journal.info << "Clear RequireAuth.";
        uFlagsOut   &= ~lsfRequireAuth;
    }

    //
    // RequireDestTag
    //

    if (bSetRequireDest && bClearRequireDest)
    {
        m_journal.info << "Malformed transaction: Contradictory flags set.";
        return temINVALID_FLAG;
    }

    if (bSetRequireDest && !isSetBit (uFlagsIn, lsfRequireDestTag))
    {
        m_journal.info << "Set lsfRequireDestTag.";
        uFlagsOut   |= lsfRequireDestTag;
    }

    if (bClearRequireDest && isSetBit (uFlagsIn, lsfRequireDestTag))
    {
        m_journal.info << "Clear lsfRequireDestTag.";
        uFlagsOut   &= ~lsfRequireDestTag;
    }

    //
    // DisallowXRP
    //

    if (bSetDisallowXRP && bClearDisallowXRP)
    {
        m_journal.info << "Malformed transaction: Contradictory flags set.";
        return temINVALID_FLAG;
    }

    if (bSetDisallowXRP && !isSetBit (uFlagsIn, lsfDisallowXRP))
    {
        m_journal.info << "Set lsfDisallowXRP.";
        uFlagsOut   |= lsfDisallowXRP;
    }

    if (bClearDisallowXRP && isSetBit (uFlagsIn, lsfDisallowXRP))
    {
        m_journal.info << "Clear lsfDisallowXRP.";
        uFlagsOut   &= ~lsfDisallowXRP;
    }

    //
    // DisableMaster
    //

    if ((uSetFlag == asfDisableMaster) && (uClearFlag == asfDisableMaster))
    {
        m_journal.info << "Malformed transaction: Contradictory flags set.";
        return temINVALID_FLAG;
    }

    if ((uSetFlag == asfDisableMaster) && !isSetBit (uFlagsIn, lsfDisableMaster))
    {
        if (!mTxnAccount->isFieldPresent (sfRegularKey))
            return tecNO_REGULAR_KEY;

        m_journal.info << "Set lsfDisableMaster.";
        uFlagsOut   |= lsfDisableMaster;
    }

    if ((uClearFlag == asfDisableMaster) && isSetBit (uFlagsIn, lsfDisableMaster))
    {
        m_journal.info << "Clear lsfDisableMaster.";
        uFlagsOut   &= ~lsfDisableMaster;
    }

    //
    // Track transaction IDs signed by this account in its root
    //

    if ((uSetFlag == asfAccountTxnID) && (uClearFlag != asfAccountTxnID) && !mTxnAccount->isFieldPresent (sfAccountTxnID))
    {
        m_journal.info << "Set AccountTxnID";
        mTxnAccount->makeFieldPresent (sfAccountTxnID);
     }

    if ((uClearFlag == asfAccountTxnID) && (uSetFlag != asfAccountTxnID) && mTxnAccount->isFieldPresent (sfAccountTxnID))
    {
        m_journal.info << "Clear AccountTxnID";
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
            m_journal.info << "unset email hash";
            mTxnAccount->makeFieldAbsent (sfEmailHash);
        }
        else
        {
            m_journal.info << "set email hash";
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
            m_journal.info << "unset wallet locator";
            mTxnAccount->makeFieldAbsent (sfEmailHash);
        }
        else
        {
            m_journal.info << "set wallet locator";
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
            m_journal.debug << "set message key";

            mTxnAccount->makeFieldAbsent (sfMessageKey);
        }
        if (vucPublic.size () > PUBLIC_BYTES_MAX)
        {
            m_journal.info << "message key too long";

            return telBAD_PUBLIC_KEY;
        }
        else
        {
            m_journal.debug << "set message key";

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
            m_journal.info << "unset domain";

            mTxnAccount->makeFieldAbsent (sfDomain);
        }
        else if (vucDomain.size () > DOMAIN_BYTES_MAX)
        {
            m_journal.info << "domain too long";

            return telBAD_DOMAIN;
        }
        else
        {
            m_journal.info << "set domain";

            mTxnAccount->setFieldVL (sfDomain, vucDomain);
        }
    }

    //
    // TransferRate
    //

    if (mTxn.isFieldPresent (sfTransferRate))
    {
        std::uint32_t      uRate   = mTxn.getFieldU32 (sfTransferRate);

        if (!uRate || uRate == QUALITY_ONE)
        {
            m_journal.info << "unset transfer rate";
            mTxnAccount->makeFieldAbsent (sfTransferRate);
        }
        else if (uRate > QUALITY_ONE)
        {
            m_journal.info << "set transfer rate";
            mTxnAccount->setFieldU32 (sfTransferRate, uRate);
        }
        else
        {
            m_journal.info << "bad transfer rate";
            return temBAD_TRANSFER_RATE;
        }
    }

    if (uFlagsIn != uFlagsOut)
        mTxnAccount->setFieldU32 (sfFlags, uFlagsOut);

    return tesSUCCESS;
}

}
