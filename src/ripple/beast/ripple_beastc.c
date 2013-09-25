//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

/*  This file includes all of the C-language beast sources needed to link.
    By including them here, we avoid having to muck with the SConstruct
    Makefile, Project file, or whatever.

    Note that these sources must be compiled using the C compiler.
*/
    
#include "BeastConfig.h"

#ifdef __cplusplus
#error "Whoops! This file must be compiled with a C compiler!"
#endif

#include "../beast/modules/beast_sqlite/beast_sqlite.c"
