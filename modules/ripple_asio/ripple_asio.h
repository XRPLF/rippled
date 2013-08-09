//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_ASIO_H_INCLUDED
#define RIPPLE_ASIO_H_INCLUDED

#include "beast/modules/beast_core/beast_core.h"
#include "beast/modules/beast_asio/beast_asio.h"

// Must be outside the namespace

#include "ripple_asio_fwdecl.h"

/** Abstractions for boost::asio

    This is the first step to removing the dependency on boost::asio.
    These classes are designed to move boost::asio header material out of
    the majority of include paths.

    @ingroup ripple_asio
    @file ripple_asio.h
*/
namespace ripple
{

using namespace beast;

#include "boost/ripple_IoService.h"
#include "boost/ripple_SslContext.h"

#include "sockets/ripple_MultiSocket.h"
# include "sockets/ripple_RippleTlsContext.h"
#include "sockets/ripple_MultiSocketType.h"

}

#endif
