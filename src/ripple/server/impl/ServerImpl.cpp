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

#include <BeastConfig.h>
#include <ripple/basics/contract.h>
#include <ripple/server/impl/ServerImpl.h>
#include <ripple/server/impl/BaseHTTPPeer.h>
#include <boost/chrono/chrono_io.hpp>
#include <boost/utility/in_place_factory.hpp>
#include <cassert>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <string>
#include <stdio.h>
#include <time.h>

namespace ripple {

ServerImpl::ServerImpl (Handler& handler,
        boost::asio::io_service& io_service, beast::Journal journal)
    : handler_(handler)
    , j_(journal)
    , io_service_(io_service)
    , strand_(io_service_)
    , work_(io_service_)
{
}

ServerImpl::~ServerImpl()
{
    // Handler::onStopped will not be called
    work_ = boost::none;
    ios_.close();
    ios_.join();
}

void
ServerImpl::ports (std::vector<Port> const& ports)
{
    if (closed())
        Throw<std::logic_error> ("ports() on closed Server");
    ports_.reserve(ports.size());
    for(auto const& port : ports)
    {
        if (! port.websockets())
        {
            ports_.push_back(port);
            if(auto sp = ios_.emplace<Door>(handler_,
                io_service_, ports_.back(), j_))
            {
                list_.push_back(sp);
                sp->run();
            }
        }
    }
}

void
ServerImpl::close()
{
    ios_.close(
    [&]
    {
        work_ = boost::none;
        handler_.onStopped(*this);
    });
}

bool
ServerImpl::closed()
{
    return ios_.closed();
}

//--------------------------------------------------------------------------

std::unique_ptr<Server>
make_Server (Handler& handler,
    boost::asio::io_service& io_service, beast::Journal journal)
{
    return std::make_unique<ServerImpl>(handler, io_service, journal);
}

} // ripple
