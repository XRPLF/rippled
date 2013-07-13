//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATOR_H_INCLUDED
#define RIPPLE_VALIDATOR_H_INCLUDED

/** Identifies a validator.

    A validator signs ledgers and participates in the consensus process.
*/
class Validator
{
public:
    // VFALCO TODO magic number argh!!!
    //             This type should be located elsewhere.
    //
    typedef UnsignedInteger <33> PublicKey;

    explicit Validator (PublicKey const& publicKey);

    //Validator (Validator const&);

    PublicKey const& getPublicKey () const { return m_publicKey; }

    // not thread safe
    void incrementWeight () { ++m_weight; }

private:
    PublicKey m_publicKey;
    int m_weight;
};

#endif
