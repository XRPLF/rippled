//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SOURCE
#include "use-type.h"
#include "statement.h"

using namespace soci;
using namespace soci::details;

standard_use_type::~standard_use_type()
{
    delete backEnd_;
}

void standard_use_type::bind(statement_impl & st, int & position)
{
    if (backEnd_ == NULL)
    {
        backEnd_ = st.make_use_type_backend();
    }
    if (name_.empty())
    {
        backEnd_->bind_by_pos(position, data_, type_, readOnly_);
    }
    else
    {
        backEnd_->bind_by_name(name_, data_, type_, readOnly_);
    }
}

void standard_use_type::pre_use()   
{
    // Handle IN direction of parameters of SQL statements and procedures
    convert_to_base();
    backEnd_->pre_use(ind_);
}

void standard_use_type::post_use(bool gotData)
{
    // Handle OUT direction of IN/OUT parameters of stored procedures
    backEnd_->post_use(gotData, ind_);
    convert_from_base();

    // IMPORTANT:
    // This treatment of input ("use") parameter as output data sink may be 
    // confusing, but it is necessary to store OUT data back in the same 
    // object as IN, of IN/OUT parameter.
    // As there is no symmetry for IN/OUT in SQL and there are no OUT/IN
    // we do not perform convert_to_base() for output ("into") parameter.
    // See conversion_use_type<T>::convert_from_base() for more details.
}

void standard_use_type::clean_up()
{
    if (backEnd_ != NULL)
    {
        backEnd_->clean_up();
    }
}

vector_use_type::~vector_use_type()
{
    delete backEnd_;
}

void vector_use_type::bind(statement_impl & st, int & position)
{
    if (backEnd_ == NULL)
    {
        backEnd_ = st.make_vector_use_type_backend();
    }
    if (name_.empty())
    {
        backEnd_->bind_by_pos(position, data_, type_);
    }
    else
    {
        backEnd_->bind_by_name(name_, data_, type_);
    }
}

void vector_use_type::pre_use()
{
    convert_to_base();

    backEnd_->pre_use(ind_ ? &ind_->at(0) : NULL);
}

std::size_t vector_use_type::size() const
{
    return backEnd_->size();
}

void vector_use_type::clean_up()
{
    if (backEnd_ != NULL)
    {
        backEnd_->clean_up();
    }
}
