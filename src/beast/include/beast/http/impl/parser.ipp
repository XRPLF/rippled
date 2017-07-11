//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_IMPL_PARSER_IPP
#define BEAST_HTTP_IMPL_PARSER_IPP

#include <boost/throw_exception.hpp>
#include <stdexcept>

namespace beast {
namespace http {

template<bool isRequest, class Body, class Allocator>
parser<isRequest, Body, Allocator>::
parser()
    : wr_(m_)
{
}

template<bool isRequest, class Body, class Allocator>
template<class Arg1, class... ArgN, class>
parser<isRequest, Body, Allocator>::
parser(Arg1&& arg1, ArgN&&... argn)
    : m_(std::forward<Arg1>(arg1),
        std::forward<ArgN>(argn)...)
    , wr_(m_)
{
}

template<bool isRequest, class Body, class Allocator>
template<class OtherBody, class... Args, class>
parser<isRequest, Body, Allocator>::
parser(parser<isRequest, OtherBody, Allocator>&& p,
        Args&&... args)
    : base_type(std::move(p))
    , m_(p.release(), std::forward<Args>(args)...)
    , wr_(m_)
{
    if(wr_inited_)
        BOOST_THROW_EXCEPTION(std::invalid_argument{
            "moved-from parser has a body"});
}

} // http
} // beast

#endif
