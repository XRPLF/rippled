//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SOURCE
#include "soci/prepare-temp-type.h"
#include "soci/ref-counted-prepare-info.h"
#include "soci/session.h"

using namespace soci;
using namespace soci::details;

prepare_temp_type::prepare_temp_type(session & s)
    : rcpi_(new ref_counted_prepare_info(s))
{
    // this is the beginning of new query
    s.get_query_stream().str("");
}

prepare_temp_type::prepare_temp_type(prepare_temp_type const & o)
    :rcpi_(o.rcpi_)
{
    rcpi_->inc_ref();
}

prepare_temp_type & prepare_temp_type::operator=(prepare_temp_type const & o)
{
    o.rcpi_->inc_ref();
    rcpi_->dec_ref();
    rcpi_ = o.rcpi_;

    return *this;
}

prepare_temp_type::~prepare_temp_type()
{
    rcpi_->dec_ref();
}

prepare_temp_type & prepare_temp_type::operator,(into_type_ptr const & i)
{
    rcpi_->exchange(i);
    return *this;
}
