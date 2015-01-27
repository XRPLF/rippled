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

#ifndef RIPPLE_CORE_LOADFEETRACKIMP_H_INCLUDED
#define RIPPLE_CORE_LOADFEETRACKIMP_H_INCLUDED

#include <ripple/protocol/JsonFields.h>
#include <ripple/core/LoadFeeTrack.h>
#include <mutex>

namespace ripple {

class LoadFeeTrackImp : public LoadFeeTrack
{
public:
    explicit LoadFeeTrackImp (bool standAlone, beast::Journal journal = beast::Journal())
        : m_journal (journal)
        , mLocalLoadLevel (lftReference)
        , mRemoteLoadLevel (lftReference)
        , mClusterLoadLevel (lftReference)
        , raiseCount (0)
        , openLedgerTxns (0)
        , closedLedgerTxns (0)
        , targetMultiplier (500)
        , targetTxnCount (50)
        , medianFee (256)
        , minimumTx (standAlone ? lftMinimumTxSA : lftMinimumTx)
    {
    }

    // Scale using load as well as base rate
    std::uint64_t scaleFeeLoad (
        std::uint64_t fee, // The number of fee units you want to scale
        std::uint64_t baseFee, // Cost of reference transaction in drops
        std::uint32_t referenceFeeUnits, // Cost of reference transaction in fee units
        bool bAdmin) override
    {
        static std::uint64_t midrange (0x00000000FFFFFFFF);

        bool big = (fee > midrange);

        if (big)                // big fee, divide first to avoid overflow
            fee /= referenceFeeUnits;
        else                    // normal fee, multiply first for accuracy
            fee *= baseFee;

        fee = scaleTxnFee (fee);

        if (big)                // Fee was big to start, must now multiply
            fee *= baseFee;
        else                    // Fee was small to start, must now divide
            fee /= referenceFeeUnits;

        return fee;
    }

    // Scale from fee units to drops
    std::uint64_t scaleFeeBase (std::uint64_t fee, std::uint64_t baseFee,
        std::uint32_t referenceFeeUnits) const override
    {
        return mulDiv (fee, baseFee, referenceFeeUnits);
    }

    std::uint32_t getLocalLevel () override
    {
        ScopedLockType sl (mLock);
        return mLocalLoadLevel;
    }

    std::uint32_t getLoadBase () override
    {
        return lftReference;
    }

    std::uint32_t getLoadFactor () override
    {
        ScopedLockType sl (mLock);
        return std::max(mClusterLoadLevel, std::max (mLocalLoadLevel, mRemoteLoadLevel));
    }

    int getMedianFee() override
    {
        ScopedLockType sl(mLock);
        return medianFee;
    }

    int getExpectedLedgerSize() override
    {
        ScopedLockType sl(mLock);
        return expectedLedgerSize();
    }

    void setClusterLevel(std::uint32_t level) override
    {
        ScopedLockType sl (mLock);
        mClusterLoadLevel = level;
    }

    std::uint32_t getClusterLevel () override
    {
        ScopedLockType sl (mLock);
        return mClusterLoadLevel;
    }

    bool isLoadedLocal () override
    {
        // VFALCO TODO This could be replaced with a SharedData and
        //             using a read/write lock instead of a critical section.
        //
        //        NOTE This applies to all the locking in this class.
        //
        //
        ScopedLockType sl (mLock);
        return (raiseCount != 0) || (mLocalLoadLevel != lftReference);
    }

    bool isLoadedCluster () override
    {
        // VFALCO TODO This could be replaced with a SharedData and
        //             using a read/write lock instead of a critical section.
        //
        //        NOTE This applies to all the locking in this class.
        //
        //
        ScopedLockType sl (mLock);
        return (raiseCount != 0) || (mLocalLoadLevel != lftReference) || (mClusterLoadLevel != lftReference);
    }

    bool raiseLocalLevel () override
    {
        ScopedLockType sl (mLock);

        if (++raiseCount < 2)
            return false;

        std::uint32_t origLevel = mLocalLoadLevel;

        if (mLocalLoadLevel < mRemoteLoadLevel)
            mLocalLoadLevel = mRemoteLoadLevel;

        mLocalLoadLevel += (mLocalLoadLevel / lftLevelIncFraction); // increment by 1/16th

        if (mLocalLoadLevel > lftLevelMax)
            mLocalLoadLevel = lftLevelMax;

        if (origLevel == mLocalLoadLevel)
            return false;

        m_journal.debug << "Local load level raised from " <<
            origLevel << " to " << mLocalLoadLevel;
        return true;
    }

