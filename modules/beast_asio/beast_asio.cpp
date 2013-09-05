//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include "BeastConfig.h"

#include "system/beast_OpenSSLIncludes.h"

#include "beast_asio.h"

namespace http_parser
{
#include "parsehttp/http_parser.h"
}

namespace beast
{

#include "async/beast_SharedHandler.cpp"

#include "basics/beast_PeerRole.cpp"
#include "basics/SSLContext.cpp"

#include "sockets/beast_SocketBase.cpp"
#include "sockets/beast_Socket.cpp"

#include "handshake/beast_HandshakeDetectLogicPROXY.cpp"

#include "tests/beast_PeerTest.cpp"
#include "tests/beast_TestPeerBasics.cpp"
#include "tests/beast_TestPeerLogic.cpp"
#include "tests/beast_TestPeerLogicProxyClient.cpp"
#include "tests/beast_TestPeerLogicSyncServer.cpp"
#include "tests/beast_TestPeerLogicSyncClient.cpp"
#include "tests/beast_TestPeerLogicAsyncServer.cpp"
#include "tests/beast_TestPeerLogicAsyncClient.cpp"
#include "tests/beast_TestPeerUnitTests.cpp"

#include "system/beast_BoostUnitTests.cpp"

}

//------------------------------------------------------------------------------

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable: 4127) // conditional expression is constant
#pragma warning (disable: 4244) // integer conversion, possible loss of data
#endif
namespace http_parser
{
#include "parsehttp/http_parser.c"
}
#ifdef _MSC_VER
#pragma warning (pop)
#endif
