//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_PROOFOFWORK_H
#define RIPPLE_PROOFOFWORK_H

class ProofOfWork : LeakChecked <ProofOfWork>
{
public:
    enum
    {
        kMaxIterations = (1 << 23)
    };

    typedef boost::shared_ptr <ProofOfWork> pointer;

    ProofOfWork (const std::string& token,
                 int iterations,
                 uint256 const& challenge,
                 uint256 const& target);

    explicit ProofOfWork (const std::string& token);

    bool isValid () const;

    uint256 solve (int maxIterations = 2 * kMaxIterations) const;
    bool checkSolution (uint256 const& solution) const;

    const std::string& getToken () const
    {
        return mToken;
    }
    uint256 const& getChallenge () const
    {
        return mChallenge;
    }

    uint64 getDifficulty () const
    {
        return getDifficulty (mTarget, mIterations);
    }

    // approximate number of hashes needed to solve
    static uint64 getDifficulty (uint256 const& target, int iterations);

    static bool validateToken (const std::string& strToken);

    static bool calcResultInfo (PowResult powCode, std::string& strToken, std::string& strHuman);

private:
    std::string     mToken;
    uint256         mChallenge;
    uint256         mTarget;
    int             mIterations;

    static const uint256 sMinTarget;
    static const int maxIterations;
};

#endif

// vim:ts=4
