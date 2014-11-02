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
#include <ripple/app/book/Quality.h>
#include <ripple/app/transactors/Transactor.h>
#include <ripple/basics/Log.h>
#include <ripple/core/Config.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

class SetAccount
    : public Transactor
{
    static std::size_t const DOMAIN_BYTES_MAX = 256;
    static std::size_t const PUBLIC_BYTES_MAX = 33;

public:
    SetAccount (
        STTx const& txn,
        TransactionEngineParams params,
        TransactionEngine* engine)
        : Transactor (
            txn,
            params,
            engine,
            deprecatedLogs().journal("SetAccount"))
    {

    }

    TER doApply () override
    {
        std::uint32_t const uTxFlags = mTxn.getFlags ();

        std::uint32_t const uFlagsIn = mTxnAccount->getFieldU32 (sfFlags);
        std::uint32_t uFlagsOut = uFlagsIn;

        std::uint32_t const uSetFlag = mTxn.getFieldU32 (sfSetFlag);
        std::uint32_t const uClearFlag = mTxn.getFieldU32 (sfClearFlag);

        if ((uSetFlag != 0) && (uSetFlag == uClearFlag))
        {
            m_journal.trace << "Malformed transaction: Set and clear same flag";
            return temINVALID_FLAG;
        }

        // legacy AccountSet flags
        bool bSetRequireDest   = (uTxFlags & TxFlag::requireDestTag) || (uSetFlag == asfRequireDest);
        bool bClearRequireDest = (uTxFlags & tfOptionalDestTag) || (uClearFlag == asfRequireDest);
        bool bSetRequireAuth   = (uTxFlags & tfRequireAuth) || (uSetFlag == asfRequireAuth);
        bool bClearRequireAuth = (uTxFlags & tfOptionalAuth) || (uClearFlag == asfRequireAuth);
        bool bSetDisallowXRP   = (uTxFlags & tfDisallowXRP) || (uSetFlag == asfDisallowXRP);
        bool bClearDisallowXRP = (uTxFlags & tfAllowXRP) || (uClearFlag == asfDisallowXRP);

        if (uTxFlags & tfAccountSetMask)
        {
            m_journal.trace << "Malformed transaction: Invalid flags set.";
            return temINVALID_FLAG;
        }

        //
        // RequireAuth
        //

        if (bSetRequireAuth && bClearRequireAuth)
        {
            m_journal.trace << "Malformed transaction: Contradictory flags set.";
            return temINVALID_FLAG;
        }

        if (bSetRequireAuth && !(uFlagsIn & lsfRequireAuth))
        {
            if (!mEngine->view().dirIsEmpty (getOwnerDirIndex (mTxnAccountID)))
            {
                m_journal.trace << "Retry: Owner directory not empty.";
                return (mParams & tapRETRY) ? terOWNERS : tecOWNERS;
            }

            m_journal.trace << "Set RequireAuth.";
            uFlagsOut |= lsfRequireAuth;
        }

        if (bClearRequireAuth && (uFlagsIn & lsfRequireAuth))
        {
            m_journal.trace << "Clear RequireAuth.";
            uFlagsOut &= ~lsfRequireAuth;
        }

        //
        // RequireDestTag
        //

        if (bSetRequireDest && bClearRequireDest)
        {
            m_journal.trace << "Malformed transaction: Contradictory flags set.";
            return temINVALID_FLAG;
        }

        if (bSetRequireDest && !(uFlagsIn & lsfRequireDestTag))
        {
            m_journal.trace << "Set lsfRequireDestTag.";
            uFlagsOut |= lsfRequireDestTag;
        }

        if (bClearRequireDest && (uFlagsIn & lsfRequireDestTag))
        {
            m_journal.trace << "Clear lsfRequireDestTag.";
            uFlagsOut &= ~lsfRequireDestTag;
        }

        //
        // DisallowXRP
        //

        if (bSetDisallowXRP && bClearDisallowXRP)
        {
            m_journal.trace << "Malformed transaction: Contradictory flags set.";
            return temINVALID_FLAG;
        }

        if (bSetDisallowXRP && !(uFlagsIn & lsfDisallowXRP))
        {
            m_journal.trace << "Set lsfDisallowXRP.";
            uFlagsOut |= lsfDisallowXRP;
        }

        if (bClearDisallowXRP && (uFlagsIn & lsfDisallowXRP))
        {
            m_journal.trace << "Clear lsfDisallowXRP.";
            uFlagsOut &= ~lsfDisallowXRP;
        }

        //
        // DisableMaster
        //

        if ((uSetFlag == asfDisableMaster) && !(uFlagsIn & lsfDisableMaster))
        {
            if (!mSigMaster)
            {
                m_journal.trace << "Can't use regular key to disable master key.";
                return tecNEED_MASTER_KEY;
            }

            if (!mTxnAccount->isFieldPresent (sfRegularKey))
                return tecNO_REGULAR_KEY;

            m_journal.trace << "Set lsfDisableMaster.";
            uFlagsOut |= lsfDisableMaster;
        }

        if ((uClearFlag == asfDisableMaster) && (uFlagsIn & lsfDisableMaster))
        {
            m_journal.trace << "Clear lsfDisableMaster.";
            uFlagsOut &= ~lsfDisableMaster;
        }

        if (uSetFlag == asfNoFreeze)
        {
            if (!mSigMaster && !(uFlagsIn & lsfDisableMaster))
            {
                m_journal.trace << "Can't use regular key to set NoFreeze.";
                return tecNEED_MASTER_KEY;
            }

            m_journal.trace << "Set NoFreeze flag";
            uFlagsOut |= lsfNoFreeze;
        }

        // Anyone may set global freeze
        if (uSetFlag == asfGlobalFreeze)
        {
            m_journal.trace << "Set GlobalFreeze flag";
            uFlagsOut |= lsfGlobalFreeze;
        }

        // If you have set NoFreeze, you may not clear GlobalFreeze
        // This prevents those who have set NoFreeze from using
        // GlobalFreeze strategically.
        if ((uSetFlag != asfGlobalFreeze) && (uClearFlag == asfGlobalFreeze) &&
            ((uFlagsOut & lsfNoFreeze) == 0))
        {
            m_journal.trace << "Clear GlobalFreeze flag";
            uFlagsOut &= ~lsfGlobalFreeze;
        }

        //
        // Track transaction IDs signed by this account in its root
        //

        if ((uSetFlag == asfAccountTxnID) && !mTxnAccount->isFieldPresent (sfAccountTxnID))
        {
            m_journal.trace << "Set AccountTxnID";
            mTxnAccount->makeFieldPresent (sfAccountTxnID);
         }

        if ((uClearFlag == asfAccountTxnID) && mTxnAccount->isFieldPresent (sfAccountTxnID))
        {
            m_journal.trace << "Clear AccountTxnID";
            mTxnAccount->makeFieldAbsent (sfAccountTxnID);
        }

        //
        // EmailHash
        //

        if (mTxn.isFieldPresent (sfEmailHash))
        {
            uint128 const uHash = mTxn.getFieldH128 (sfEmailHash);

            if (!uHash)
            {
                m_journal.trace << "unset email hash";
                mTxnAccount->makeFieldAbsent (sfEmailHash);
            }
            else
            {
                m_journal.trace << "set email hash";
                mTxnAccount->setFieldH128 (sfEmailHash, uHash);
            }
        }

        //
        // WalletLocator
        //

        if (mTxn.isFieldPresent (sfWalletLocator))
        {
            uint256 const uHash = mTxn.getFieldH256 (sfWalletLocator);

            if (!uHash)
            {
                m_journal.trace << "unset wallet locator";
                mTxnAccount->makeFieldAbsent (sfWalletLocator);
            }
            else
            {
                m_journal.trace << "set wallet locator";
                mTxnAccount->setFieldH256 (sfWalletLocator, uHash);
            }
        }

        //
        // MessageKey
        //

        if (mTxn.isFieldPresent (sfMessageKey))
        {
            Blob const messageKey = mTxn.getFieldVL (sfMessageKey);

            if (messageKey.size () > PUBLIC_BYTES_MAX)
            {
                m_journal.trace << "message key too long";
                return telBAD_PUBLIC_KEY;
            }

            if (messageKey.empty ())
            {
                m_journal.debug << "set message key";
                mTxnAccount->makeFieldAbsent (sfMessageKey);
            }
            else
            {
                m_journal.debug << "set message key";
                mTxnAccount->setFieldVL (sfMessageKey, messageKey);
            }
        }

        //
        // Domain
        //

        if (mTxn.isFieldPresent (sfDomain))
        {
            Blob const domain = mTxn.getFieldVL (sfDomain);

            if (domain.size () > DOMAIN_BYTES_MAX)
            {
                m_journal.trace << "domain too long";
                return telBAD_DOMAIN;
            }

            if (domain.empty ())
            {
                m_journal.trace << "unset domain";
                mTxnAccount->makeFieldAbsent (sfDomain);
            }
            else
            {
                m_journal.trace << "set domain";
                mTxnAccount->setFieldVL (sfDomain, domain);
            }
        }

        //
        // TransferRate
        //

        if (mTxn.isFieldPresent (sfTransferRate))
        {
            std::uint32_t uRate = mTxn.getFieldU32 (sfTransferRate);

            if (!uRate || uRate == QUALITY_ONE)
            {
                m_journal.trace << "unset transfer rate";
                mTxnAccount->makeFieldAbsent (sfTransferRate);
            }
            else if (uRate > QUALITY_ONE)
            {
                m_journal.trace << "set transfer rate";
                mTxnAccount->setFieldU32 (sfTransferRate, uRate);
            }
            else
            {
                m_journal.trace << "bad transfer rate";
                return temBAD_TRANSFER_RATE;
            }
        }

        if (uFlagsIn != uFlagsOut)
            mTxnAccount->setFieldU32 (sfFlags, uFlagsOut);

        return tesSUCCESS;
    }
};

TER
transact_SetAccount (
    STTx const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine)
{
    return SetAccount(txn, params, engine).apply ();
}

}
