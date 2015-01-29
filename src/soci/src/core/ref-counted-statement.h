//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_REF_COUNTED_STATEMENT_H_INCLUDED
#define SOCI_REF_COUNTED_STATEMENT_H_INCLUDED

#include "statement.h"
#include "into-type.h"
#include "use-type.h"
// std
#include <sstream>

namespace soci
{

namespace details
{

// this class is a base for both "once" and "prepare" statements
class SOCI_DECL ref_counted_statement_base
{
public:
    ref_counted_statement_base(session& s);
    
    virtual ~ref_counted_statement_base() {}

    virtual void final_action() = 0;

    void inc_ref() { ++refCount_; }
    void dec_ref()
    {
        if (--refCount_ == 0)
        {
            try
            {
                final_action();
            }
            catch (...)
            {
                delete this;
                throw;
            }

            delete this;
        }
    }

    template <typename T>
    void accumulate(T const & t) { get_query_stream() << t; }

protected:
    // this function allows to break the circular dependenc
    // between session and this class
    std::ostringstream & get_query_stream();

    int refCount_;

    session & session_;

private:
    // noncopyable
    ref_counted_statement_base(ref_counted_statement_base const&);
    ref_counted_statement_base& operator=(ref_counted_statement_base const&);
};

// this class is supposed to be a vehicle for the "once" statements
// it executes the whole statement in its destructor
class ref_counted_statement : public ref_counted_statement_base
{
public:
    ref_counted_statement(session & s)
        : ref_counted_statement_base(s), st_(s) {}

    void exchange(into_type_ptr const & i) { st_.exchange(i); }
    void exchange(use_type_ptr const & u) { st_.exchange(u); }

    virtual void final_action();

private:
    statement st_;
};

} // namespace details

} // namespace soci

#endif
