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

#ifndef BEAST_STREAMHANDSHAKEDETECTORTYPE_H_INCLUDED
#define BEAST_STREAMHANDSHAKEDETECTORTYPE_H_INCLUDED

/** Wraps a HandshakeDetector and does the work on the Socket for you.
*/
template <class Detector>
class StreamHandshakeDetectorType
{
protected:
    typedef boost::system::error_code error_code;
    typedef StreamHandshakeDetectorType <Detector> This;

public:
    typedef typename Detector::arg_type arg_type;

    explicit StreamHandshakeDetectorType (arg_type const& arg = arg_type ())
    {
    }

    template <typename HandshakeHandler>
    void async_handshake (Socket& socket, BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler)
    {
#if 0
        std::size_t const bytes = m_detector.needed ();
#if 1
        boost::asio::async_read (socket, m_buffer.prepare (bytes), boost::bind (
            &This::on_read <typename HandshakeHandler>, this, &socket,
            handler,
            boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
#else
        boost::asio::async_read (socket, m_buffer.prepare (bytes), boost::bind (
            &This::on_read2, this,
            boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
#endif
#endif
    }

protected:
    template <typename HandshakeHandler>
    void on_read (Socket* socket, BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler,
        error_code const& ec, std::size_t bytes_transferred)
    {
        m_buffer.commit (bytes_transferred);

        if (m_detector.analyze (m_buffer.data ()))
        {
            if (m_detector.success ())
            {
                //socket->async_handshake (Socket::server, m_buffer.data (), handler);
            }
        }
    }

private:
    Detector m_detector;
    boost::asio::streambuf m_buffer;
};

#endif
