//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SOURCE
#include "ref-counted-prepare-info.h"
#include "session.h"

using namespace soci;
using namespace soci::details;

void ref_counted_prepare_info::exchange(into_type_ptr const & i)
{
    intos_.push_back(i.get());
    i.release();
}

void ref_counted_prepare_info::exchange(use_type_ptr const & u)
{
    uses_.push_back(u.get());
    u.release();
}

void ref_counted_prepare_info::final_action()
{
    // deallocate all bind and define objects
    for (std::size_t i = intos_.size(); i > 0; --i)
    {
        delete intos_[i - 1];
        intos_.resize(i - 1);
    }

    for (std::size_t i = uses_.size(); i > 0; --i)
    {
        delete uses_[i - 1];
        uses_.resize(i - 1);
    }
}

std::string ref_counted_prepare_info::get_query() const
{
    return session_.get_query();
}
