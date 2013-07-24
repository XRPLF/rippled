//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_BOOSTINCLUDES_RIPPLEHEADER
#define RIPPLE_BOOSTINCLUDES_RIPPLEHEADER

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
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/bind.hpp>
#include <boost/cstdint.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/function.hpp>
#include <boost/functional/hash.hpp>
#include <boost/lexical_cast.hpp>
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
