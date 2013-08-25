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

// Must come before <boost/bind.hpp>
#include "beast/modules/beast_core/beast_core.h"

#include <boost/version.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio.hpp>
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>
#include <boost/array.hpp>
#include <boost/foreach.hpp>
#include <boost/unordered_map.hpp> // for unit test
#include <boost/mpl/at.hpp>
#include <boost/mpl/vector.hpp>

#include "beast/modules/beast_asio/beast_asio.h"

#include "ripple_asio.h"

namespace ripple
{

#include "sockets/ripple_MultiSocketType.h"
#include "sockets/RippleSSLContext.cpp"
#include "sockets/ripple_MultiSocket.cpp"

}
