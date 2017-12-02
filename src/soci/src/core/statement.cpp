//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SOURCE
#include "soci/statement.h"
#include "soci/session.h"
#include "soci/into-type.h"
#include "soci/use-type.h"
#include "soci/values.h"
#include "soci-compiler.h"
#include <ctime>
#include <cctype>

using namespace soci;
using namespace soci::details;


statement_impl::statement_impl(session & s)
    : session_(s), refCount_(1), row_(0),
      fetchSize_(1), initialFetchSize_(1),
      alreadyDescribed_(false)
{
    backEnd_ = s.make_statement_backend();
}

statement_impl::statement_impl(prepare_temp_type const & prep)
    : session_(prep.get_prepare_info()->session_),
      refCount_(1), row_(0), fetchSize_(1), alreadyDescribed_(false)
{
    backEnd_ = session_.make_statement_backend();

    ref_counted_prepare_info * prepInfo = prep.get_prepare_info();

    // take all bind/define info
    intos_.swap(prepInfo->intos_);
    uses_.swap(prepInfo->uses_);

    // allocate handle
    alloc();

    // prepare the statement
    query_ = prepInfo->get_query();
    try
    {
        prepare(query_);
    }
    catch(...)
    {
        clean_up();
        throw;
    }

    define_and_bind();
}

statement_impl::~statement_impl()
{
    clean_up();
}

void statement_impl::alloc()
{
    backEnd_->alloc();
}

void statement_impl::bind(values & values)
{
    std::size_t cnt = 0;

    try
    {
        for (std::vector<details::standard_use_type*>::iterator it =
            values.uses_.begin(); it != values.uses_.end(); ++it)
        {
            // only bind those variables which are:
            // - either named and actually referenced in the statement,
            // - or positional

            std::string const& useName = (*it)->get_name();
            if (useName.empty())
            {
                // positional use element

                int position = static_cast<int>(uses_.size());
                (*it)->bind(*this, position);
                uses_.push_back(*it);
                indicators_.push_back(values.indicators_[cnt]);
            }
            else
            {
                // named use element - check if it is used
                std::string const placeholder = ":" + useName;

                std::size_t pos = query_.find(placeholder);
                while (pos != std::string::npos)
                {
                    // Retrieve next char after placeholder
                    // make sure we do not go out of range on the string
                    const char nextChar = (pos + placeholder.size()) < query_.size() ?
                                          query_[pos + placeholder.size()] : '\0';

                    if (std::isalnum(nextChar))
                    {
                        // We got a partial match only,
                        // keep looking for the placeholder
                        pos = query_.find(placeholder, pos + placeholder.size());
                    }
                    else
                    {
                        int position = static_cast<int>(uses_.size());
                        (*it)->bind(*this, position);
                        uses_.push_back(*it);
                        indicators_.push_back(values.indicators_[cnt]);
                        // Ok we found it, done
                        break;
                    }
                }
                // In case we couldn't find the placeholder
                if (pos == std::string::npos)
                {
                    values.add_unused(*it, values.indicators_[cnt]);
                }
            }

            cnt++;
        }
    }
    catch (...)
    {
        for (std::size_t i = ++cnt; i != values.uses_.size(); ++i)
        {
            values.add_unused(values.uses_[i], values.indicators_[i]);
        }

        rethrow_current_exception_with_context("binding parameters of");
    }
}

void statement_impl::bind_clean_up()
{
    // deallocate all bind and define objects
    std::size_t const isize = intos_.size();
    for (std::size_t i = isize; i != 0; --i)
    {
        intos_[i - 1]->clean_up();
        delete intos_[i - 1];
        intos_.resize(i - 1);
    }

    std::size_t const ifrsize = intosForRow_.size();
    for (std::size_t i = ifrsize; i != 0; --i)
    {
        intosForRow_[i - 1]->clean_up();
        delete intosForRow_[i - 1];
        intosForRow_.resize(i - 1);
    }

    std::size_t const usize = uses_.size();
    for (std::size_t i = usize; i != 0; --i)
    {
        uses_[i - 1]->clean_up();
        delete uses_[i - 1];
        uses_.resize(i - 1);
    }

    std::size_t const indsize = indicators_.size();
    for (std::size_t i = 0; i != indsize; ++i)
    {
        delete indicators_[i];
        indicators_[i] = NULL;
    }

    row_ = NULL;
    alreadyDescribed_ = false;
}

void statement_impl::clean_up()
{
    bind_clean_up();
    if (backEnd_ != NULL)
    {
        backEnd_->clean_up();
        delete backEnd_;
        backEnd_ = NULL;
    }
}

