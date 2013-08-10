//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

/** Add this to get the @ref ripple_asio module.

    @file ripple_asio.cpp
    @ingroup ripple_asio
*/

//------------------------------------------------------------------------------

#include "BeastConfig.h"

#include "beast/modules/beast_basics/beast_basics.h"

#include "ripple_asio.h"

#include "ripple_asio_impl.h"

namespace ripple
{

#include "boost/ripple_IoService.cpp"
#include "boost/ripple_SslContext.cpp"

#include "sockets/ripple_RippleTlsContext.cpp"
#include "sockets/ripple_MultiSocket.cpp"

}
