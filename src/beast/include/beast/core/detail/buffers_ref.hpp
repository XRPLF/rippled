//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_BUFFERS_REF_HPP
#define BEAST_DETAIL_BUFFERS_REF_HPP

#include <beast/core/type_traits.hpp>

namespace beast {
namespace detail {

// A very lightweight reference to a buffer sequence
template<class BufferSequence>
class buffers_ref
{
    BufferSequence const& buffers_;

public:
    using value_type =
        typename BufferSequence::value_type;

    using const_iterator =
        typename BufferSequence::const_iterator;

    buffers_ref(buffers_ref const&) = default;

    explicit
    buffers_ref(BufferSequence const& buffers)
        : buffers_(buffers)
    {
    }

    const_iterator
    begin() const
    {
        return buffers_.begin();
    }

    const_iterator
    end() const
    {
        return buffers_.end();
    }
};

// Return a reference to a buffer sequence
template<class BufferSequence>
buffers_ref<BufferSequence>
make_buffers_ref(BufferSequence const& buffers)
{
    return buffers_ref<BufferSequence>(buffers);
}

} // detail
} // beast

#endif
