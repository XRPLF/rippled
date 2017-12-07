//
// Copyright (C) 2004-2016 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SOURCE
#include "soci/into-type.h"
#include "soci/statement.h"

using namespace soci;
using namespace soci::details;

standard_into_type::~standard_into_type()
{
    delete backEnd_;
}

void standard_into_type::define(statement_impl & st, int & position)
{
    if (backEnd_ == NULL)
    {
        backEnd_ = st.make_into_type_backend();
    }
    
    backEnd_->define_by_pos(position, data_, type_);
}

void standard_into_type::pre_exec(int num)
{
    backEnd_->pre_exec(num);
}

void standard_into_type::pre_fetch()
{
    backEnd_->pre_fetch();
}

void standard_into_type::post_fetch(bool gotData, bool calledFromFetch)
{
    backEnd_->post_fetch(gotData, calledFromFetch, ind_);

    if (gotData)
    {
        convert_from_base();
    }
}

void standard_into_type::clean_up()
{
    // backEnd_ might be NULL if IntoType<Row> was used
    if (backEnd_ != NULL)
    {
        backEnd_->clean_up();
    }
}

vector_into_type::~vector_into_type()
{
    delete backEnd_;
}

void vector_into_type::define(statement_impl & st, int & position)
{
    if (backEnd_ == NULL)
    {
        backEnd_ = st.make_vector_into_type_backend();
    }

    if (end_ != NULL)
    {
        backEnd_->define_by_pos_bulk(position, data_, type_, begin_, end_);
    }
    else
    {
        backEnd_->define_by_pos(position, data_, type_);
    }
}

void vector_into_type::pre_exec(int num)
{
    backEnd_->pre_exec(num);
}

void vector_into_type::pre_fetch()
{
    backEnd_->pre_fetch();
}

void vector_into_type::post_fetch(bool gotData, bool /* calledFromFetch */)
{
    if (indVec_ != NULL && indVec_->empty() == false)
    {
        backEnd_->post_fetch(gotData, &(*indVec_)[0]);
    }
    else
    {
        backEnd_->post_fetch(gotData, NULL);
    }

    if (gotData)
    {
        convert_from_base();
    }
}

void vector_into_type::resize(std::size_t sz)
{
    if (indVec_ != NULL && end_ == NULL)
    {
        indVec_->resize(sz);
    }

    backEnd_->resize(sz);
}

std::size_t vector_into_type::size() const
{
    return backEnd_->size();
}

void vector_into_type::clean_up()
{
    if (backEnd_ != NULL)
    {
        backEnd_->clean_up();
    }
}
