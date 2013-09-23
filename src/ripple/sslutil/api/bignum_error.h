//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#ifndef RIPPLE_SSLUTIL_BIGNUM_ERROR_H_INCLUDED
#define RIPPLE_SSLUTIL_BIGNUM_ERROR_H_INCLUDED

#include <stdexcept>

namespace ripple {

class bignum_error : public std::runtime_error
{
public:
    explicit bignum_error (std::string const& str)
        : std::runtime_error (str)
    {
    }
};

}

#endif
