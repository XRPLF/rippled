//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SOURCE
#include "values.h"
#include "row.h"

#include <cstddef>
#include <map>
#include <sstream>
#include <string>

using namespace soci;
using namespace soci::details;

indicator values::get_indicator(std::size_t pos) const
{
    if (row_)
    {
        return row_->get_indicator(pos);
    }
    else
    {
        return *indicators_[pos];
    }
}

indicator values::get_indicator(std::string const& name) const
{
    if (row_)
    {
        return row_->get_indicator(name);
    }
    else
    {
        std::map<std::string, std::size_t>::const_iterator it = index_.find(name);
        if (it == index_.end())
        {
            std::ostringstream msg;
            msg << "Column '" << name << "' not found";
            throw soci_error(msg.str());
        }
        return *indicators_[it->second];
    }
}

column_properties const& values::get_properties(std::size_t pos) const
{
    if (row_)
    {
        return row_->get_properties(pos);
    }

    throw soci_error("Rowset is empty");
}

column_properties const& values::get_properties(std::string const& name) const
{
    if (row_)
    {
        return row_->get_properties(name);
    }

    throw soci_error("Rowset is empty");
}
