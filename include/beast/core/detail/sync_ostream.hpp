//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_SYNC_OSTREAM_HPP
#define BEAST_DETAIL_SYNC_OSTREAM_HPP

#include <beast/core/buffer_concepts.hpp>
#include <beast/core/error.hpp>
#include <boost/asio/buffer.hpp>
#include <ostream>

namespace beast {
namespace detail {

/** A SyncWriteStream which outputs to a `std::ostream`

    Objects of this type meet the requirements of @b SyncWriteStream.
*/
class sync_ostream
{
    std::ostream& os_;

public:
    /** Construct the stream.

        @param os The associated `std::ostream`. All buffers
        written will be passed to the associated output stream.
    */
    sync_ostream(std::ostream& os)
        : os_(os)
    {
    }

    template<class ConstBufferSequence>
    std::size_t
    write_some(ConstBufferSequence const& buffers);

    template<class ConstBufferSequence>
    std::size_t
    write_some(ConstBufferSequence const& buffers,
        error_code& ec);
};

template<class ConstBufferSequence>
std::size_t
sync_ostream::
write_some(ConstBufferSequence const& buffers)
{
    static_assert(
        is_ConstBufferSequence<ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    error_code ec;
    auto const n = write_some(buffers, ec);
    if(ec)
        throw system_error{ec};
    return n;
}

template<class ConstBufferSequence>
std::size_t
sync_ostream::
write_some(ConstBufferSequence const& buffers,
    error_code& ec)
{
    static_assert(
        is_ConstBufferSequence<ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    std::size_t n = 0;
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    for(auto const& buffer : buffers)
    {
        os_.write(buffer_cast<char const*>(buffer),
            buffer_size(buffer));
        if(os_.fail())
        {
            ec = errc::make_error_code(
                errc::no_stream_resources);
            break;
        }
        n += buffer_size(buffer);
    }
    return n;
}

} // detail
} // beast

#endif
