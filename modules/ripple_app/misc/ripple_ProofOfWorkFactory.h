//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_PROOFOFWORKFACTORY_RIPPLEHEADER
#define RIPPLE_PROOFOFWORKFACTORY_RIPPLEHEADER

// PRIVATE HEADER

class ProofOfWorkFactory
    : public IProofOfWorkFactory
    , LeakChecked <ProofOfWorkFactory>
{
public:
    ProofOfWorkFactory ();

    ProofOfWork getProof ();
    POWResult checkProof (const std::string& token, uint256 const& solution);
    uint64 getDifficulty ()
    {
        return ProofOfWork::getDifficulty (mTarget, mIterations);
    }
    void setDifficulty (int i);

    void loadHigh ();
    void loadLow ();
    void sweep (void);

    uint256 const& getSecret () const
    {
        return mSecret;
    }
    void setSecret (uint256 const& secret)
    {
        mSecret = secret;
    }

    static int getPowEntry (uint256 const& target, int iterations);

private:
    typedef RippleMutex LockType;
    typedef LockType::ScopedLockType ScopedLockType;
    LockType mLock;

    uint256      mSecret;
    int          mIterations;
    uint256      mTarget;
    time_t       mLastDifficultyChange;
    int          mValidTime;
    int          mPowEntry;

    powMap_t     mSolvedChallenges;
};

#endif
