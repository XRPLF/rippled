//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_IN_PLACE_INIT_HPP
#define BEAST_DETAIL_IN_PLACE_INIT_HPP

#include <boost/version.hpp>
#include <boost/optional/optional.hpp>

// Provide boost::in_place_init_t and boost::in_place_init
// for Boost versions earlier than 1.63.0.

#if BOOST_VERSION < 106300

namespace boost {

namespace optional_ns {
 
// a tag for in-place initialization of contained value
struct in_place_init_t
{
  struct init_tag{};
  explicit in_place_init_t(init_tag){}
};            
const in_place_init_t in_place_init ((in_place_init_t::init_tag()));
  
} // namespace optional_ns

using optional_ns::in_place_init_t;
using optional_ns::in_place_init;

}

#endif

#endif

