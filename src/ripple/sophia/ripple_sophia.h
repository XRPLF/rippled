//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_SOPHIA_H_INCLUDED
#define RIPPLE_SOPHIA_H_INCLUDED

#include "beast/modules/beast_core/system/TargetPlatform.h"

#if ! BEAST_WIN32

#define RIPPLE_SOPHIA_AVAILABLE 1

#ifdef __cplusplus
extern "C" {
#endif

#include "../sophia/db/sophia.h"

#ifdef __cplusplus
}
#endif

#else

#define RIPPLE_SOPHIA_AVAILABLE 0

#endif

#endif
