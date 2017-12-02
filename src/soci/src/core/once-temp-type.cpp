//
// Copyright (C) 2004-2016 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SOURCE
#include "soci/once-temp-type.h"
#include "soci/ref-counted-statement.h"
#include "soci/session.h"

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

once_temp_type::~once_temp_type() SOCI_NOEXCEPT_FALSE
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

ddl_type::ddl_type(session & s)
    : s_(&s), rcst_(new ref_counted_statement(s))
{
    // this is the beginning of new query
    s.get_query_stream().str("");
}

ddl_type::ddl_type(const ddl_type & d)
    :rcst_(d.rcst_)
{
    rcst_->inc_ref();
}

ddl_type & ddl_type::operator=(const ddl_type & d)
{
    d.rcst_->inc_ref();
    rcst_->dec_ref();
    rcst_ = d.rcst_;

    return *this;
}

ddl_type::~ddl_type() SOCI_NOEXCEPT_FALSE
{
    rcst_->dec_ref();
}

void ddl_type::create_table(const std::string & tableName)
{
    rcst_->accumulate(s_->get_backend()->create_table(tableName));
}

void ddl_type::add_column(const std::string & tableName,
    const std::string & columnName, data_type dt,
    int precision, int scale)
{
    rcst_->accumulate(s_->get_backend()->add_column(
            tableName, columnName, dt, precision, scale));
}

void ddl_type::alter_column(const std::string & tableName,
    const std::string & columnName, data_type dt,
    int precision, int scale)
{
    rcst_->accumulate(s_->get_backend()->alter_column(
            tableName, columnName, dt, precision, scale));
}

void ddl_type::drop_column(const std::string & tableName,
    const std::string & columnName)
{
    rcst_->accumulate(s_->get_backend()->drop_column(
            tableName, columnName));
}

ddl_type & ddl_type::column(const std::string & columnName, data_type dt,
    int precision, int scale)
{
    if (rcst_->get_need_comma())
    {
        rcst_->accumulate(", ");
    }

    rcst_->accumulate(columnName);
    rcst_->accumulate(" ");
    rcst_->accumulate(
        s_->get_backend()->create_column_type(dt, precision, scale));

    rcst_->set_need_comma(true);
        
    return *this;
}

ddl_type & ddl_type::unique(const std::string & name,
    const std::string & columnNames)
{
    if (rcst_->get_need_comma())
    {
        rcst_->accumulate(", ");
    }
        
    rcst_->accumulate(s_->get_backend()->constraint_unique(
            name, columnNames));

    rcst_->set_need_comma(true);
        
    return *this;
}

ddl_type & ddl_type::primary_key(const std::string & name,
    const std::string & columnNames)
{
    if (rcst_->get_need_comma())
    {
        rcst_->accumulate(", ");
    }
        
    rcst_->accumulate(s_->get_backend()->constraint_primary_key(
            name, columnNames));

    rcst_->set_need_comma(true);
        
    return *this;
}

ddl_type & ddl_type::foreign_key(const std::string & name,
    const std::string & columnNames,
    const std::string & refTableName,
    const std::string & refColumnNames)
{
    if (rcst_->get_need_comma())
    {
        rcst_->accumulate(", ");
    }
        
    rcst_->accumulate(s_->get_backend()->constraint_foreign_key(
            name, columnNames, refTableName, refColumnNames));

    rcst_->set_need_comma(true);
        
    return *this;
}

ddl_type & ddl_type::operator()(const std::string & arbitrarySql)
{
    rcst_->accumulate(" " + arbitrarySql);

    return *this;
}

void ddl_type::set_tail(const std::string & tail)
{
    rcst_->set_tail(tail);
}
