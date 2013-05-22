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

/**	Add this to get the @ref ripple_client module.

    @file ripple_client.cpp
    @ingroup ripple_client
*/

#include "ripple_client.h"

// VFALCO: TODO, fix these warnings!
#ifdef _MSC_VER
//#pragma warning (push) // Causes spurious C4503 "decorated name exceeds maximum length"
#pragma warning (disable: 4018) // signed/unsigned mismatch
#pragma warning (disable: 4244) // conversion, possible loss of data
#endif

#include "src/cpp/ripple/CallRPC.cpp"
#include "src/cpp/ripple/HttpsClient.cpp"
#include "src/cpp/ripple/rpc.cpp"
#include "src/cpp/ripple/RPCDoor.cpp"
#include "src/cpp/ripple/RPCErr.cpp"
#include "src/cpp/ripple/RPCHandler.cpp"
#include "src/cpp/ripple/RPCServer.cpp"
#include "src/cpp/ripple/RPCSub.cpp"

#ifdef _MSC_VER
//#pragma warning (pop)
#endif
