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

#ifndef RIPPLE_PLATFORMMACROS_H
#define RIPPLE_PLATFORMMACROS_H

// VFALCO: TODO Clean this up

#if (!defined(FORCE_NO_C11X) && (__cplusplus > 201100L)) || defined(FORCE_C11X)

#define C11X
#include			 	<functional>
#define UPTR_T			std::unique_ptr
#define MOVE_P(p)		std::move(p)
#define BIND_TYPE		std::bind
#define FUNCTION_TYPE	std::function
#define P_1				std::placeholders::_1
#define P_2				std::placeholders::_2
#define P_3				std::placeholders::_3
#define P_4				std::placeholders::_4

#else

#include 				<boost/bind.hpp>
#include				<boost/function.hpp>
#define UPTR_T			std::auto_ptr
#define MOVE_P(p)		(p)
#define BIND_TYPE		boost::bind
#define FUNCTION_TYPE	boost::function
#define P_1				_1
#define P_2				_2
#define P_3				_3
#define P_4				_4

#endif

#endif
