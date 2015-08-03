//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SOURCE
#include "soci/procedure.h"
#include "soci/statement.h"
#include "soci/prepare-temp-type.h"

using namespace soci;
using namespace soci::details;

procedure_impl::procedure_impl(prepare_temp_type const & prep)
    : statement_impl(prep.get_prepare_info()->session_),
    refCount_(1)
{
    ref_counted_prepare_info * prepInfo = prep.get_prepare_info();

    // take all bind/define info
    intos_.swap(prepInfo->intos_);
    uses_.swap(prepInfo->uses_);

    // allocate handle
    alloc();

    // prepare the statement
    prepare(rewrite_for_procedure_call(prepInfo->get_query()));

    define_and_bind();
}
