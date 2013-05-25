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

/**	Add this to get the @ref ripple_main module.

    @file ripple_main.cpp
    @ingroup ripple_main
*/

#include "ripple_main.h"

// VFALCO: TODO, fix these warnings!
#ifdef _MSC_VER
//#pragma warning (push) // Causes spurious C4503 "decorated name exceeds maximum length"
#pragma warning (disable: 4018) // signed/unsigned mismatch
#pragma warning (disable: 4244) // conversion, possible loss of data
#pragma warning (disable: 4535) // call requires /EHa
#endif

#include "src/cpp/ripple/Application.cpp"
#include "src/cpp/ripple/Config.cpp" // no log
#include "src/cpp/ripple/InstanceCounter.cpp" // no log
#include "src/cpp/ripple/JobQueue.cpp"
#include "src/cpp/ripple/LoadManager.cpp"
#include "src/cpp/ripple/LoadMonitor.cpp"
#include "src/cpp/ripple/UpdateTables.cpp"
#include "src/cpp/ripple/main.cpp"
#include "src/cpp/ripple/ValidationCollection.cpp"

#ifdef _MSC_VER
//#pragma warning (pop)
#endif
