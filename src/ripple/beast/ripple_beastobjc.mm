//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

/*  This file includes all of the beast sources needed to link.
    By including them here, we avoid having to muck with the SConstruct
    Makefile, Project file, or whatever.
*/

// MUST come first!
#include "BeastConfig.h"

// Include this to get all the basic includes included, to prevent errors
#include "../beast/modules/beast_core/beast_core.h"

#include "../beast/modules/beast_core/beast_core.cpp"
