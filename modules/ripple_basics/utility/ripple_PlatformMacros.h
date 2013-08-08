//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_PLATFORMMACROS_H
#define RIPPLE_PLATFORMMACROS_H

#define FUNCTION_TYPE beast::function
#define BIND_TYPE     beast::bind
#define P_1           beast::_1
#define P_2           beast::_2
#define P_3           beast::_3
#define P_4           beast::_4

// VFALCO TODO Clean this up

#if (!defined(FORCE_NO_C11X) && (__cplusplus > 201100L)) || defined(FORCE_C11X)

// VFALCO TODO Get rid of the C11X macro
#define C11X
#define UPTR_T          std::unique_ptr
#define MOVE_P(p)       std::move(p)

#else

#define UPTR_T          std::auto_ptr
#define MOVE_P(p)       (p)

#endif

// VFALCO TODO Clean this stuff up. Remove as much as possible
#define nothing()           do {} while (0)
#define fallthru()          do {} while (0)
#define NUMBER(x)           (sizeof(x)/sizeof((x)[0]))
#define isSetBit(x,y)       (!!((x) & (y)))

#endif
