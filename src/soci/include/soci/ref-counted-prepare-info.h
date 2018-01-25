//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_REF_COUNTED_PREPARE_INFO_INCLUDED
#define SOCI_REF_COUNTED_PREPARE_INFO_INCLUDED

#include "soci/bind-values.h"
#include "soci/ref-counted-statement.h"
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
    {}

    void exchange(use_type_ptr const& u) { uses_.exchange(u); }

    template <typename T, typename Indicator>
    void exchange(use_container<T, Indicator> const &uc)
    { uses_.exchange(uc); }

    void exchange(into_type_ptr const& i) { intos_.exchange(i); }

    template <typename T, typename Indicator>
    void exchange(into_container<T, Indicator> const &ic)
    { intos_.exchange(ic); }

    void final_action() SOCI_OVERRIDE;

private:
    friend class statement_impl;
    friend class procedure_impl;

    into_type_vector intos_;
    use_type_vector  uses_;

    std::string get_query() const;
};

} // namespace details

} // namespace soci

#endif
