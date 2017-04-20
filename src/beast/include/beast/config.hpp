//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_CONFIG_HPP
#define BEAST_CONFIG_HPP

/*
	_MSC_VER and _MSC_FULL_VER by version:

	14.0 (2015)	        1900	190023026
	14.0 (2015 Update 1)	1900	190023506
	14.0 (2015 Update 2)	1900	190023918
	14.0 (2015 Update 3)	1900	190024210
*/

#if defined(_MSC_FULL_VER)
#if _MSC_FULL_VER < 190024210
static_assert(false,
	"This library requires Visual Studio 2015 Update 3 or later");
#endif
#endif

#endif
