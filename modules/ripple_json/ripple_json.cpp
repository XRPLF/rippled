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

/**	Add this to get the @ref ripple_json module.

    @file ripple_json.cpp
    @ingroup ripple_json
*/

#include "ripple_json.h"

// VFALCO: TODO Remove unneeded includes
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <stdio.h>
#include <string.h>
#include <utility>

#ifdef JSON_USE_CPPTL
# include <cpptl/conststring.h>
#endif

// VFALCO: TODO, eliminate this boost dependency
#include <boost/lexical_cast.hpp>

#ifndef JSON_USE_SIMPLE_INTERNAL_ALLOCATOR
#include "json/json_batchallocator.h"
#endif

#define JSON_ASSERT_UNREACHABLE assert( false )
#define JSON_ASSERT( condition ) assert( condition );  // @todo <= change this into an exception throw
#define JSON_ASSERT_MESSAGE( condition, message ) if (!( condition )) throw std::runtime_error( message );

//#if _MSC_VER >= 1400 // VC++ 8.0
//#pragma warning( disable : 4996 )   // disable warning about strdup being deprecated.
//#endif

#include "json/json_reader.cpp"
#include "json/json_value.cpp"
#include "json/json_writer.cpp"

