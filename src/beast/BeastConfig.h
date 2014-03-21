//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_BEASTCONFIG_H_INCLUDED
#define BEAST_BEASTCONFIG_H_INCLUDED

/** Configuration file for Beast.
    This sets various configurable options for Beast. In order to compile you
    must place a copy of this file in a location where your build environment
    can find it, and then customize its contents to suit your needs.
    @file BeastConfig.h
*/

//------------------------------------------------------------------------------
//
// Unit Tests
//
//------------------------------------------------------------------------------

/** Config: BEAST_NO_UNIT_TEST_INLINE
    Prevents unit test definitions from being inserted into a global table.
*/
#ifndef BEAST_NO_UNIT_TEST_INLINE
#define BEAST_NO_UNIT_TEST_INLINE 0
#endif

//------------------------------------------------------------------------------
//
// Diagnostics
//
//------------------------------------------------------------------------------

/** Config: BEAST_FORCE_DEBUG
    Normally, BEAST_DEBUG is set to 1 or 0 based on compiler and project
    settings, but if you define this value, you can override this to force it
    to be true or false.
*/
#ifndef   BEAST_FORCE_DEBUG
//#define BEAST_FORCE_DEBUG 1
#endif

/** Config: BEAST_LOG_ASSERTIONS
    If this flag is enabled, the the bassert and bassertfalse macros will always
    use Logger::writeToLog() to write a message when an assertion happens.
    Enabling it will also leave this turned on in release builds. When it's
    disabled, however, the bassert and bassertfalse macros will not be compiled
    in a release build.
    @see bassert, bassertfalse, Logger
*/
#ifndef   BEAST_LOG_ASSERTIONS
//#define BEAST_LOG_ASSERTIONS 1
#endif

/** Config: BEAST_CHECK_MEMORY_LEAKS
    Enables a memory-leak check for certain objects when the app terminates.
    See the LeakChecked class for more details about enabling leak checking for
    specific classes.
*/
#ifndef   BEAST_CHECK_MEMORY_LEAKS
//#define BEAST_CHECK_MEMORY_LEAKS 0
#endif

/** Config: BEAST_COMPILER_CHECKS_SOCKET_OVERRIDES
    Setting this option makes Socket-derived classes generate compile errors
    if they forget any of the virtual overrides As some Socket-derived classes
    intentionally omit member functions that are not applicable, this macro
    should only be enabled temporarily when writing your own Socket-derived
    class, to make sure that the function signatures match as expected.
*/
#ifndef   BEAST_COMPILER_CHECKS_SOCKET_OVERRIDES
//#define BEAST_COMPILER_CHECKS_SOCKET_OVERRIDES 1
#endif

//------------------------------------------------------------------------------
//
// Libraries
//
//------------------------------------------------------------------------------

/** Config: BEAST_DONT_AUTOLINK_TO_WIN32_LIBRARIES
    In a Visual C++  build, this can be used to stop the required system libs
    being automatically added to the link stage.
*/
#ifndef   BEAST_DONT_AUTOLINK_TO_WIN32_LIBRARIES
//#define BEAST_DONT_AUTOLINK_TO_WIN32_LIBRARIES 1
#endif

/** Config: BEAST_INCLUDE_ZLIB_CODE
    This can be used to disable Beast's embedded 3rd-party zlib code.
    You might need to tweak this if you're linking to an external zlib library
    in your app, but for normal apps, this option should be left alone.

    If you disable this, you might also want to set a value for
    BEAST_ZLIB_INCLUDE_PATH, to specify the path where your zlib headers live.
*/
#ifndef   BEAST_INCLUDE_ZLIB_CODE
//#define BEAST_INCLUDE_ZLIB_CODE 1
#endif

/** Config: BEAST_ZLIB_INCLUDE_PATH
    This is included when BEAST_INCLUDE_ZLIB_CODE is set to zero.
*/
#ifndef BEAST_ZLIB_INCLUDE_PATH
#define BEAST_ZLIB_INCLUDE_PATH <zlib.h>
#endif

/** Config: BEAST_SQLITE_FORCE_NDEBUG
    Setting this option forces sqlite into release mode even if NDEBUG is not set
*/
#ifndef   BEAST_SQLITE_FORCE_NDEBUG
//#define BEAST_SQLITE_FORCE_NDEBUG 1
#endif

/** Config: BEAST_FUNCTIONAL_USES_###
    <functional> source configuration.
    Set one of these to manually force a particular implementation of bind().
    If nothing is chosen then beast will use whatever is appropriate for your
    environment based on what is available.
    If you override these, set ONE to 1 and the rest to 0
*/
#ifndef   BEAST_FUNCTIONAL_USES_STD
//#define BEAST_FUNCTIONAL_USES_STD    0
#endif
#ifndef   BEAST_FUNCTIONAL_USES_TR1
//#define BEAST_FUNCTIONAL_USES_TR1    0
#endif
#ifndef   BEAST_FUNCTIONAL_USES_BOOST
//#define BEAST_FUNCTIONAL_USES_BOOST  0
#endif

//------------------------------------------------------------------------------
//
// Boost
//
//------------------------------------------------------------------------------

/** Config: BEAST_USE_BOOST_FEATURES
    This activates boost specific features and improvements. If this is
    turned on, the include paths for your build environment must be set
    correctly to find the boost headers.
*/
#ifndef   BEAST_USE_BOOST_FEATURES
//#define BEAST_USE_BOOST_FEATURES 1
#endif

#endif
