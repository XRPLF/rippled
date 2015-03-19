//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_STATEMENT_H_INCLUDED
#define SOCI_STATEMENT_H_INCLUDED

#include "into-type.h"
#include "into.h"
#include "use-type.h"
#include "soci-backend.h"
#include "row.h"
// std
#include <cstddef>
#include <string>
#include <map>
#include <vector>

namespace soci
{

class session;
class values;

namespace details
{

class into_type_base;
class use_type_base;
class prepare_temp_type;

class SOCI_DECL statement_impl
{
public:
    explicit statement_impl(session & s);
    explicit statement_impl(prepare_temp_type const & prep);
    ~statement_impl();

    void alloc();
    void bind(values & v);
    void exchange(into_type_ptr const & i);
    void exchange(use_type_ptr const & u);
    void clean_up();

    void prepare(std::string const & query,
                    statement_type eType = st_repeatable_query);
    void define_and_bind();
    void undefine_and_bind();
    bool execute(bool withDataExchange = false);
    long long get_affected_rows();
    bool fetch();
    void describe();
    void set_row(row * r);
    void exchange_for_rowset(into_type_ptr const & i);

    // for diagnostics and advanced users
    // (downcast it to expected back-end statement class)
    statement_backend * get_backend() { return backEnd_; }

    standard_into_type_backend * make_into_type_backend();
    standard_use_type_backend * make_use_type_backend();
    vector_into_type_backend * make_vector_into_type_backend();
    vector_use_type_backend * make_vector_use_type_backend();

    void inc_ref();
    void dec_ref();

    session & session_;

    std::string rewrite_for_procedure_call(std::string const & query);

protected:
    std::vector<details::into_type_base *> intos_;
    std::vector<details::use_type_base *> uses_;
    std::vector<indicator *> indicators_;

private:

    int refCount_;

    row * row_;
    std::size_t fetchSize_;
    std::size_t initialFetchSize_;
    std::string query_;
    std::map<std::string, use_type_base *> namedUses_;

    std::vector<into_type_base *> intosForRow_;
    int definePositionForRow_;

    void exchange_for_row(into_type_ptr const & i);
    void define_for_row();

    template<typename T>
    void into_row()
    {
        T * t = new T();
        indicator * ind = new indicator(i_ok);
        row_->add_holder(t, ind);
        exchange_for_row(into(*t, *ind));
    }

    template<data_type>
    void bind_into();

    bool alreadyDescribed_;

    std::size_t intos_size();
    std::size_t uses_size();
    void pre_fetch();
    void pre_use();
    void post_fetch(bool gotData, bool calledFromFetch);
    void post_use(bool gotData);
    bool resize_intos(std::size_t upperBound = 0);
    void truncate_intos();

    soci::details::statement_backend * backEnd_;

    // The type is noncopyable.
    statement_impl(statement_impl const &);
    statement_impl& operator=(statement_impl const &);

};

} // namespace details

// Statement is a handle class for statement_impl
// (this provides copyability to otherwise non-copyable type)
class SOCI_DECL statement
{
public:
    statement(session & s)
        : impl_(new details::statement_impl(s)) {}
    statement(details::prepare_temp_type const & prep)
        : impl_(new details::statement_impl(prep)) {}
    ~statement() { impl_->dec_ref(); }

    // copy is supported for this handle class
    statement(statement const & other)
        : impl_(other.impl_)
    {
        impl_->inc_ref();
    }

    void operator=(statement const & other)
    {
        other.impl_->inc_ref();
        impl_->dec_ref();
        impl_ = other.impl_;
    }

    void alloc()                         { impl_->alloc();    }
    void bind(values & v)                { impl_->bind(v);    }
    void exchange(details::into_type_ptr const & i);
    void exchange(details::use_type_ptr const & u);
    void clean_up()                      { impl_->clean_up(); }

    void prepare(std::string const & query,
        details::statement_type eType = details::st_repeatable_query)
    {
        impl_->prepare(query, eType);
    }

    void define_and_bind() { impl_->define_and_bind(); }
    void undefine_and_bind()  { impl_->undefine_and_bind(); }
    bool execute(bool withDataExchange = false)
    {
        gotData_ = impl_->execute(withDataExchange);
        return gotData_;
    }

    long long get_affected_rows()
    {
        return impl_->get_affected_rows();
    }

    bool fetch()
    {
        gotData_ = impl_->fetch();
        return gotData_;
    }

    bool got_data() const { return gotData_; }

    void describe()       { impl_->describe(); }
    void set_row(row * r) { impl_->set_row(r); }
    void exchange_for_rowset(details::into_type_ptr const & i)
    {
        impl_->exchange_for_rowset(i);
    }

    // for diagnostics and advanced users
    // (downcast it to expected back-end statement class)
    details::statement_backend * get_backend()
    {
        return impl_->get_backend();
    }

    details::standard_into_type_backend * make_into_type_backend()
    {
        return impl_->make_into_type_backend();
    }

    details::standard_use_type_backend * make_use_type_backend()
    {
        return impl_->make_use_type_backend();
    }

    details::vector_into_type_backend * make_vector_into_type_backend()
    {
        return impl_->make_vector_into_type_backend();
    }

    details::vector_use_type_backend * make_vector_use_type_backend()
    {
        return impl_->make_vector_use_type_backend();
    }

    std::string rewrite_for_procedure_call(std::string const & query)
    {
        return impl_->rewrite_for_procedure_call(query);
    }

private:
    details::statement_impl * impl_;
    bool gotData_;
};

namespace details
{
// exchange_traits for statement

template <>
struct exchange_traits<statement>
{
    typedef basic_type_tag type_family;
    enum { x_type = x_statement };
};

// into and use types for Statement (for nested statements and cursors)

template <>
class into_type<statement> : public standard_into_type
{
public:
    into_type(statement & s) : standard_into_type(&s, x_statement) {}
    into_type(statement & s, indicator & ind)
        : standard_into_type(&s, x_statement, ind) {}
};

template <>
class use_type<statement> : public standard_use_type
{
public:
    use_type(statement & s, std::string const & name = std::string())
        : standard_use_type(&s, x_statement, false, name) {}
    use_type(statement & s, indicator & ind,
        std::string const & name = std::string())
        : standard_use_type(&s, x_statement, ind, false, name) {}

    // Note: there is no const version of use for statement,
    // because most likely it would not make much sense anyway.
};

} // namespace details

} // namespace soci

#endif // SOCI_STATEMENT_H_INCLUDED
