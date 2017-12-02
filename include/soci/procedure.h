//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_PROCEDURE_H_INCLUDED
#define SOCI_PROCEDURE_H_INCLUDED

#include "soci/statement.h"

namespace soci
{

namespace details
{

class SOCI_DECL procedure_impl : public statement_impl
{
public:
    procedure_impl(session & s) : statement_impl(s), refCount_(1) {}
    procedure_impl(prepare_temp_type const & prep);

    void inc_ref() { ++refCount_; }
    void dec_ref()
    {
        if (--refCount_ == 0)
        {
            delete this;
        }
    }

private:
    int refCount_;
};

} // namespace details

class SOCI_DECL procedure
{
public:
    // this is a conversion constructor
    procedure(details::prepare_temp_type const & prep)
        : impl_(new details::procedure_impl(prep)), gotData_(false) {}

    ~procedure() { impl_->dec_ref(); }

    // copy is supported here
    procedure(procedure const & other)
        : impl_(other.impl_), gotData_(other.gotData_)
    {
        impl_->inc_ref();
    }
    void operator=(procedure const & other)
    {
        other.impl_->inc_ref();
        impl_->dec_ref();
        impl_ = other.impl_;
        gotData_ = other.gotData_;
    }

    // forwarders to procedure_impl
    // (or rather to its base interface from statement_impl)

    bool execute(bool withDataExchange = false)
    {
        gotData_ = impl_->execute(withDataExchange);
        return gotData_;
    }

    bool fetch()
    {
        gotData_ = impl_->fetch();
        return gotData_;
    }

    bool got_data() const
    {
        return gotData_;
    }

private:
    details::procedure_impl * impl_;
    bool gotData_;
};

} // namespace soci

#endif
