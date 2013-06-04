//------------------------------------------------------------------------------
/*
	Copyright (c) 2011-2013, OpenCoin, Inc.

	Permission to use, copy, modify, and/or distribute this software for any
	purpose with  or without fee is hereby granted,  provided that the above
	copyright notice and this permission notice appear in all copies.

	THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
	WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES OF
	MERCHANTABILITY  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
	ANY SPECIAL,  DIRECT, INDIRECT,  OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
	WHATSOEVER  RESULTING  FROM LOSS OF USE, DATA OR PROFITS,  WHETHER IN AN
	ACTION OF CONTRACT, NEGLIGENCE  OR OTHER TORTIOUS ACTION, ARISING OUT OF
	OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

/**	Include this to get the @ref ripple_data module.

    @file ripple_data.h
    @ingroup ripple_data
*/

/**	Ripple specific data representation and manipulation.

	These form the building blocks of Ripple data.

	@defgroup ripple_data
*/

#ifndef RIPPLE_DATA_H
#define RIPPLE_DATA_H

// Base58Data
#include <string>
#include <algorithm>
#include <boost/functional/hash.hpp>

// CBigNum
#include <stdexcept>
#include <vector>
#include <openssl/bn.h>

// CKey
#include <stdexcept>
#include <vector>
#include <cassert>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>
#include <boost/shared_ptr.hpp>

// Serializer
#include <vector>
#include <string>
#include <list>
#include <boost/shared_ptr.hpp>

// VFALCO: TODO, try to reduce these dependencies
#include "../ripple_basics/ripple_basics.h"

#include "crypto/ripple_CBigNum.h"
#include "crypto/ripple_Base58.h" // VFALCO: TODO, Can be moved to .cpp if we clean up setAlphabet stuff
#include "crypto/ripple_Base58Data.h"

#include "types/ripple_FieldNames.h"
#include "types/ripple_RippleAddress.h"
#include "types/ripple_Serializer.h" // needs CKey

#include "src/cpp/ripple/SerializedTypes.h"

// VFALCO: TODO, resolve the location of this file
#include "ripple.pb.h"

#endif
