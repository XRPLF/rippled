//
// Copyright (C) 2004-2016 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_REF_COUNTED_STATEMENT_H_INCLUDED
#define SOCI_REF_COUNTED_STATEMENT_H_INCLUDED

#include "soci/statement.h"
#include "soci/into-type.h"
#include "soci/use-type.h"
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
                if (tail_.empty() == false)
                {
                    accumulate(tail_);
                }

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

    void set_tail(const std::string & tail) { tail_ = tail; }
    void set_need_comma(bool need_comma) { need_comma_ = need_comma; }
    bool get_need_comma() const { return need_comma_; }

protected:
    // this function allows to break the circular dependenc
    // between session and this class
    std::ostringstream & get_query_stream();

    int refCount_;

    session & session_;

    // used mainly for portable ddl
    std::string tail_;
    bool need_comma_;

private:
    SOCI_NOT_COPYABLE(ref_counted_statement_base)
};

// this class is supposed to be a vehicle for the "once" statements
// it executes the whole statement in its destructor
class ref_counted_statement : public ref_counted_statement_base
{
public:
    ref_counted_statement(session & s)
        : ref_counted_statement_base(s), st_(s) {}

    void final_action() SOCI_OVERRIDE;

    template <typename T>
    void exchange(T &t) { st_.exchange(t); }

private:
    statement st_;
};

} // namespace details

} // namespace soci

#endif
