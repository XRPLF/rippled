//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_IMPL_STATIC_BUFFER_IPP
#define BEAST_IMPL_STATIC_BUFFER_IPP

#include <beast/core/detail/type_traits.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <cstring>
#include <iterator>
#include <stdexcept>

namespace beast {

/*  Memory is laid out thusly:

    begin_ ..|.. in_ ..|.. out_ ..|.. last_ ..|.. end_
*/

inline
auto
static_buffer::
data() const ->
    const_buffers_type
{
    return {in_, dist(in_, out_)};
}

inline
auto
static_buffer::
prepare(std::size_t n) ->
    mutable_buffers_type
{
    return prepare_impl(n);
}

inline
void
static_buffer::
reset(void* p, std::size_t n)
{
    reset_impl(p, n);
}

template<class>
void
static_buffer::
reset_impl(void* p, std::size_t n)
{
    begin_ =
        reinterpret_cast<char*>(p);
    in_ = begin_;
    out_ = begin_;
    last_ = begin_;
    end_ = begin_ + n;
}

template<class>
auto
static_buffer::
prepare_impl(std::size_t n) ->
    mutable_buffers_type
{
    if(n <= dist(out_, end_))
    {
        last_ = out_ + n;
        return {out_, n};
    }
    auto const len = size();
    if(n > capacity() - len)
        BOOST_THROW_EXCEPTION(std::length_error{
            "buffer overflow"});
    if(len > 0)
        std::memmove(begin_, in_, len);
    in_ = begin_;
    out_ = in_ + len;
    last_ = out_ + n;
    return {out_, n};
}

template<class>
void
static_buffer::
consume_impl(std::size_t n)
{
    if(n >= size())
    {
        in_ = begin_;
        out_ = in_;
        return;
    }
    in_ += n;
}

//------------------------------------------------------------------------------

template<std::size_t N>
static_buffer_n<N>::
static_buffer_n(static_buffer_n const& other)
    : static_buffer(buf_, N)
{
    using boost::asio::buffer_copy;
    this->commit(buffer_copy(
        this->prepare(other.size()), other.data()));
}

template<std::size_t N>
auto
static_buffer_n<N>::
operator=(static_buffer_n const& other) ->
    static_buffer_n<N>&
{
    using boost::asio::buffer_copy;
    this->consume(this->size());
    this->commit(buffer_copy(
        this->prepare(other.size()), other.data()));
    return *this;
}

} // beast

#endif
