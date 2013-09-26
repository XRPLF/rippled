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


AsyncService::AsyncService (char const* name, Stoppable& parent)
    : Stoppable (name, parent)
{
}

AsyncService::~AsyncService ()
{
    // If this goes off it means the
    // AsyncService API contract was violated.
    //
    bassert (m_pendingIo.get() == 0);
}

void AsyncService::serviceCountIoPending ()
{
    ++m_pendingIo;
}

bool AsyncService::serviceCountIoComplete (boost::system::error_code const& ec)
{
    // If this goes off, the count is unbalanced.
    bassert (m_pendingIo.get() > 0);

    --m_pendingIo;

    if (! ec || ec == boost::asio::error::operation_aborted)
        return true;

    return false;
}

void AsyncService::onServiceIoComplete ()
{
    //stopped();
}
