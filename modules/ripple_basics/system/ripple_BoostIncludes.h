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

#define BOOST_FILESYSTEM_NO_DEPRECATED

#include <boost/filesystem.hpp>

//------------------------------------------------------------------------------

// Boost Unit Test Framework

#define BOOST_TEST_NO_LIB
#define BOOST_TEST_ALTERNATIVE_INIT_API
#define BOOST_TEST_NO_MAIN

#include <boost/test/unit_test.hpp>

#endif
