//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_BOOST_FUSION_H_INCLUDED
#define SOCI_BOOST_FUSION_H_INCLUDED

#ifndef SOCI_MAX_FUSION_SEQUENCE_LENGTH
#define SOCI_MAX_FUSION_SEQUENCE_LENGTH 10
#endif

#include "values.h"
#include "type-conversion-traits.h"
// boost
#include <boost/fusion/container/vector.hpp>
#include <boost/fusion/sequence/intrinsic/at.hpp>
#include <boost/fusion/sequence/intrinsic/size.hpp>
#include <boost/fusion/include/at.hpp>
#include <boost/fusion/support/is_sequence.hpp>
#include <boost/preprocessor/repetition/repeat.hpp>
#include <boost/preprocessor/repetition/repeat_from_to.hpp>
#include <boost/utility/enable_if.hpp>


#endif // SOCI_BOOST_FUSION_H_INCLUDED
