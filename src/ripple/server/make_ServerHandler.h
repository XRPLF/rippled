//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_SERVER_MAKE_SERVERHANDLER_H_INCLUDED
#define RIPPLE_SERVER_MAKE_SERVERHANDLER_H_INCLUDED

#include <ripple/core/JobQueue.h>
#include <ripple/resource/ResourceManager.h>
#include <ripple/server/ServerHandler.h>
#include <beast/threads/Stoppable.h>
#include <boost/asio/io_service.hpp>
#include <memory>

namespace ripple {

class NetworkOPs;

std::unique_ptr <ServerHandler>
make_ServerHandler (Application& app, beast::Stoppable& parent, boost::asio::io_service&,
    JobQueue&, NetworkOPs&, Resource::Manager&,
        CollectorManager& cm);

} // ripple

#endif
