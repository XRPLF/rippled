//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
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

/** Config: BEAST_CATCH_UNHANDLED_EXCEPTIONS
    This will wrap certain operating system calls with exception catching
    code that converts the system exception into a regular error.
*/
#ifndef   BEAST_CATCH_UNHANDLED_EXCEPTIONS
//#define BEAST_CATCH_UNHANDLED_EXCEPTIONS 0
#endif

/** Config: BEAST_CHECK_MEMORY_LEAKS
    Enables a memory-leak check for certain objects when the app terminates.
    See the LeakChecked class for more details about enabling leak checking for
    specific classes.
*/
#ifndef   BEAST_CHECK_MEMORY_LEAKS
//#define BEAST_CHECK_MEMORY_LEAKS 0
#endif

/** Config: BEAST_DISABLE_CONTRACT_CHECKS
    Set this to 1 to prevent check_contract macros from evaluating their
    conditions, which might be expensive. meet_contract macros will still
    evaluate their conditions since their return values are checked.
*/
#ifndef   BEAST_DISABLE_CONTRACT_CHECKS
//#define BEAST_DISABLE_CONTRACT_CHECKS 1
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

/** Config: BEAST_DISABLE_BEAST_VERSION_PRINTING
    Turns off the debugging display of the beast version number
*/
#ifndef   BEAST_DISABLE_BEAST_VERSION_PRINTING
//#define BEAST_DISABLE_BEAST_VERSION_PRINTING 1
#endif

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
#ifndef BEAST_USE_BOOST_FEATURES
#define BEAST_USE_BOOST_FEATURES 1
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

/** Config: RIPPLE_TRACK_MUTEXES
    Turns on a feature that enables tracking and diagnostics for mutex
    and recursive mutex objects. This affects the type of lock used
    by RippleMutex and RippleRecursiveMutex
    @note This can slow down performance considerably.
*/
#ifndef RIPPLE_TRACK_MUTEXES
#define RIPPLE_TRACK_MUTEXES 0
#endif

//------------------------------------------------------------------------------

// This is only here temporarily. Use it to turn off MultiSocket
// in Peer code if you suspect you're having problems because of it.
//
#ifndef RIPPLE_PEER_USES_BEAST_MULTISOCKET
#define RIPPLE_PEER_USES_BEAST_MULTISOCKET 0
#endif

#endif
