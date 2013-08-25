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

#ifndef BEAST_ASIO_H_INCLUDED
#define BEAST_ASIO_H_INCLUDED

//------------------------------------------------------------------------------

/*  If you fail to make sure that all your compile units are building Beast with
    the same set of option flags, then there's a risk that different compile
    units will treat the classes as having different memory layouts, leading to
    very nasty memory corruption errors when they all get linked together.
    That's why it's best to always include the BeastConfig.h file before any
    beast headers.
*/
#ifndef BEAST_BEASTCONFIG_H_INCLUDED
# ifdef _MSC_VER
#  pragma message ("Have you included your BeastConfig.h file before including the Beast headers?")
# else
#  warning "Have you included your BeastConfig.h file before including the Beast headers?"
# endif
#endif

// Must come before boost includes to fix the bost placeholders.
#include "../beast_core/beast_core.h"

// This module requires boost and possibly OpenSSL
#include "system/beast_BoostIncludes.h"

// Checking overrides replaces unimplemented stubs with pure virtuals
#ifndef BEAST_COMPILER_CHECKS_SOCKET_OVERRIDES
# define BEAST_COMPILER_CHECKS_SOCKET_OVERRIDES 0
#endif
#if BEAST_COMPILER_CHECKS_SOCKET_OVERRIDES
# define BEAST_SOCKET_VIRTUAL = 0
#else
# define BEAST_SOCKET_VIRTUAL
#endif

// A potentially dangerous but powerful feature which
// might need to be turned off to see if it fixes anything.
//
#ifndef BEAST_USE_HANDLER_ALLOCATIONS
# define BEAST_USE_HANDLER_ALLOCATIONS 1
#endif

namespace beast
{

// Order matters
  #include "async/beast_SharedHandler.h"
 #include "async/beast_SharedHandlerType.h"
 #include "async/beast_SharedHandlerPtr.h"
 #include "async/beast_ComposedAsyncOperation.h"
#include "async/beast_SharedHandlerAllocator.h"

#include "basics/beast_BufferType.h"
#include "basics/beast_FixedInputBuffer.h"
#include "basics/beast_PeerRole.h"
#include "basics/SSLContext.h"

   #include "sockets/beast_SocketBase.h"
  #include "sockets/beast_Socket.h"
 #include "sockets/beast_SocketWrapper.h"
#include "sockets/SocketWrapperStrand.h"

  #include "handshake/beast_InputParser.h"
 #include "handshake/beast_HandshakeDetectLogic.h"
#include "handshake/beast_HandshakeDetectLogicPROXY.h"
#include "handshake/beast_HandshakeDetectLogicSSL2.h"
#include "handshake/beast_HandshakeDetectLogicSSL3.h"
#include "handshake/beast_HandshakeDetector.h"
#include "handshake/beast_PrefilledReadStream.h"

#include "tests/beast_TestPeerBasics.h"
#include "tests/beast_TestPeer.h"
#include "tests/beast_TestPeerDetails.h"
#include "tests/beast_TestPeerLogic.h"
#include "tests/beast_TestPeerLogicSyncServer.h"
#include "tests/beast_TestPeerLogicSyncClient.h"
#include "tests/beast_TestPeerLogicProxyClient.h"
#include "tests/beast_TestPeerLogicAsyncServer.h"
#include "tests/beast_TestPeerLogicAsyncClient.h"
#include "tests/beast_TestPeerType.h"
#include "tests/beast_TestPeerDetailsTcp.h"
#include "tests/beast_PeerTest.h"

}

#endif

