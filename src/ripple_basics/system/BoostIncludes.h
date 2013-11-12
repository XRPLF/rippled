//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_BOOSTINCLUDES_H_INCLUDED
#define RIPPLE_BOOSTINCLUDES_H_INCLUDED

// All Boost includes used throughout Ripple.
//
// This shows all the dependencies in one place. Please do not add
// boost includes anywhere else in the source code. If possible, do
// not add any more includes.
//
// A long term goal is to reduce and hopefully eliminate the usage of boost.
//

#include <boost/version.hpp>

#if BOOST_VERSION < 104700
# error Ripple requires Boost version 1.47 or later
#endif

// This is better than setting it in some Makefile or IDE Project file.
//
#define BOOST_FILESYSTEM_NO_DEPRECATED

#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/function.hpp>
#include <boost/make_shared.hpp>
#include <boost/ptr_container/ptr_vector.hpp> // VFALCO NOTE this looks like junk
#include <boost/ref.hpp>
#include <boost/regex.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/unordered_map.hpp>

#endif
