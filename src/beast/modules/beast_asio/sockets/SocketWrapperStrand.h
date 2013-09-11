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

#ifndef BEAST_ASIO_SOCKETS_SOCKETWRAPPERSTRAND_H_INCLUDED
#define BEAST_ASIO_SOCKETS_SOCKETWRAPPERSTRAND_H_INCLUDED

/** Wraps the async I/O of a SocketWrapper with an io_service::strand
    To use this in a chain of wrappers, customize the Base type.
*/
template <typename Object, typename Base = SocketWrapper <Object> >
class SocketWrapperStrand
    : public Base
{
public:
    template <typename Arg>
    SocketWrapperStrand (Arg& arg)
        : Base (arg)
        , m_strand (this->get_io_service ())
    {
    }

    template <typename Arg1, typename Arg2>
    SocketWrapperStrand (Arg1& arg1, Arg2& arg2)
        : Base (arg1, arg2)
        , m_strand (this->get_io_service ())
    {
    }

    //--------------------------------------------------------------------------
    //
    // basic_stream_socket
    //

    void async_read_some (MutableBuffers const& buffers, SharedHandlerPtr handler)
    {
        this->Base::async_read_some (buffers,
            newReadHandler (m_strand.wrap (handler)));
    }

    void async_write_some (MutableBuffers const& buffers, SharedHandlerPtr handler)
    {
        this->Base::async_write_some (buffers,
            newWriteHandler (m_strand.wrap (handler)));
    }

protected:
    boost::asio::io_service::strand m_strand;
};

#endif