    bool lowerLocalLevel () override
    {
        ScopedLockType sl (mLock);
        std::uint32_t origLevel = mLocalLoadLevel;
        raiseCount = 0;

        mLocalLoadLevel -= (mLocalLoadLevel / lftLevelDecFraction ); // reduce by 1/4

        if (mLocalLoadLevel < lftReference)
            mLocalLoadLevel = lftReference;

        if (origLevel == mLocalLoadLevel)
            return false;

        m_journal.debug << "Local load level lowered from " << origLevel << " to " << mLocalLoadLevel;
        return true;
    }

    void onTx (std::uint64_t fee) override
    {
        ScopedLockType sl (mLock);

        ++openLedgerTxns;

        m_journal.trace << "Tx count pending: "
            << openLedgerTxns;
    }

    int setMinimumTx (int m) override
    {
        ScopedLockType sl (mLock);

        int old = minimumTx;
        minimumTx = m;
        return old;
    }

    void onLedger (std::size_t openCount, std::vector<int> const& feesPaid, bool healthy) override
    {
        // Transactions in the closed ledger were accepted
        // Transactions in the open ledger are held over
        m_journal.debug <<
            "Tx count: before: " << openLedgerTxns <<
            " confirmed: " << feesPaid.size() <<
            " pending: " << openCount;

        ScopedLockType sl (mLock);

        openLedgerTxns = openCount;

        if (!healthy)
        {
            // As soon as we start having trouble, clamp down.
            closedLedgerTxns = feesPaid.size();
            closedLedgerTxns = std::min(closedLedgerTxns, targetTxnCount);
        }
        else if (feesPaid.size() > closedLedgerTxns)
        {
            // As long as we're staying healthy, keep fees down by
            // allowing more transactions per ledger at low cost.
            closedLedgerTxns = feesPaid.size();
        }

        if (!feesPaid.empty())
        {
            // In the case of an odd number of elements, this adds
            // the same element to itself and takes the average;
            // for an even number of elements, it will add the two
            // "middle" elements and average them.
            auto size = feesPaid.size();
            medianFee = (feesPaid[size / 2] +
                feesPaid[(size - 1) / 2] + 1) / 2;
        }
        else
        {
            medianFee = lftReference;
        }
    }

    // Scale the required fee level based on transaction load
    std::uint64_t scaleTxnFee (std::uint64_t baseFee) override
    {
        ScopedLockType sl (mLock);

        // The number of transactions accepted so far
        int openT = openLedgerTxns;

        // Target number of transactions allowed
        int closeT = expectedLedgerSize();

        if (openT > closeT)
        {
            // Compute fee level required for constant counts
            int multiplier = std::max(medianFee, targetMultiplier);
            baseFee *= openT * openT * multiplier;
            baseFee /= (closeT * closeT);
        }

        return baseFee;
    }

    std::uint64_t getTxnFeeReport () override
    {
        std::uint64_t base = getLoadBase ();
        std::uint64_t ret = scaleTxnFee (base);

        // If the fee is elevated, report a slightly
        // higher fee to clients
        if (ret != base)
            ret += ret / lftExtraFee;

        return ret;
    }

private:
    // VFALCO TODO Move this function to some "math utilities" file
    // compute (value)*(mul)/(div) - avoid overflow but keep precision
    static std::uint64_t mulDiv (std::uint64_t value, std::uint32_t mul, std::uint64_t div)
    {
        // VFALCO TODO replace with beast::literal64bitUnsigned ()
        //
        const std::uint64_t boundary = (0x00000000FFFFFFFF);

        if (value > boundary)                           // Large value, avoid overflow
            return (value / div) * mul;
        else                                            // Normal value, preserve accuracy
            return (value * mul) / div;
    }

    int expectedLedgerSize()
    {
        int closeT = minimumTx;
        closeT = std::max(closeT, closedLedgerTxns);
        closeT = std::min(closeT, targetTxnCount - 1);
        return closeT;
    }

private:
    static const int lftReference = 256;        // 256 means normal (DO NOT CHANGE)
    static const int lftExtraFee = 3;           // boost reported fee by 1/3
    static const int lftMinimumTx = 5;          // don't raise fee for first 5 transactions
    static const int lftMinimumTxSA = 100;      // Allow more in standalone mode

    static const int lftLevelIncFraction = 4;     // increase level by 1/4
    static const int lftLevelDecFraction = 4;     // decrease level by 1/4
    static const int lftLevelMax = lftReference * 1000000;

    beast::Journal m_journal;
    using LockType = std::mutex;
    using ScopedLockType = std::lock_guard <LockType>;
    LockType mLock;

    std::uint32_t mLocalLoadLevel;        // Scale factor, lftReference = normal
    std::uint32_t mRemoteLoadLevel;       // Scale factor, lftReference = normal
    std::uint32_t mClusterLoadLevel;      // Scale factor, lftReference = normal
    int raiseCount;

    int openLedgerTxns;
    int closedLedgerTxns;
    int targetMultiplier;
    int targetTxnCount;
    int medianFee;
    int minimumTx;
};

} // ripple

#endif
