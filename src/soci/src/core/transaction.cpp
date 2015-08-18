//
// Copyright (C) 2004-2008 Maciej Sobczak
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SOURCE
#include "soci/transaction.h"
#include "soci/error.h"

using namespace soci;

transaction::transaction(session& sql)
    : handled_(false), sql_(sql)
{
    sql_.begin();
}

transaction::~transaction()
{
    if (handled_ == false)
    {
        try
        {
            rollback();
        }
        catch (...)
        {}
    }
}

void transaction::commit()
{
    if (handled_)
    {
        throw soci_error("The transaction object cannot be handled twice.");
    }

    sql_.commit();
    handled_ = true;
}

void transaction::rollback()
{
    if (handled_)
    {
        throw soci_error("The transaction object cannot be handled twice.");
    }

    sql_.rollback();
    handled_ = true;
}
