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

#if BEAST_INCLUDE_BEASTCONFIG
#include <BeastConfig.h>
#endif

#include <modules/beast_asio/system/OpenSSLIncludes.h>

#include <modules/beast_asio/beast_asio.h>

#include <beast/http/impl/joyent_parser.h>

#include <modules/beast_asio/basics/PeerRole.cpp>
#include <modules/beast_asio/basics/SSLContext.cpp>

#include <modules/beast_asio/protocol/HandshakeDetectLogicPROXY.cpp>

#include <modules/beast_asio/http/HTTPParserImpl.h>
#include <modules/beast_asio/http/HTTPClientType.cpp>
#include <modules/beast_asio/http/HTTPField.cpp>
#include <modules/beast_asio/http/HTTPHeaders.cpp>
#include <modules/beast_asio/http/HTTPMessage.cpp>
#include <modules/beast_asio/http/HTTPRequest.cpp>
#include <modules/beast_asio/http/HTTPResponse.cpp>
#include <modules/beast_asio/http/HTTPVersion.cpp>

#include <modules/beast_asio/tests/PeerTest.cpp>
#include <modules/beast_asio/tests/TestPeerBasics.cpp>
#include <modules/beast_asio/tests/TestPeerLogic.cpp>
#include <modules/beast_asio/tests/TestPeerLogicProxyClient.cpp>
#include <modules/beast_asio/tests/TestPeerLogicSyncServer.cpp>
#include <modules/beast_asio/tests/TestPeerLogicSyncClient.cpp>
#include <modules/beast_asio/tests/TestPeerLogicAsyncServer.cpp>
#include <modules/beast_asio/tests/TestPeerLogicAsyncClient.cpp>
#include <modules/beast_asio/tests/TestPeerUnitTests.cpp>

#include <modules/beast_asio/http/HTTPParser.cpp>
#include <modules/beast_asio/http/HTTPRequestParser.cpp>
#include <modules/beast_asio/http/HTTPResponseParser.cpp>
