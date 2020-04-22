//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_MISC_DETAIL_WORKPLAIN_H_INCLUDED
#define RIPPLE_APP_MISC_DETAIL_WORKPLAIN_H_INCLUDED

#include <ripple/app/misc/detail/WorkBase.h>

namespace ripple {

namespace detail {

// Work over TCP/IP
class WorkPlain : public WorkBase<WorkPlain>,
                  public std::enable_shared_from_this<WorkPlain>
{
    friend class WorkBase<WorkPlain>;

public:
    WorkPlain(
        std::string const& host,
        std::string const& path,
        std::string const& port,
        boost::asio::io_service& ios,
        callback_type cb);
    ~WorkPlain() = default;

private:
    void
    onConnect(error_code const& ec);

    socket_type&
    stream()
    {
        return socket_;
    }
};

//------------------------------------------------------------------------------

WorkPlain::WorkPlain(
    std::string const& host,
    std::string const& path,
    std::string const& port,
    boost::asio::io_service& ios,
    callback_type cb)
    : WorkBase(host, path, port, ios, cb)
{
}

void
WorkPlain::onConnect(error_code const& ec)
{
    if (ec)
        return fail(ec);

    onStart();
}

}  // namespace detail

}  // namespace ripple

#endif
