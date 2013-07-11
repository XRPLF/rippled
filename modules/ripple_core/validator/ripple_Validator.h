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
    typedef RippleAddress PublicKey;

    Validator ();

private:
    PublicKey m_publicKey;
};

#endif
