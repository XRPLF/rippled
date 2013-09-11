//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_PROOFOFWORKFACTORY_H_INCLUDED
#define RIPPLE_PROOFOFWORKFACTORY_H_INCLUDED

class ProofOfWorkFactory
{
public:
    enum
    {
        kMaxDifficulty = 30,
    };

    static ProofOfWorkFactory* New ();

    virtual ~ProofOfWorkFactory () { }

    virtual ProofOfWork getProof () = 0;

    virtual PowResult checkProof (const std::string& token, uint256 const& solution) = 0;

    virtual uint64 getDifficulty () = 0;

    virtual void setDifficulty (int i) = 0;

    virtual void loadHigh () = 0;

    virtual void loadLow () = 0;

    virtual void sweep () = 0;

    virtual uint256 const& getSecret () const = 0;

    virtual void setSecret (uint256 const& secret) = 0;
};

#endif
