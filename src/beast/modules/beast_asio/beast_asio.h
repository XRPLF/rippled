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
#include "../beast_core/beast_core.h"

// This module requires boost and possibly OpenSSL
#include "system/BoostIncludes.h"

#include "../../beast/http/URL.h"
#include "../../beast/http/ParsedURL.h"

#include "../../beast/asio/IPAddressConversion.h"

// Order matters
#include "async/AsyncObject.h"

#include "basics/FixedInputBuffer.h"
#include "basics/PeerRole.h"
#include "basics/SSLContext.h"
#include "basics/SharedArg.h"

#   include "http/HTTPVersion.h"
#   include "http/HTTPField.h"
#   include "http/HTTPHeaders.h"
#  include "http/HTTPMessage.h"
# include "http/HTTPRequest.h"
# include "http/HTTPResponse.h"

# include "http/HTTPParser.h"
#include "http/HTTPRequestParser.h"
#include "http/HTTPResponseParser.h"

#include "http/HTTPClientType.h"

#  include "protocol/InputParser.h"
# include "protocol/HandshakeDetectLogic.h"
#include "protocol/HandshakeDetectLogicPROXY.h"
#include "protocol/HandshakeDetectLogicSSL2.h"
#include "protocol/HandshakeDetectLogicSSL3.h"
#include "protocol/HandshakeDetector.h"
#include "protocol/PrefilledReadStream.h"

#include "tests/TestPeerBasics.h"
#include "tests/TestPeer.h"
#include "tests/TestPeerDetails.h"
#include "tests/TestPeerLogic.h"
#include "tests/TestPeerLogicSyncServer.h"
#include "tests/TestPeerLogicSyncClient.h"
#include "tests/TestPeerLogicProxyClient.h"
#include "tests/TestPeerLogicAsyncServer.h"
#include "tests/TestPeerLogicAsyncClient.h"
#include "tests/TestPeerType.h"
#include "tests/TestPeerDetailsTcp.h"
#include "tests/PeerTest.h"

#endif