void statement_impl::prepare(std::string const & query,
    statement_type eType)
{
    try
    {
        query_ = query;
        session_.log_query(query);

        backEnd_->prepare(query, eType);
    }
    catch (...)
    {
        rethrow_current_exception_with_context("preparing");
    }
}

void statement_impl::define_and_bind()
{
    int definePosition = 1;
    std::size_t const isize = intos_.size();
    for (std::size_t i = 0; i != isize; ++i)
    {
        intos_[i]->define(*this, definePosition);
    }

    // if there are some implicite into elements
    // injected by the row description process,
    // they should be defined in the later phase,
    // starting at the position where the above loop finished
    definePositionForRow_ = definePosition;

    int bindPosition = 1;
    std::size_t const usize = uses_.size();
    for (std::size_t i = 0; i != usize; ++i)
    {
        uses_[i]->bind(*this, bindPosition);
    }
}

void statement_impl::define_for_row()
{
    std::size_t const isize = intosForRow_.size();
    for (std::size_t i = 0; i != isize; ++i)
    {
        intosForRow_[i]->define(*this, definePositionForRow_);
    }
}

void statement_impl::undefine_and_bind()
{
    std::size_t const isize = intos_.size();
    for (std::size_t i = isize; i != 0; --i)
    {
        intos_[i - 1]->clean_up();
    }

    std::size_t const ifrsize = intosForRow_.size();
    for (std::size_t i = ifrsize; i != 0; --i)
    {
        intosForRow_[i - 1]->clean_up();
    }

    std::size_t const usize = uses_.size();
    for (std::size_t i = usize; i != 0; --i)
    {
        uses_[i - 1]->clean_up();
    }
}

bool statement_impl::execute(bool withDataExchange)
{
    try
    {
        initialFetchSize_ = intos_size();

        if (intos_.empty() == false && initialFetchSize_ == 0)
        {
            // this can happen only with into-vectors elements
            // and is not allowed when calling execute
            throw soci_error("Vectors of size 0 are not allowed.");
        }

        fetchSize_ = initialFetchSize_;

        // pre-use should be executed before inspecting the sizes of use
        // elements, as they can be resized in type conversion routines

        pre_use();

        std::size_t const bindSize = uses_size();

        if (bindSize > 1 && fetchSize_ > 1)
        {
            throw soci_error(
                 "Bulk insert/update and bulk select not allowed in same query");
        }

        // looks like a hack and it is - row description should happen
        // *after* the use elements were completely prepared
        // and *before* the into elements are touched, so that the row
        // description process can inject more into elements for
        // implicit data exchange
        if (row_ != NULL && alreadyDescribed_ == false)
        {
            describe();
            define_for_row();
        }

        int num = 0;
        if (withDataExchange)
        {
            num = 1;

            pre_fetch();

            if (static_cast<int>(fetchSize_) > num)
            {
                num = static_cast<int>(fetchSize_);
            }
            if (static_cast<int>(bindSize) > num)
            {
                num = static_cast<int>(bindSize);
            }
        }
        
        pre_exec(num);

        statement_backend::exec_fetch_result res = backEnd_->execute(num);

        bool gotData = false;

        if (res == statement_backend::ef_success)
        {
            // the "success" means that the statement executed correctly
            // and for select statement this also means that some rows were read

            if (num > 0)
            {
                gotData = true;

                // ensure into vectors have correct size
                resize_intos(static_cast<std::size_t>(num));
            }
        }
        else // res == ef_no_data
        {
            // the "no data" means that the end-of-rowset condition was hit
            // but still some rows might have been read (the last bunch of rows)
            // it can also mean that the statement did not produce any results

            gotData = fetchSize_ > 1 ? resize_intos() : false;
        }

        if (num > 0)
        {
            post_fetch(gotData, false);
        }

        post_use(gotData);

        session_.set_got_data(gotData);
        return gotData;
    }
    catch (...)
    {
        rethrow_current_exception_with_context("executing");
    }
}

long long statement_impl::get_affected_rows()
{
    try
    {
        return backEnd_->get_affected_rows();
    }
    catch (...)
    {
        rethrow_current_exception_with_context("getting the number of rows affected by");
    }
}

