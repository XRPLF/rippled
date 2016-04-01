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

#ifndef BEAST_ASIO_TEMP_BUFFER_H_INCLUDED
#define BEAST_ASIO_TEMP_BUFFER_H_INCLUDED

#include <boost/asio/buffer.hpp>
#include <boost/asio/detail/handler_alloc_helpers.hpp>
#include <cstdlib>
#include <memory>
#include <utility>

namespace beast {

template<class Handler>
class temp_buffer
{
    Handler& h_;
    std::size_t n_ = 0;
    std::uint8_t* p_ = nullptr;

public:
    explicit
    temp_buffer(Handler& h)
        : h_(h)
    {
    }

    ~temp_buffer()
    {
        if(p_)
            dealloc();
    }

    operator
    boost::asio::const_buffer() const
    {
        return boost::asio::const_buffer{p_, n_};
    }

    operator
    boost::asio::mutable_buffer() const
    {
        return boost::asio::mutable_buffer{p_, n_};
    }

    std::uint8_t*
    data() const
    {
        return p_;
    }

    std::size_t
    size()
    {
        return n_;
    }

    boost::asio::mutable_buffers_1
    buffers() const
    {
        return boost::asio::mutable_buffers_1{
            p_, n_};
    }

    void
    alloc(std::size_t size)
    {
        if(n_ != size)
        {
            if(p_)
                dealloc();
            n_ = size;
            if(n_ > 0)
                p_ = reinterpret_cast<std::uint8_t*>(
                    boost_asio_handler_alloc_helpers::
                        allocate(n_, h_));
        }
    }

    void
    dealloc()
    {
        boost_asio_handler_alloc_helpers::
            deallocate(p_, n_, h_);
        p_ = nullptr;
        n_ = 0;
    }
};

} // beast

#endif
