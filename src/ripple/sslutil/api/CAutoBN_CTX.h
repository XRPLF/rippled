//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#ifndef RIPPLE_SSLUTIL_CAUTOBN_CTX_H_INCLUDED
#define RIPPLE_SSLUTIL_CAUTOBN_CTX_H_INCLUDED

#include "bignum_error.h"

namespace ripple {

class CAutoBN_CTX : public Uncopyable
{
protected:
    BN_CTX* pctx;
    CAutoBN_CTX& operator= (BN_CTX* pnew)
    {
        pctx = pnew;
        return *this;
    }

public:
    CAutoBN_CTX ()
    {
        pctx = BN_CTX_new ();

        if (pctx == nullptr)
            throw bignum_error ("CAutoBN_CTX : BN_CTX_new() returned nullptr");
    }

    ~CAutoBN_CTX ()
    {
        if (pctx != nullptr)
            BN_CTX_free (pctx);
    }

    operator BN_CTX* ()
    {
        return pctx;
    }
    BN_CTX& operator* ()
    {
        return *pctx;
    }
    BN_CTX** operator& ()
    {
        return &pctx;
    }
    bool operator! ()
    {
        return (pctx == nullptr);
    }
};

}

#endif
