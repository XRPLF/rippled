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

#ifndef RIPPLE_APP_BASICAPP_H_INCLUDED
#define RIPPLE_APP_BASICAPP_H_INCLUDED

#include <boost/asio/io_service.hpp>
#include <boost/optional.hpp>
#include <thread>
#include <vector>

// This is so that the io_service can outlive all the children
class BasicApp
{
private:
    boost::optional<boost::asio::io_service::work> work_;
    std::vector<std::thread> threads_;
    boost::asio::io_service io_service_;

public:
    BasicApp(std::size_t numberOfThreads);
    ~BasicApp();

    boost::asio::io_service&
    get_io_service()
    {
        return io_service_;
    }
};

#endif
