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
#include <modules/beast_core/beast_core.h>

// This module requires boost and possibly OpenSSL
#include <modules/beast_asio/system/BoostIncludes.h>

#include <beast/http/URL.h>
#include <beast/http/ParsedURL.h>

#include <beast/asio/IPAddressConversion.h>

// Order matters
#include <modules/beast_asio/async/AsyncObject.h>

#include <modules/beast_asio/basics/FixedInputBuffer.h>
#include <modules/beast_asio/basics/PeerRole.h>
#include <modules/beast_asio/basics/SSLContext.h>
#include <modules/beast_asio/basics/SharedArg.h>

#include <modules/beast_asio/http/HTTPVersion.h>
#include <modules/beast_asio/http/HTTPField.h>
#include <modules/beast_asio/http/HTTPHeaders.h>
#include <modules/beast_asio/http/HTTPMessage.h>
#include <modules/beast_asio/http/HTTPRequest.h>
#include <modules/beast_asio/http/HTTPResponse.h>

#include <modules/beast_asio/http/HTTPParser.h>
#include <modules/beast_asio/http/HTTPRequestParser.h>
#include <modules/beast_asio/http/HTTPResponseParser.h>

#include <modules/beast_asio/http/HTTPClientType.h>

#include <modules/beast_asio/protocol/InputParser.h>
#include <modules/beast_asio/protocol/HandshakeDetectLogic.h>
#include <modules/beast_asio/protocol/HandshakeDetectLogicPROXY.h>
#include <modules/beast_asio/protocol/HandshakeDetectLogicSSL2.h>
#include <modules/beast_asio/protocol/HandshakeDetectLogicSSL3.h>
#include <modules/beast_asio/protocol/HandshakeDetector.h>
#include <modules/beast_asio/protocol/PrefilledReadStream.h>

#include <modules/beast_asio/tests/TestPeerBasics.h>
#include <modules/beast_asio/tests/TestPeer.h>
#include <modules/beast_asio/tests/TestPeerDetails.h>
#include <modules/beast_asio/tests/TestPeerLogic.h>
#include <modules/beast_asio/tests/TestPeerLogicSyncServer.h>
#include <modules/beast_asio/tests/TestPeerLogicSyncClient.h>
#include <modules/beast_asio/tests/TestPeerLogicProxyClient.h>
#include <modules/beast_asio/tests/TestPeerLogicAsyncServer.h>
#include <modules/beast_asio/tests/TestPeerLogicAsyncClient.h>
#include <modules/beast_asio/tests/TestPeerType.h>
#include <modules/beast_asio/tests/TestPeerDetailsTcp.h>
#include <modules/beast_asio/tests/PeerTest.h>

#endif

