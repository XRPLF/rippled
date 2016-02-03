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

#ifndef BEAST_BEASTCONFIG_H_INCLUDED
#define BEAST_BEASTCONFIG_H_INCLUDED

/** Configuration file for Beast
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
    The default is to include inline unit test definitions.
*/

#ifndef BEAST_NO_UNIT_TEST_INLINE
//#define BEAST_NO_UNIT_TEST_INLINE 1
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

/** Config: BEAST_CHECK_MEMORY_LEAKS
    Enables a memory-leak check for certain objects when the app terminates.
*/
#ifndef   BEAST_CHECK_MEMORY_LEAKS
//#define BEAST_CHECK_MEMORY_LEAKS 0
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
#ifndef BEAST_SQLITE_FORCE_NDEBUG
#define BEAST_SQLITE_FORCE_NDEBUG 1
#endif

//------------------------------------------------------------------------------
//
// Ripple
//
//------------------------------------------------------------------------------

/** Config: RIPPLE_VERIFY_NODEOBJECT_KEYS
    This verifies that the hash of node objects matches the payload.
    It is quite expensive so normally this is turned off!
*/
#ifndef   RIPPLE_VERIFY_NODEOBJECT_KEYS
//#define RIPPLE_VERIFY_NODEOBJECT_KEYS 1
#endif

/** Config: RIPPLE_DUMP_LEAKS_ON_EXIT
    Displays heap blocks and counted objects which were not disposed of
    during exit.
*/
#ifndef RIPPLE_DUMP_LEAKS_ON_EXIT
#define RIPPLE_DUMP_LEAKS_ON_EXIT 1
#endif

//------------------------------------------------------------------------------

// These control whether or not certain functionality gets
// compiled into the resulting rippled executable

/** Config: RIPPLE_ROCKSDB_AVAILABLE
    Controls whether or not the RocksDB database back-end is compiled into
    rippled. RocksDB requires a relatively modern C++ compiler (tested with
    gcc versions 4.8.1 and later) that supports some C++11 features.
*/
#ifndef   RIPPLE_ROCKSDB_AVAILABLE
//#define RIPPLE_ROCKSDB_AVAILABLE 0
#endif

//------------------------------------------------------------------------------

// Here temporarily to turn off new Validations code while it
// is being written.
//
#ifndef RIPPLE_USE_VALIDATORS
#define RIPPLE_USE_VALIDATORS 0
#endif

/** Config: RIPPLE_SINGLE_IO_SERVICE_THREAD
    When set, restricts the number of threads calling io_service::run to one.
    This is useful when debugging.
*/
#ifndef RIPPLE_SINGLE_IO_SERVICE_THREAD
#define RIPPLE_SINGLE_IO_SERVICE_THREAD 0
#endif

// Uses OpenSSL instead of alternatives
#ifndef RIPPLE_USE_OPENSSL
#define RIPPLE_USE_OPENSSL 1
#endif

#endif
