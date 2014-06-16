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

#ifndef BEAST_ASIO_MODULE_H_INCLUDED
#define BEAST_ASIO_MODULE_H_INCLUDED

// Must come before boost includes to fix the bost placeholders.
#include <beast/module/core/core.h>

// This module requires boost and possibly OpenSSL
#include <beast/module/asio/system/BoostIncludes.h>

#include <beast/http/URL.h>
#include <beast/http/ParsedURL.h>

#include <beast/asio/IPAddressConversion.h>

// Order matters
#include <beast/module/asio/async/AsyncObject.h>

#include <beast/module/asio/basics/FixedInputBuffer.h>
#include <beast/module/asio/basics/PeerRole.h>
#include <beast/module/asio/basics/SSLContext.h>
#include <beast/module/asio/basics/SharedArg.h>

#include <beast/module/asio/http/HTTPVersion.h>
#include <beast/module/asio/http/HTTPField.h>
#include <beast/module/asio/http/HTTPHeaders.h>
#include <beast/module/asio/http/HTTPMessage.h>
#include <beast/module/asio/http/HTTPRequest.h>
#include <beast/module/asio/http/HTTPResponse.h>

#include <beast/module/asio/http/HTTPParser.h>
#include <beast/module/asio/http/HTTPRequestParser.h>
#include <beast/module/asio/http/HTTPResponseParser.h>

#include <beast/module/asio/http/HTTPClientType.h>

#include <beast/module/asio/protocol/InputParser.h>
#include <beast/module/asio/protocol/HandshakeDetectLogic.h>
#include <beast/module/asio/protocol/HandshakeDetectLogicPROXY.h>
#include <beast/module/asio/protocol/HandshakeDetectLogicSSL2.h>
#include <beast/module/asio/protocol/HandshakeDetectLogicSSL3.h>
#include <beast/module/asio/protocol/HandshakeDetector.h>
#include <beast/module/asio/protocol/PrefilledReadStream.h>

#include <beast/module/asio/tests/TestPeerBasics.h>
#include <beast/module/asio/tests/TestPeer.h>
#include <beast/module/asio/tests/TestPeerDetails.h>
#include <beast/module/asio/tests/TestPeerLogic.h>
#include <beast/module/asio/tests/TestPeerLogicSyncServer.h>
#include <beast/module/asio/tests/TestPeerLogicSyncClient.h>
#include <beast/module/asio/tests/TestPeerLogicProxyClient.h>
#include <beast/module/asio/tests/TestPeerLogicAsyncServer.h>
#include <beast/module/asio/tests/TestPeerLogicAsyncClient.h>
#include <beast/module/asio/tests/TestPeerType.h>
#include <beast/module/asio/tests/TestPeerDetailsTcp.h>
#include <beast/module/asio/tests/PeerTest.h>

#endif

