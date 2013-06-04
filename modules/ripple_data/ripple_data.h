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

#include <cassert>
#include <algorithm>
#include <list>
#include <stdexcept>
#include <string>
#include <stdexcept>
#include <vector>

#include <boost/functional/hash.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/ptr_container/ptr_vector.hpp> // VFALCO: NOTE, this looks like junk

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>

//------------------------------------------------------------------------------

// VFALCO: TODO, try to reduce these dependencies
#include "../ripple_basics/ripple_basics.h"

#include "crypto/ripple_CBigNum.h"
#include "crypto/ripple_Base58.h" // VFALCO: TODO, Can be moved to .cpp if we clean up setAlphabet stuff
#include "crypto/ripple_Base58Data.h"

#include "format/ripple_FieldNames.h"
#include "format/ripple_RippleAddress.h"
#include "format/ripple_Serializer.h" // needs CKey
#include "format/ripple_TER.h"
#include "format/ripple_SerializedTypes.h" // needs Serializer, TER
#include "format/ripple_SerializedObject.h"
#include "format/ripple_LedgerFormat.h" // needs SOTemplate from SerializedObject
#include "format/ripple_TransactionFormat.h"

// VFALCO: TODO, resolve the location of this file
#include "ripple.pb.h"

#endif
