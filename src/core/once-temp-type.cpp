//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SOURCE
#include "once-temp-type.h"
#include "ref-counted-statement.h"
#include "session.h"

using namespace soci;
using namespace soci::details;

once_temp_type::once_temp_type(session & s)
    : rcst_(new ref_counted_statement(s))
{
    // this is the beginning of new query
    s.get_query_stream().str("");
}

once_temp_type::once_temp_type(once_temp_type const & o)
    :rcst_(o.rcst_)
{
    rcst_->inc_ref();
}

once_temp_type & once_temp_type::operator=(once_temp_type const & o)
{
    o.rcst_->inc_ref();
    rcst_->dec_ref();
    rcst_ = o.rcst_;

    return *this;
}

once_temp_type::~once_temp_type() SOCI_ONCE_TEMP_TYPE_NOEXCEPT
{
    rcst_->dec_ref();
}

once_temp_type & once_temp_type::operator,(into_type_ptr const & i)
{
    rcst_->exchange(i);
    return *this;
}

once_temp_type & once_temp_type::operator,(use_type_ptr const & u)
{
    rcst_->exchange(u);
    return *this;
}