bool statement_impl::fetch()
{
    try
    {
        if (fetchSize_ == 0)
        {
            truncate_intos();
            session_.set_got_data(false);
            return false;
        }

        bool gotData = false;

        // vectors might have been resized between fetches
        std::size_t const newFetchSize = intos_size();
        if (newFetchSize > initialFetchSize_)
        {
            // this is not allowed, because most likely caused reallocation
            // of the vector - this would require complete re-bind

            throw soci_error(
                "Increasing the size of the output vector is not supported.");
        }
        else if (newFetchSize == 0)
        {
            session_.set_got_data(false);
            return false;
        }
        else
        {
            // the output vector was downsized or remains the same as before
            fetchSize_ = newFetchSize;
        }

        statement_backend::exec_fetch_result const res = backEnd_->fetch(static_cast<int>(fetchSize_));
        if (res == statement_backend::ef_success)
        {
            // the "success" means that some number of rows was read
            // and that it is not yet the end-of-rowset (there are more rows)

            gotData = true;

            // ensure into vectors have correct size
            resize_intos(fetchSize_);
        }
        else // res == ef_no_data
        {
            // end-of-rowset condition

            if (fetchSize_ > 1)
            {
                // but still the last bunch of rows might have been read
                gotData = resize_intos();
                fetchSize_ = 0;
            }
            else
            {
                truncate_intos();
                gotData = false;
            }
        }

        post_fetch(gotData, true);
        session_.set_got_data(gotData);
        return gotData;
    }
    catch (...)
    {
        rethrow_current_exception_with_context("fetching data from");
    }
}

std::size_t statement_impl::intos_size()
{
    // this function does not need to take into account intosForRow_ elements,
    // since their sizes are always 1 (which is the same and the primary
    // into(row) element, which has injected them)

    std::size_t intos_size = 0;
    std::size_t const isize = intos_.size();
    for (std::size_t i = 0; i != isize; ++i)
    {
        if (i==0)
        {
            intos_size = intos_[i]->size();
        }
        else if (intos_size != intos_[i]->size())
        {
            std::ostringstream msg;
            msg << "Bind variable size mismatch (into["
                << static_cast<unsigned long>(i) << "] has size "
                << static_cast<unsigned long>(intos_[i]->size())
                << ", into[0] has size "
                << static_cast<unsigned long>(intos_size);
            throw soci_error(msg.str());
        }
    }
    return intos_size;
}

std::size_t statement_impl::uses_size()
{
    std::size_t usesSize = 0;
    std::size_t const usize = uses_.size();
    for (std::size_t i = 0; i != usize; ++i)
    {
        if (i==0)
        {
            usesSize = uses_[i]->size();
            if (usesSize == 0)
            {
                 // this can happen only for vectors
                 throw soci_error("Vectors of size 0 are not allowed.");
            }
        }
        else if (usesSize != uses_[i]->size())
        {
            std::ostringstream msg;
            msg << "Bind variable size mismatch (use["
                << static_cast<unsigned long>(i) << "] has size "
                << static_cast<unsigned long>(uses_[i]->size())
                << ", use[0] has size "
                << static_cast<unsigned long>(usesSize);
            throw soci_error(msg.str());
        }
    }
    return usesSize;
}

bool statement_impl::resize_intos(std::size_t upperBound)
{
    // this function does not need to take into account the intosForRow_
    // elements, since they are never used for bulk operations

    int rows = backEnd_->get_number_of_rows();
    if (rows < 0)
    {
        rows = 0;
    }
    if (upperBound != 0 && upperBound < static_cast<std::size_t>(rows))
    {
        rows = static_cast<int>(upperBound);
    }

    std::size_t const isize = intos_.size();
    for (std::size_t i = 0; i != isize; ++i)
    {
        intos_[i]->resize((std::size_t)rows);
    }

    return rows > 0 ? true : false;
}

void statement_impl::truncate_intos()
{
    std::size_t const isize = intos_.size();
    for (std::size_t i = 0; i != isize; ++i)
    {
        intos_[i]->resize(0);
    }
}

void statement_impl::pre_exec(int num)
{
    std::size_t const isize = intos_.size();
    for (std::size_t i = 0; i != isize; ++i)
    {
        intos_[i]->pre_exec(num);
    }
    
    std::size_t const ifrsize = intosForRow_.size();
    for (std::size_t i = 0; i != ifrsize; ++i)
    {
        intosForRow_[i]->pre_exec(num);
    }
    
    std::size_t const usize = uses_.size();
    for (std::size_t i = 0; i != usize; ++i)
    {
        uses_[i]->pre_exec(num);
    }
}

void statement_impl::pre_fetch()
{
    std::size_t const isize = intos_.size();
    for (std::size_t i = 0; i != isize; ++i)
    {
        intos_[i]->pre_fetch();
    }

    std::size_t const ifrsize = intosForRow_.size();
    for (std::size_t i = 0; i != ifrsize; ++i)
    {
        intosForRow_[i]->pre_fetch();
    }
}

void statement_impl::pre_use()
{
    std::size_t const usize = uses_.size();
    for (std::size_t i = 0; i != usize; ++i)
    {
        uses_[i]->pre_use();
    }
}

