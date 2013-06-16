//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_PLATFORMMACROS_H
#define RIPPLE_PLATFORMMACROS_H

// VFALCO TODO Clean this up

#if (!defined(FORCE_NO_C11X) && (__cplusplus > 201100L)) || defined(FORCE_C11X)

// VFALCO TODO replace BIND_TYPE with a namespace lift

#define C11X
#include                <functional>
#define UPTR_T          std::unique_ptr
#define MOVE_P(p)       std::move(p)
#define BIND_TYPE       std::bind
#define FUNCTION_TYPE   std::function
#define P_1             std::placeholders::_1
#define P_2             std::placeholders::_2
#define P_3             std::placeholders::_3
#define P_4             std::placeholders::_4

#else

#define UPTR_T          std::auto_ptr
#define MOVE_P(p)       (p)
#define BIND_TYPE       boost::bind
#define FUNCTION_TYPE   boost::function
#define P_1             _1
#define P_2             _2
#define P_3             _3
#define P_4             _4

#endif

// VFALCO TODO Clean this junk up
#define nothing()           do {} while (0)
#define fallthru()          do {} while (0)
#define NUMBER(x)           (sizeof(x)/sizeof((x)[0]))
#define isSetBit(x,y)       (!!((x) & (y)))

#endif
