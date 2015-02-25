//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_REF_COUNTED_PREPARE_INFO_INCLUDED
#define SOCI_REF_COUNTED_PREPARE_INFO_INCLUDED

#include "ref-counted-statement.h"
// std
#include <string>
#include <vector>

namespace soci
{

class session;

namespace details
{

class procedure_impl;
class statement_impl;
class into_type_base;

// this class conveys only the statement text and the bind/define info
// it exists only to be passed to statement's constructor
class ref_counted_prepare_info : public ref_counted_statement_base
{
public:
    ref_counted_prepare_info(session& s)
        : ref_counted_statement_base(s)
        , session_(s)
    {}

    void exchange(into_type_ptr const& i);
    void exchange(use_type_ptr const& u);

    void final_action();

private:
    friend class statement_impl;
    friend class procedure_impl;

    session& session_;

    std::vector<into_type_base*> intos_;
    std::vector<use_type_base*> uses_;

    std::string get_query() const;
};

} // namespace details

} // namespace soci

#endif