void statement_impl::post_fetch(bool gotData, bool calledFromFetch)
{
    // first iterate over intosForRow_ elements, since the Row element
    // (which is among the intos_ elements) might depend on the
    // values of those implicitly injected elements

    std::size_t const ifrsize = intosForRow_.size();
    for (std::size_t i = 0; i != ifrsize; ++i)
    {
        intosForRow_[i]->post_fetch(gotData, calledFromFetch);
    }

    std::size_t const isize = intos_.size();
    for (std::size_t i = 0; i != isize; ++i)
    {
        intos_[i]->post_fetch(gotData, calledFromFetch);
    }
}

void statement_impl::post_use(bool gotData)
{
    // iterate in reverse order here in case the first item
    // is an UseType<Values> (since it depends on the other UseTypes)
    for (std::size_t i = uses_.size(); i != 0; --i)
    {
        uses_[i-1]->post_use(gotData);
    }
}

namespace soci
{
namespace details
{

// Map data_types to stock types for dynamic result set support

template<>
void statement_impl::bind_into<dt_string>()
{
    into_row<std::string>();
}

template<>
void statement_impl::bind_into<dt_double>()
{
    into_row<double>();
}

template<>
void statement_impl::bind_into<dt_integer>()
{
    into_row<int>();
}

template<>
void statement_impl::bind_into<dt_long_long>()
{
    into_row<long long>();
}

template<>
void statement_impl::bind_into<dt_unsigned_long_long>()
{
    into_row<unsigned long long>();
}

template<>
void statement_impl::bind_into<dt_date>()
{
    into_row<std::tm>();
}

void statement_impl::describe()
{
    row_->clean_up();

    int const numcols = backEnd_->prepare_for_describe();
    for (int i = 1; i <= numcols; ++i)
    {
        data_type dtype;
        std::string columnName;

        backEnd_->describe_column(i, dtype, columnName);

        column_properties props;
        props.set_name(columnName);
        props.set_data_type(dtype);

        switch (dtype)
        {
        case dt_string:
            bind_into<dt_string>();
            break;
        case dt_double:
            bind_into<dt_double>();
            break;
        case dt_integer:
            bind_into<dt_integer>();
            break;
        case dt_long_long:
            bind_into<dt_long_long>();
            break;
        case dt_unsigned_long_long:
            bind_into<dt_unsigned_long_long>();
            break;
        case dt_date:
            bind_into<dt_date>();
            break;
        default:
            std::ostringstream msg;
            msg << "db column type " << dtype
                <<" not supported for dynamic selects"<<std::endl;
            throw soci_error(msg.str());
        }
        row_->add_properties(props);
    }

    alreadyDescribed_ = true;
}

} // namespace details
} // namespace soci

void statement_impl::set_row(row * r)
{
    if (row_ != NULL)
    {
        throw soci_error(
            "Only one Row element allowed in a single statement.");
    }

    row_ = r;
    row_->uppercase_column_names(session_.get_uppercase_column_names());
}

std::string statement_impl::rewrite_for_procedure_call(std::string const & query)
{
    return backEnd_->rewrite_for_procedure_call(query);
}

void statement_impl::inc_ref()
{
    ++refCount_;
}

void statement_impl::dec_ref()
{
    if (--refCount_ == 0)
    {
        delete this;
    }
}

standard_into_type_backend *
statement_impl::make_into_type_backend()
{
    return backEnd_->make_into_type_backend();
}

standard_use_type_backend *
statement_impl::make_use_type_backend()
{
    return backEnd_->make_use_type_backend();
}

vector_into_type_backend *
statement_impl::make_vector_into_type_backend()
{
    return backEnd_->make_vector_into_type_backend();
}

vector_use_type_backend *
statement_impl::make_vector_use_type_backend()
{
    return backEnd_->make_vector_use_type_backend();
}

SOCI_NORETURN
statement_impl::rethrow_current_exception_with_context(char const* operation)
{
    try
    {
        throw;
    }
    catch (soci_error& e)
    {
        if (!query_.empty())
        {
            std::ostringstream oss;
            oss << "while " << operation << " \"" << query_ << "\"";

            if (!uses_.empty())
            {
                oss << " with ";

                std::size_t const usize = uses_.size();
                for (std::size_t i = 0; i != usize; ++i)
                {
                    if (i != 0)
                        oss << ", ";

                    details::use_type_base const& u = *uses_[i];

                    // Use the name specified in the "use()" call if any,
                    // otherwise get the name of the matching parameter from
                    // the query itself, as parsed by the backend.
                    std::string name = u.get_name();
                    if (name.empty())
                        name = backEnd_->get_parameter_name(static_cast<int>(i));

                    oss << ":";
                    if (!name.empty())
                        oss << name;
                    else
                        oss << (i + 1);
                    oss << "=";

                    u.dump_value(oss);
                }
            }

            e.add_context(oss.str());
        }

        throw;
    }
}
