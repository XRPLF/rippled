//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATOR_H_INCLUDED
#define RIPPLE_VALIDATOR_H_INCLUDED

/** Fixed information on a validator.
*/
struct ValidatorInfo
{
    // VFALCO TODO magic number argh!!!
    //             This type should be located elsewhere.
    //
    typedef UnsignedInteger <33> PublicKey;

    PublicKey publicKey;
    String friendlyName;
    String organizationType;
    String jurisdicton;
};

/** Identifies a validator.

    A validator signs ledgers and participates in the consensus process.
*/
class Validator : public SharedObject
{
public:
    typedef SharedObjectPtr <Validator> Ptr;

    typedef ValidatorInfo::PublicKey PublicKey;

public:
    explicit Validator (PublicKey const& publicKey);

    //Validator (Validator const&);

    PublicKey const& getPublicKey () const { return m_publicKey; }

    // not thread safe
    void incrementWeight () { ++m_weight; }

private:
    ValidatorInfo m_info;
    PublicKey m_publicKey;
    int m_weight;
};

#endif
