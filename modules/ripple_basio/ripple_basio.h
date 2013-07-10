//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_BASIO_H_INCLUDED
#define RIPPLE_BASIO_H_INCLUDED

#include "beast/modules/beast_core/beast_core.h"

// Must be outside the namespace

#include "ripple_basio_fwdecl.h"

/** Abstractions for boost::asio

    This is the first step to removing the dependency on boost::asio.
    These classes are designed to move boost::asio header material out of
    the majority of include paths.

    @ingroup ripple_basio
    @file ripple_basio.h
*/
namespace ripple
{

using namespace beast;

#include "boost/ripple_IoService.h"

}

#endif
