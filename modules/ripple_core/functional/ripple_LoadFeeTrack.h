//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_LOADFEETRACK_RIPPLEHEADER
#define RIPPLE_LOADFEETRACK_RIPPLEHEADER

// PRIVATE HEADER
class LoadManager;

class LoadFeeTrack : public ILoadFeeTrack
{
public:
    LoadFeeTrack ()
        : mLocalTxnLoadFee (lftNormalFee)
        , mRemoteTxnLoadFee (lftNormalFee)
        , raiseCount (0)
    {
    }

    // Scale using load as well as base rate
    uint64 scaleFeeLoad (uint64 fee, uint64 baseFee, uint32 referenceFeeUnits, bool bAdmin)
    {
        static uint64 midrange (0x00000000FFFFFFFF);

        bool big = (fee > midrange);

        if (big)                // big fee, divide first to avoid overflow
            fee /= baseFee;
        else                    // normal fee, multiply first for accuracy
            fee *= referenceFeeUnits;

        uint32 feeFactor = std::max (mLocalTxnLoadFee, mRemoteTxnLoadFee);

        // Let admins pay the normal fee until the local load exceeds four times the remote
        if (bAdmin && (feeFactor > mRemoteTxnLoadFee) && (feeFactor < (4 * mRemoteTxnLoadFee)))
            feeFactor = mRemoteTxnLoadFee;

        {
            boost::mutex::scoped_lock sl (mLock);
            fee = mulDiv (fee, feeFactor, lftNormalFee);
        }

        if (big)                // Fee was big to start, must now multiply
            fee *= referenceFeeUnits;
        else                    // Fee was small to start, mst now divide
            fee /= baseFee;

        return fee;
    }

    // Scale from fee units to millionths of a ripple
    uint64 scaleFeeBase (uint64 fee, uint64 baseFee, uint32 referenceFeeUnits)
    {
        return mulDiv (fee, referenceFeeUnits, baseFee);
    }

    uint32 getRemoteFee ()
    {
        boost::mutex::scoped_lock sl (mLock);
        return mRemoteTxnLoadFee;
    }

    uint32 getLocalFee ()
    {
        boost::mutex::scoped_lock sl (mLock);
        return mLocalTxnLoadFee;
    }

    uint32 getLoadBase ()
    {
        return lftNormalFee;
    }

    uint32 getLoadFactor ()
    {
        boost::mutex::scoped_lock sl (mLock);
        return std::max (mLocalTxnLoadFee, mRemoteTxnLoadFee);
    }

    bool isLoaded ()
    {
        // VFALCO TODO This could be replaced with a SharedData and
        //             using a read/write lock instead of a critical section.
        //
        //        NOTE This applies to all the locking in this class.
        //
        //
        boost::mutex::scoped_lock sl (mLock);
        return (raiseCount != 0) || (mLocalTxnLoadFee != lftNormalFee);
    }

    void setRemoteFee (uint32 f)
    {
        boost::mutex::scoped_lock sl (mLock);
        mRemoteTxnLoadFee = f;
    }

    bool raiseLocalFee ()
    {
        boost::mutex::scoped_lock sl (mLock);

        if (++raiseCount < 2)
            return false;

        uint32 origFee = mLocalTxnLoadFee;

        if (mLocalTxnLoadFee < mRemoteTxnLoadFee) // make sure this fee takes effect
            mLocalTxnLoadFee = mRemoteTxnLoadFee;

        mLocalTxnLoadFee += (mLocalTxnLoadFee / lftFeeIncFraction); // increment by 1/16th

        if (mLocalTxnLoadFee > lftFeeMax)
            mLocalTxnLoadFee = lftFeeMax;

        if (origFee == mLocalTxnLoadFee)
            return false;

        WriteLog (lsDEBUG, LoadManager) << "Local load fee raised from " << origFee << " to " << mLocalTxnLoadFee;
        return true;
    }

    bool lowerLocalFee ()
    {
        boost::mutex::scoped_lock sl (mLock);
        uint32 origFee = mLocalTxnLoadFee;
        raiseCount = 0;

        mLocalTxnLoadFee -= (mLocalTxnLoadFee / lftFeeDecFraction ); // reduce by 1/4

        if (mLocalTxnLoadFee < lftNormalFee)
            mLocalTxnLoadFee = lftNormalFee;

        if (origFee == mLocalTxnLoadFee)
            return false;

        WriteLog (lsDEBUG, LoadManager) << "Local load fee lowered from " << origFee << " to " << mLocalTxnLoadFee;
        return true;
    }

    Json::Value getJson (uint64 baseFee, uint32 referenceFeeUnits)
    {
        Json::Value j (Json::objectValue);

        {
            boost::mutex::scoped_lock sl (mLock);

            // base_fee = The cost to send a "reference" transaction under no load, in millionths of a Ripple
            j["base_fee"] = Json::Value::UInt (baseFee);

            // load_fee = The cost to send a "reference" transaction now, in millionths of a Ripple
            j["load_fee"] = Json::Value::UInt (
                                mulDiv (baseFee, std::max (mLocalTxnLoadFee, mRemoteTxnLoadFee), lftNormalFee));
        }

        return j;
    }

private:
    // VFALCO TODO Move this function to some "math utilities" file
    // compute (value)*(mul)/(div) - avoid overflow but keep precision
    uint64 mulDiv (uint64 value, uint32 mul, uint64 div)
    {
        // VFALCO TODO replace with beast::literal64bitUnsigned ()
        //
        static uint64 boundary = (0x00000000FFFFFFFF);

        if (value > boundary)                           // Large value, avoid overflow
            return (value / div) * mul;
        else                                            // Normal value, preserve accuracy
            return (value * mul) / div;
    }

private:
    static const int lftNormalFee = 256;        // 256 is the minimum/normal load factor
    static const int lftFeeIncFraction = 16;    // increase fee by 1/16
    static const int lftFeeDecFraction = 4;     // decrease fee by 1/4
    static const int lftFeeMax = lftNormalFee * 1000000;

    uint32 mLocalTxnLoadFee;        // Scale factor, lftNormalFee = normal fee
    uint32 mRemoteTxnLoadFee;       // Scale factor, lftNormalFee = normal fee
    int raiseCount;

    boost::mutex mLock;
};

#endif
