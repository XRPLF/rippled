//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_SSLUTIL_H_INCLUDED
#define RIPPLE_SSLUTIL_H_INCLUDED

#include "beast/modules/beast_core/beast_core.h"

#include "../types/ripple_types.h"

#include <openssl/bn.h>
#include <openssl/dh.h>
#include <openssl/ripemd.h>
#include <openssl/sha.h>

namespace ripple {
using namespace beast;
}

# include "api/bignum_error.h"
#include "api/CAutoBN_CTX.h"
#include "api/CBigNum.h"
#include "api/DHUtil.h"
#include "api/HashUtilities.h"

#endif
