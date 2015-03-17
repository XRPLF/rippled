//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_INTO_ROW_H_INCLUDED
#define SOCI_INTO_ROW_H_INCLUDED

#include "into-type.h"
#include "exchange-traits.h"
#include "row.h"
#include "statement.h"
// std
#include <cstddef>

namespace soci
{

namespace details
{

// Support selecting into a row for dynamic queries

template <>
class into_type<row>
    : public into_type_base // bypass the standard_into_type
{
public:
    into_type(row & r) : r_(r) {}
    into_type(row & r, indicator &) : r_(r) {}

private:
    // special handling for Row
    virtual void define(statement_impl & st, int & /* position */)
    {
        st.set_row(&r_);

        // actual row description is performed
        // as part of the statement execute
    }

    virtual void pre_fetch() {}
    virtual void post_fetch(bool gotData, bool /* calledFromFetch */)
    {
        r_.reset_get_counter();

        if (gotData)
        {
            // this is used only to re-dispatch to derived class, if any
            // (the derived class might be generated automatically by
            // user conversions)
            convert_from_base();
        }
    }

    virtual void clean_up() {}

    virtual std::size_t size() const { return 1; }

    virtual void convert_from_base() {}

    row & r_;
};

template <>
struct exchange_traits<row>
{
    typedef basic_type_tag type_family;
};

} // namespace details

} // namespace soci

#endif
