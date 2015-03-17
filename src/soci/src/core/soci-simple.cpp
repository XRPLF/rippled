//
// Copyright (C) 2008 Maciej Sobczak
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SOURCE

#include "soci-simple.h"
#include "soci.h"

#include <cstddef>
#include <cstdio>
#include <ctime>
#include <exception>
#include <map>
#include <string>
#include <vector>

using namespace soci;

namespace // unnamed
{

struct session_wrapper
{
    session sql;

    bool is_ok;
    std::string error_message;
};

} // namespace unnamed


SOCI_DECL session_handle soci_create_session(char const * connection_string)
{
    session_wrapper * wrapper = NULL;
    try
    {
        wrapper = new session_wrapper();
    }
    catch (...)
    {
        return NULL;
    }

    try
    {
        wrapper->sql.open(connection_string);
        wrapper->is_ok = true;
    }
    catch (std::exception const & e)
    {
        wrapper->is_ok = false;
        wrapper->error_message = e.what();
    }

    return wrapper;
}

SOCI_DECL void soci_destroy_session(session_handle s)
{
    session_wrapper * wrapper = static_cast<session_wrapper *>(s);
    delete wrapper;
}

SOCI_DECL void soci_begin(session_handle s)
{
    session_wrapper * wrapper = static_cast<session_wrapper *>(s);
    try
    {
        wrapper->sql.begin();
        wrapper->is_ok = true;
    }
    catch (std::exception const & e)
    {
        wrapper->is_ok = false;
        wrapper->error_message = e.what();
    }
}

SOCI_DECL void soci_commit(session_handle s)
{
    session_wrapper * wrapper = static_cast<session_wrapper *>(s);
    try
    {
        wrapper->sql.commit();
        wrapper->is_ok = true;
    }
    catch (std::exception const & e)
    {
        wrapper->is_ok = false;
        wrapper->error_message = e.what();
    }
}

SOCI_DECL void soci_rollback(session_handle s)
{
    session_wrapper * wrapper = static_cast<session_wrapper *>(s);
    try
    {
        wrapper->sql.rollback();
        wrapper->is_ok = true;
    }
    catch (std::exception const & e)
    {
        wrapper->is_ok = false;
        wrapper->error_message = e.what();
    }
}

// this will not be needed until dynamic row is exposed
// SOCI_DECL void soci_uppercase_column_names(session_handle s, bool forceToUpper)
// {
//     session_wrapper * wrapper = static_cast<session_wrapper *>(s);
//     wrapper->sql.uppercase_column_names(forceToUpper);
//     wrapper->is_ok = true;
// }

SOCI_DECL int soci_session_state(session_handle s)
{
    session_wrapper * wrapper = static_cast<session_wrapper *>(s);

    return wrapper->is_ok ? 1 : 0;
}

SOCI_DECL char const * soci_session_error_message(session_handle s)
{
    session_wrapper * wrapper = static_cast<session_wrapper *>(s);

    return wrapper->error_message.c_str();
}


// statement


namespace // unnamed
{

struct statement_wrapper
{
    statement_wrapper(session & sql)
        : st(sql), statement_state(clean), into_kind(empty), use_kind(empty),
          next_position(0), is_ok(true) {}

    statement st;

    enum state { clean, defining, executing } statement_state;
    enum kind { empty, single, bulk } into_kind, use_kind;

    // into elements
    int next_position;
    std::vector<data_type> into_types; // for both single and bulk
    std::vector<indicator> into_indicators;
    std::map<int, std::string> into_strings;
    std::map<int, int> into_ints;
    std::map<int, long long> into_longlongs;
    std::map<int, double> into_doubles;
    std::map<int, std::tm> into_dates;

    std::vector<std::vector<indicator> > into_indicators_v;
    std::map<int, std::vector<std::string> > into_strings_v;
    std::map<int, std::vector<int> > into_ints_v;
    std::map<int, std::vector<long long> > into_longlongs_v;
    std::map<int, std::vector<double> > into_doubles_v;
    std::map<int, std::vector<std::tm> > into_dates_v;

    // use elements
    std::map<std::string, indicator> use_indicators;
    std::map<std::string, std::string> use_strings;
    std::map<std::string, int> use_ints;
    std::map<std::string, long long> use_longlongs;
    std::map<std::string, double> use_doubles;
    std::map<std::string, std::tm> use_dates;

    std::map<std::string, std::vector<indicator> > use_indicators_v;
    std::map<std::string, std::vector<std::string> > use_strings_v;
    std::map<std::string, std::vector<int> > use_ints_v;
    std::map<std::string, std::vector<long long> > use_longlongs_v;
    std::map<std::string, std::vector<double> > use_doubles_v;
    std::map<std::string, std::vector<std::tm> > use_dates_v;

    // format is: "YYYY MM DD hh mm ss"
    char date_formatted[20];

    bool is_ok;
    std::string error_message;
};

// helper for checking if the attempt was made to add more into/use elements
// after the statement was set for execution
bool cannot_add_elements(statement_wrapper & wrapper, statement_wrapper::kind k, bool into)
{
    if (wrapper.statement_state == statement_wrapper::executing)
    {
        wrapper.is_ok = false;
        wrapper.error_message = "Cannot add more data items.";
        return true;
    }
    
    if (into)
    {
        if (k == statement_wrapper::single && wrapper.into_kind == statement_wrapper::bulk)
        {
            wrapper.is_ok = false;
            wrapper.error_message = "Cannot add single into data items.";
            return true;
        }
        if (k == statement_wrapper::bulk && wrapper.into_kind == statement_wrapper::single)
        {
            wrapper.is_ok = false;
            wrapper.error_message = "Cannot add vector into data items.";
            return true;
        }
    }
    else
    {
        // trying to add use elements
        if (k == statement_wrapper::single && wrapper.use_kind == statement_wrapper::bulk)
        {
            wrapper.is_ok = false;
            wrapper.error_message = "Cannot add single use data items.";
            return true;
        }
        if (k == statement_wrapper::bulk && wrapper.use_kind == statement_wrapper::single)
        {
            wrapper.is_ok = false;
            wrapper.error_message = "Cannot add vector use data items.";
            return true;
        }
    }

    wrapper.is_ok = true;
    return false;
}

// helper for checking if the expected into element exists on the given position
bool position_check_failed(statement_wrapper & wrapper, statement_wrapper::kind k,
    int position, data_type expected_type, char const * type_name)
{
    if (position < 0 || position >= wrapper.next_position)
    {
        wrapper.is_ok = false;
        wrapper.error_message = "Invalid position.";
        return true;
    }

    if (wrapper.into_types[position] != expected_type)
    {
        wrapper.is_ok = false;
        wrapper.error_message = "No into ";
        if (k == statement_wrapper::bulk)
        {
            wrapper.error_message += "vector ";
        }
        wrapper.error_message += type_name;
        wrapper.error_message += " element at this position.";
        return true;
    }

    wrapper.is_ok = true;
    return false;
}

// helper for checking if the into element on the given position
// is not null
bool not_null_check_failed(statement_wrapper & wrapper, int position)
{
    if (wrapper.into_indicators[position] == i_null)
    {
        wrapper.is_ok = false;
        wrapper.error_message = "Element is null.";
        return true;
    }

    wrapper.is_ok = true;
    return false;
}

// overloaded version for vectors
bool not_null_check_failed(statement_wrapper & wrapper, int position, int index)
{
    if (wrapper.into_indicators_v[position][index] == i_null)
    {
        wrapper.is_ok = false;
        wrapper.error_message = "Element is null.";
        return true;
    }

    wrapper.is_ok = true;
    return false;
}

// helper for checking the index value
template <typename T>
bool index_check_failed(std::vector<T> const & v,
    statement_wrapper & wrapper, int index)
{
    if (index < 0 || index >= static_cast<int>(v.size()))
    {
        wrapper.is_ok = false;
        wrapper.error_message = "Invalid index.";
        return true;
    }

    wrapper.is_ok = true;
    return false;
}

// helper for checking the uniqueness of the use element's name
bool name_unique_check_failed(statement_wrapper & wrapper,
    statement_wrapper::kind k, char const * name)
{
    bool is_unique;
    if (k == statement_wrapper::single)
    {
        typedef std::map<std::string, indicator>::const_iterator iterator;
        iterator const it = wrapper.use_indicators.find(name);
        is_unique = it == wrapper.use_indicators.end();
    }
    else
    {
        // vector version

        typedef std::map
            <
                std::string,
                std::vector<indicator>
            >::const_iterator iterator;

        iterator const it = wrapper.use_indicators_v.find(name);
        is_unique = it == wrapper.use_indicators_v.end();
    }

    if (is_unique)
    {
        wrapper.is_ok = true;
        return false;
    }
    else
    {
        wrapper.is_ok = false;
        wrapper.error_message = "Name of use element should be unique.";
        return true;
    }
}

// helper for checking if the use element with the given name exists
bool name_exists_check_failed(statement_wrapper & wrapper,
    char const * name, data_type expected_type,
    statement_wrapper::kind k, char const * type_name)
{
    bool name_exists = false;
    if (k == statement_wrapper::single)
    {
        switch (expected_type)
        {
        case dt_string:
            {
                typedef std::map
                    <
                        std::string,
                        std::string
                    >::const_iterator iterator;
                iterator const it = wrapper.use_strings.find(name);
                name_exists = (it != wrapper.use_strings.end());
            }
            break;
        case dt_integer:
            {
                typedef std::map<std::string, int>::const_iterator iterator;
                iterator const it = wrapper.use_ints.find(name);
                name_exists = (it != wrapper.use_ints.end());
            }
            break;
        case dt_long_long:
            {
                typedef std::map<std::string, long long>::const_iterator
                    iterator;
                iterator const it = wrapper.use_longlongs.find(name);
                name_exists = (it != wrapper.use_longlongs.end());
            }
            break;
        case dt_double:
            {
                typedef std::map<std::string, double>::const_iterator iterator;
                iterator const it = wrapper.use_doubles.find(name);
                name_exists = (it != wrapper.use_doubles.end());
            }
            break;
        case dt_date:
            {
                typedef std::map<std::string, std::tm>::const_iterator iterator;
                iterator const it = wrapper.use_dates.find(name);
                name_exists = (it != wrapper.use_dates.end());
            }
            break;
        default:
            assert(false);
        }
    }
    else
    {
        // vector version

        switch (expected_type)
        {
        case dt_string:
            {
                typedef std::map
                    <
                        std::string,
                        std::vector<std::string>
                    >::const_iterator iterator;
                iterator const it = wrapper.use_strings_v.find(name);
                name_exists = (it != wrapper.use_strings_v.end());
            }
            break;
        case dt_integer:
            {
                typedef std::map
                    <
                        std::string,
                        std::vector<int>
                    >::const_iterator iterator;
                iterator const it = wrapper.use_ints_v.find(name);
                name_exists = (it != wrapper.use_ints_v.end());
            }
            break;
        case dt_long_long:
            {
                typedef std::map
                    <
                        std::string,
                        std::vector<long long>
                    >::const_iterator iterator;
                iterator const it = wrapper.use_longlongs_v.find(name);
                name_exists = (it != wrapper.use_longlongs_v.end());
            }
            break;
        case dt_double:
            {
                typedef std::map<std::string,
                    std::vector<double> >::const_iterator iterator;
                iterator const it = wrapper.use_doubles_v.find(name);
                name_exists = (it != wrapper.use_doubles_v.end());
            }
            break;
        case dt_date:
            {
                typedef std::map<std::string,
                        std::vector<std::tm> >::const_iterator iterator;
                iterator const it = wrapper.use_dates_v.find(name);
                name_exists = (it != wrapper.use_dates_v.end());
            }
            break;
        default:
            assert(false);
        }
    }

    if (name_exists)
    {
        wrapper.is_ok = true;
        return false;
    }
    else
    {
        wrapper.is_ok = false;
        wrapper.error_message = "No use ";
        wrapper.error_message += type_name;
        wrapper.error_message += " element with this name.";
        return true;
    }
}

// helper function for resizing all vectors<T> in the map
template <typename T>
void resize_in_map(std::map<std::string, std::vector<T> > & m, int new_size)
{
    typedef typename std::map<std::string, std::vector<T> >::iterator iterator;
    iterator it = m.begin();
    iterator const end = m.end();
    for ( ; it != end; ++it)
    {
        std::vector<T> & v = it->second;
        v.resize(new_size);
    }
}

// helper for formatting date values
char const * format_date(statement_wrapper & wrapper, std::tm const & d)
{
    std::sprintf(wrapper.date_formatted, "%d %d %d %d %d %d",
        d.tm_year + 1900, d.tm_mon + 1, d.tm_mday,
        d.tm_hour, d.tm_min, d.tm_sec);

    return wrapper.date_formatted;
}

bool string_to_date(char const * val, std::tm & /* out */ dt,
    statement_wrapper & wrapper)
{
    // format is: "YYYY MM DD hh mm ss"
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int const converted = std::sscanf(val, "%d %d %d %d %d %d",
        &year, &month, &day, &hour, &minute, &second);
    if (converted != 6)
    {
        wrapper.is_ok = false;
        wrapper.error_message = "Cannot convert date.";
        return false;
    }

    wrapper.is_ok = true;

    dt.tm_year = year - 1900;
    dt.tm_mon = month - 1;
    dt.tm_mday = day;
    dt.tm_hour = hour;
    dt.tm_min = minute;
    dt.tm_sec = second;

return true;
}

} // namespace unnamed


SOCI_DECL statement_handle soci_create_statement(session_handle s)
{
    session_wrapper * session_w = static_cast<session_wrapper *>(s);
    try
    {
        statement_wrapper * statement_w = new statement_wrapper(session_w->sql);
        return statement_w;
    }
    catch (std::exception const & e)
    {
        session_w->is_ok = false;
        session_w->error_message = e.what();
        return NULL;
    }
}

SOCI_DECL void soci_destroy_statement(statement_handle st)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);
    delete wrapper;
}

SOCI_DECL int soci_into_string(statement_handle st)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (cannot_add_elements(*wrapper, statement_wrapper::single, true))
    {
        return -1;
    }

    wrapper->statement_state = statement_wrapper::defining;
    wrapper->into_kind = statement_wrapper::single;

    wrapper->into_types.push_back(dt_string);
    wrapper->into_indicators.push_back(i_ok);
    wrapper->into_strings[wrapper->next_position]; // create new entry
    return wrapper->next_position++;
}

SOCI_DECL int soci_into_int(statement_handle st)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (cannot_add_elements(*wrapper, statement_wrapper::single, true))
    {
        return -1;
    }

    wrapper->statement_state = statement_wrapper::defining;
    wrapper->into_kind = statement_wrapper::single;

    wrapper->into_types.push_back(dt_integer);
    wrapper->into_indicators.push_back(i_ok);
    wrapper->into_ints[wrapper->next_position]; // create new entry
    return wrapper->next_position++;
}

SOCI_DECL int soci_into_long_long(statement_handle st)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (cannot_add_elements(*wrapper, statement_wrapper::single, true))
    {
        return -1;
    }

    wrapper->statement_state = statement_wrapper::defining;
    wrapper->into_kind = statement_wrapper::single;

    wrapper->into_types.push_back(dt_long_long);
    wrapper->into_indicators.push_back(i_ok);
    wrapper->into_longlongs[wrapper->next_position]; // create new entry
    return wrapper->next_position++;
}

SOCI_DECL int soci_into_double(statement_handle st)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (cannot_add_elements(*wrapper, statement_wrapper::single, true))
    {
        return -1;
    }

    wrapper->statement_state = statement_wrapper::defining;
    wrapper->into_kind = statement_wrapper::single;

    wrapper->into_types.push_back(dt_double);
    wrapper->into_indicators.push_back(i_ok);
    wrapper->into_doubles[wrapper->next_position]; // create new entry
    return wrapper->next_position++;
}

SOCI_DECL int soci_into_date(statement_handle st)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (cannot_add_elements(*wrapper, statement_wrapper::single, true))
    {
        return -1;
    }

    wrapper->statement_state = statement_wrapper::defining;
    wrapper->into_kind = statement_wrapper::single;

    wrapper->into_types.push_back(dt_date);
    wrapper->into_indicators.push_back(i_ok);
    wrapper->into_dates[wrapper->next_position]; // create new entry
    return wrapper->next_position++;
}

SOCI_DECL int soci_into_string_v(statement_handle st)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (cannot_add_elements(*wrapper, statement_wrapper::bulk, true))
    {
        return -1;
    }

    wrapper->statement_state = statement_wrapper::defining;
    wrapper->into_kind = statement_wrapper::bulk;

    wrapper->into_types.push_back(dt_string);
    wrapper->into_indicators_v.push_back(std::vector<indicator>());
    wrapper->into_strings_v[wrapper->next_position];
    return wrapper->next_position++;
}

SOCI_DECL int soci_into_int_v(statement_handle st)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (cannot_add_elements(*wrapper, statement_wrapper::bulk, true))
    {
        return -1;
    }

    wrapper->statement_state = statement_wrapper::defining;
    wrapper->into_kind = statement_wrapper::bulk;

    wrapper->into_types.push_back(dt_integer);
    wrapper->into_indicators_v.push_back(std::vector<indicator>());
    wrapper->into_ints_v[wrapper->next_position];
    return wrapper->next_position++;
}

SOCI_DECL int soci_into_long_long_v(statement_handle st)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (cannot_add_elements(*wrapper, statement_wrapper::bulk, true))
    {
        return -1;
    }

    wrapper->statement_state = statement_wrapper::defining;
    wrapper->into_kind = statement_wrapper::bulk;

    wrapper->into_types.push_back(dt_long_long);
    wrapper->into_indicators_v.push_back(std::vector<indicator>());
    wrapper->into_longlongs_v[wrapper->next_position];
    return wrapper->next_position++;
}

SOCI_DECL int soci_into_double_v(statement_handle st)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (cannot_add_elements(*wrapper, statement_wrapper::bulk, true))
    {
        return -1;
    }

    wrapper->statement_state = statement_wrapper::defining;
    wrapper->into_kind = statement_wrapper::bulk;

    wrapper->into_types.push_back(dt_double);
    wrapper->into_indicators_v.push_back(std::vector<indicator>());
    wrapper->into_doubles_v[wrapper->next_position];
    return wrapper->next_position++;
}

SOCI_DECL int soci_into_date_v(statement_handle st)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (cannot_add_elements(*wrapper, statement_wrapper::bulk, true))
    {
        return -1;
    }

    wrapper->statement_state = statement_wrapper::defining;
    wrapper->into_kind = statement_wrapper::bulk;

    wrapper->into_types.push_back(dt_date);
    wrapper->into_indicators_v.push_back(std::vector<indicator>());
    wrapper->into_dates_v[wrapper->next_position];
    return wrapper->next_position++;
}

SOCI_DECL int soci_get_into_state(statement_handle st, int position)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (position < 0 || position >= wrapper->next_position)
    {
        wrapper->is_ok = false;
        wrapper->error_message = "Invalid position.";
        return 0;
    }

    wrapper->is_ok = true;
    return wrapper->into_indicators[position] == i_ok ? 1 : 0;
}

SOCI_DECL char const * soci_get_into_string(statement_handle st, int position)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (position_check_failed(*wrapper,
            statement_wrapper::single, position, dt_string, "string") ||
        not_null_check_failed(*wrapper, position))
    {
        return "";
    }

    return wrapper->into_strings[position].c_str();
}

SOCI_DECL int soci_get_into_int(statement_handle st, int position)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (position_check_failed(*wrapper,
            statement_wrapper::single, position, dt_integer, "int") ||
        not_null_check_failed(*wrapper, position))
    {
        return 0;
    }

    return wrapper->into_ints[position];
}

SOCI_DECL long long soci_get_into_long_long(statement_handle st, int position)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (position_check_failed(*wrapper,
            statement_wrapper::single, position, dt_long_long, "long long") ||
        not_null_check_failed(*wrapper, position))
    {
        return 0LL;
    }

    return wrapper->into_longlongs[position];
}

SOCI_DECL double soci_get_into_double(statement_handle st, int position)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (position_check_failed(*wrapper,
            statement_wrapper::single, position, dt_double, "double") ||
        not_null_check_failed(*wrapper, position))
    {
        return 0.0;
    }

    return wrapper->into_doubles[position];
}

SOCI_DECL char const * soci_get_into_date(statement_handle st, int position)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (position_check_failed(*wrapper,
            statement_wrapper::single, position, dt_date, "date") ||
        not_null_check_failed(*wrapper, position))
    {
        return "";
    }

    // format is: "YYYY MM DD hh mm ss"
    std::tm const & d = wrapper->into_dates[position];
    return format_date(*wrapper, d);
}

SOCI_DECL int soci_into_get_size_v(statement_handle st)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (wrapper->into_kind != statement_wrapper::bulk)
    {
        wrapper->is_ok = false;
        wrapper->error_message = "No vector into elements.";
        return -1;
    }

    return static_cast<int>(wrapper->into_indicators_v[0].size());
}

SOCI_DECL void soci_into_resize_v(statement_handle st, int new_size)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (new_size <= 0)
    {
        wrapper->is_ok = false;
        wrapper->error_message = "Invalid size.";
        return;
    }

    if (wrapper->into_kind != statement_wrapper::bulk)
    {
        wrapper->is_ok = false;
        wrapper->error_message = "No vector into elements.";
        return;
    }

    for (int i = 0; i != wrapper->next_position; ++i)
    {
        wrapper->into_indicators_v[i].resize(new_size);

        switch (wrapper->into_types[i])
        {
        case dt_string:
            wrapper->into_strings_v[i].resize(new_size);
            break;
        case dt_integer:
            wrapper->into_ints_v[i].resize(new_size);
            break;
        case dt_long_long:
            wrapper->into_longlongs_v[i].resize(new_size);
            break;
        case dt_double:
            wrapper->into_doubles_v[i].resize(new_size);
            break;
        case dt_date:
            wrapper->into_dates_v[i].resize(new_size);
            break;
        default:
            assert(false);
        }
    }

    wrapper->is_ok = true;
}

SOCI_DECL int soci_get_into_state_v(statement_handle st, int position, int index)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (position < 0 || position >= wrapper->next_position)
    {
        wrapper->is_ok = false;
        wrapper->error_message = "Invalid position.";
        return 0;
    }

    std::vector<indicator> const & v = wrapper->into_indicators_v[position];
    if (index_check_failed(v, *wrapper, index))
    {
        return 0;
    }

    return v[index] == i_ok ? 1 : 0;
}

SOCI_DECL char const * soci_get_into_string_v(statement_handle st, int position, int index)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (position_check_failed(*wrapper,
            statement_wrapper::bulk, position, dt_string, "string"))
    {
        return "";
    }

    std::vector<std::string> const & v = wrapper->into_strings_v[position];
    if (index_check_failed(v, *wrapper, index) ||
        not_null_check_failed(*wrapper, position, index))
    {
        return "";
    }

    return v[index].c_str();
}

SOCI_DECL int soci_get_into_int_v(statement_handle st, int position, int index)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (position_check_failed(*wrapper,
            statement_wrapper::bulk, position, dt_integer, "int"))
    {
        return 0;
    }

    std::vector<int> const & v = wrapper->into_ints_v[position];
    if (index_check_failed(v, *wrapper, index) ||
        not_null_check_failed(*wrapper, position, index))
    {
        return 0;
    }

    return v[index];
}

SOCI_DECL long long soci_get_into_long_long_v(statement_handle st, int position, int index)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (position_check_failed(*wrapper,
            statement_wrapper::bulk, position, dt_long_long, "long long"))
    {
        return 0;
    }

    std::vector<long long> const & v = wrapper->into_longlongs_v[position];
    if (index_check_failed(v, *wrapper, index) ||
        not_null_check_failed(*wrapper, position, index))
    {
        return 0;
    }

    return v[index];
}

SOCI_DECL double soci_get_into_double_v(statement_handle st, int position, int index)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (position_check_failed(*wrapper,
            statement_wrapper::bulk, position, dt_double, "double"))
    {
        return 0.0;
    }

    std::vector<double> const & v = wrapper->into_doubles_v[position];
    if (index_check_failed(v, *wrapper, index) ||
        not_null_check_failed(*wrapper, position, index))
    {
        return 0.0;
    }

    return v[index];
}

SOCI_DECL char const * soci_get_into_date_v(statement_handle st, int position, int index)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (position_check_failed(*wrapper,
            statement_wrapper::bulk, position, dt_date, "date"))
    {
        return "";
    }

    std::vector<std::tm> const & v = wrapper->into_dates_v[position];
    if (index_check_failed(v, *wrapper, index) ||
        not_null_check_failed(*wrapper, position, index))
    {
        return "";
    }

    return format_date(*wrapper, v[index]);
}

SOCI_DECL void soci_use_string(statement_handle st, char const * name)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (cannot_add_elements(*wrapper, statement_wrapper::single, false) ||
        name_unique_check_failed(*wrapper, statement_wrapper::single, name))
    {
        return;
    }

    wrapper->statement_state = statement_wrapper::defining;
    wrapper->use_kind = statement_wrapper::single;

    wrapper->use_indicators[name] = i_ok; // create new entry
    wrapper->use_strings[name]; // create new entry
}

SOCI_DECL void soci_use_int(statement_handle st, char const * name)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (cannot_add_elements(*wrapper, statement_wrapper::single, false) ||
        name_unique_check_failed(*wrapper, statement_wrapper::single, name))
    {
        return;
    }

    wrapper->statement_state = statement_wrapper::defining;
    wrapper->use_kind = statement_wrapper::single;

    wrapper->use_indicators[name] = i_ok; // create new entry
    wrapper->use_ints[name]; // create new entry
}

SOCI_DECL void soci_use_long_long(statement_handle st, char const * name)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (cannot_add_elements(*wrapper, statement_wrapper::single, false) ||
        name_unique_check_failed(*wrapper, statement_wrapper::single, name))
    {
        return;
    }

    wrapper->statement_state = statement_wrapper::defining;
    wrapper->use_kind = statement_wrapper::single;

    wrapper->use_indicators[name] = i_ok; // create new entry
    wrapper->use_longlongs[name]; // create new entry
}

SOCI_DECL void soci_use_double(statement_handle st, char const * name)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (cannot_add_elements(*wrapper, statement_wrapper::single, false) ||
        name_unique_check_failed(*wrapper, statement_wrapper::single, name))
    {
        return;
    }

    wrapper->statement_state = statement_wrapper::defining;
    wrapper->use_kind = statement_wrapper::single;

    wrapper->use_indicators[name] = i_ok; // create new entry
    wrapper->use_doubles[name]; // create new entry
}

SOCI_DECL void soci_use_date(statement_handle st, char const * name)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (cannot_add_elements(*wrapper, statement_wrapper::single, false) ||
        name_unique_check_failed(*wrapper, statement_wrapper::single, name))
    {
        return;
    }

    wrapper->statement_state = statement_wrapper::defining;
    wrapper->use_kind = statement_wrapper::single;

    wrapper->use_indicators[name] = i_ok; // create new entry
    wrapper->use_dates[name]; // create new entry
}

SOCI_DECL void soci_use_string_v(statement_handle st, char const * name)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (cannot_add_elements(*wrapper, statement_wrapper::bulk, false) ||
        name_unique_check_failed(*wrapper, statement_wrapper::bulk, name))
    {
        return;
    }

    wrapper->statement_state = statement_wrapper::defining;
    wrapper->use_kind = statement_wrapper::bulk;

    wrapper->use_indicators_v[name]; // create new entry
    wrapper->use_strings_v[name]; // create new entry
}

SOCI_DECL void soci_use_int_v(statement_handle st, char const * name)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (cannot_add_elements(*wrapper, statement_wrapper::bulk, false) ||
        name_unique_check_failed(*wrapper, statement_wrapper::bulk, name))
    {
        return;
    }

    wrapper->statement_state = statement_wrapper::defining;
    wrapper->use_kind = statement_wrapper::bulk;

    wrapper->use_indicators_v[name]; // create new entry
    wrapper->use_ints_v[name]; // create new entry
}

SOCI_DECL void soci_use_long_long_v(statement_handle st, char const * name)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (cannot_add_elements(*wrapper, statement_wrapper::bulk, false) ||
        name_unique_check_failed(*wrapper, statement_wrapper::bulk, name))
    {
        return;
    }

    wrapper->statement_state = statement_wrapper::defining;
    wrapper->use_kind = statement_wrapper::bulk;

    wrapper->use_indicators_v[name]; // create new entry
    wrapper->use_longlongs_v[name]; // create new entry
}

SOCI_DECL void soci_use_double_v(statement_handle st, char const * name)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (cannot_add_elements(*wrapper, statement_wrapper::bulk, false) ||
        name_unique_check_failed(*wrapper, statement_wrapper::bulk, name))
    {
        return;
    }

    wrapper->statement_state = statement_wrapper::defining;
    wrapper->use_kind = statement_wrapper::bulk;

    wrapper->use_indicators_v[name]; // create new entry
    wrapper->use_doubles_v[name]; // create new entry
}

SOCI_DECL void soci_use_date_v(statement_handle st, char const * name)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (cannot_add_elements(*wrapper, statement_wrapper::bulk, false) ||
        name_unique_check_failed(*wrapper, statement_wrapper::bulk, name))
    {
        return;
    }

    wrapper->statement_state = statement_wrapper::defining;
    wrapper->use_kind = statement_wrapper::bulk;

    wrapper->use_indicators_v[name]; // create new entry
    wrapper->use_dates_v[name]; // create new entry
}

SOCI_DECL void soci_set_use_state(statement_handle st, char const * name, int state)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    typedef std::map<std::string, indicator>::const_iterator iterator;
    iterator const it = wrapper->use_indicators.find(name);
    if (it == wrapper->use_indicators.end())
    {
        wrapper->is_ok = false;
        wrapper->error_message = "Invalid name.";
        return;
    }

    wrapper->is_ok = true;
    wrapper->use_indicators[name] = (state != 0 ? i_ok : i_null);
}

SOCI_DECL void soci_set_use_string(statement_handle st, char const * name, char const * val)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (name_exists_check_failed(*wrapper,
            name, dt_string, statement_wrapper::single, "string"))
    {
        return;
    }

    wrapper->use_indicators[name] = i_ok;
    wrapper->use_strings[name] = val;
}

SOCI_DECL void soci_set_use_int(statement_handle st, char const * name, int val)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (name_exists_check_failed(*wrapper,
            name, dt_integer, statement_wrapper::single, "int"))
    {
        return;
    }

    wrapper->use_indicators[name] = i_ok;
    wrapper->use_ints[name] = val;
}

SOCI_DECL void soci_set_use_long_long(statement_handle st, char const * name, long long val)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (name_exists_check_failed(*wrapper,
            name, dt_long_long, statement_wrapper::single, "long long"))
    {
        return;
    }

    wrapper->use_indicators[name] = i_ok;
    wrapper->use_longlongs[name] = val;
}

SOCI_DECL void soci_set_use_double(statement_handle st, char const * name, double val)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (name_exists_check_failed(*wrapper,
            name, dt_double, statement_wrapper::single, "double"))
    {
        return;
    }

    wrapper->use_indicators[name] = i_ok;
    wrapper->use_doubles[name] = val;
}

SOCI_DECL void soci_set_use_date(statement_handle st, char const * name, char const * val)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (name_exists_check_failed(*wrapper,
            name, dt_date, statement_wrapper::single, "date"))
    {
        return;
    }

    std::tm dt;
    bool const converted = string_to_date(val, dt, *wrapper);
    if (converted == false)
    {
        return;
    }

    wrapper->use_indicators[name] = i_ok;
    wrapper->use_dates[name] = dt;
}

SOCI_DECL int soci_use_get_size_v(statement_handle st)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (wrapper->use_kind != statement_wrapper::bulk)
    {
        wrapper->is_ok = false;
        wrapper->error_message = "No vector use elements.";
        return -1;
    }

    typedef std::map<std::string,
        std::vector<indicator> >::const_iterator iterator;
    iterator const any_element = wrapper->use_indicators_v.begin();
    assert(any_element != wrapper->use_indicators_v.end());

    return static_cast<int>(any_element->second.size());
}

SOCI_DECL void soci_use_resize_v(statement_handle st, int new_size)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (new_size <= 0)
    {
        wrapper->is_ok = false;
        wrapper->error_message = "Invalid size.";
        return;
    }

    if (wrapper->use_kind != statement_wrapper::bulk)
    {
        wrapper->is_ok = false;
        wrapper->error_message = "No vector use elements.";
        return;
    }

    resize_in_map(wrapper->use_indicators_v, new_size);
    resize_in_map(wrapper->use_strings_v, new_size);
    resize_in_map(wrapper->use_ints_v, new_size);
    resize_in_map(wrapper->use_longlongs_v, new_size);
    resize_in_map(wrapper->use_doubles_v, new_size);
    resize_in_map(wrapper->use_dates_v, new_size);

    wrapper->is_ok = true;
}

SOCI_DECL void soci_set_use_state_v(statement_handle st,
    char const * name, int index, int state)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    typedef std::map<std::string, std::vector<indicator> >::iterator iterator;
    iterator const it = wrapper->use_indicators_v.find(name);
    if (it == wrapper->use_indicators_v.end())
    {
        wrapper->is_ok = false;
        wrapper->error_message = "Invalid name.";
        return;
    }

    std::vector<indicator> & v = it->second;
    if (index_check_failed(v, *wrapper, index))
    {
        return;
    }

    v[index] = (state != 0 ? i_ok : i_null);
}

SOCI_DECL void soci_set_use_string_v(statement_handle st,
    char const * name, int index, char const * val)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (name_exists_check_failed(*wrapper,
            name, dt_string, statement_wrapper::bulk, "vector string"))
    {
        return;
    }

    std::vector<std::string> & v = wrapper->use_strings_v[name];
    if (index_check_failed(v, *wrapper, index))
    {
        return;
    }

    wrapper->use_indicators_v[name][index] = i_ok;
    v[index] = val;
}

SOCI_DECL void soci_set_use_int_v(statement_handle st,
    char const * name, int index, int val)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (name_exists_check_failed(*wrapper,
            name, dt_integer, statement_wrapper::bulk, "vector int"))
    {
        return;
    }

    std::vector<int> & v = wrapper->use_ints_v[name];
    if (index_check_failed(v, *wrapper, index))
    {
        return;
    }

    wrapper->use_indicators_v[name][index] = i_ok;
    v[index] = val;
}

SOCI_DECL void soci_set_use_long_long_v(statement_handle st,
    char const * name, int index, long long val)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (name_exists_check_failed(*wrapper,
            name, dt_long_long, statement_wrapper::bulk, "vector long long"))
    {
        return;
    }

    std::vector<long long> & v = wrapper->use_longlongs_v[name];
    if (index_check_failed(v, *wrapper, index))
    {
        return;
    }

    wrapper->use_indicators_v[name][index] = i_ok;
    v[index] = val;
}

SOCI_DECL void soci_set_use_double_v(statement_handle st,
    char const * name, int index, double val)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (name_exists_check_failed(*wrapper,
            name, dt_double, statement_wrapper::bulk, "vector double"))
    {
        return;
    }

    std::vector<double> & v = wrapper->use_doubles_v[name];
    if (index_check_failed(v, *wrapper, index))
    {
        return;
    }

    wrapper->use_indicators_v[name][index] = i_ok;
    v[index] = val;
}

SOCI_DECL void soci_set_use_date_v(statement_handle st,
    char const * name, int index, char const * val)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (name_exists_check_failed(*wrapper,
            name, dt_date, statement_wrapper::bulk, "vector date"))
    {
        return;
    }

    std::vector<std::tm> & v = wrapper->use_dates_v[name];
    if (index_check_failed(v, *wrapper, index))
    {
        return;
    }

    std::tm dt;
    bool const converted = string_to_date(val, dt, *wrapper);
    if (converted == false)
    {
        return;
    }

    wrapper->use_indicators_v[name][index] = i_ok;
    v[index] = dt;
}

SOCI_DECL int soci_get_use_state(statement_handle st, char const * name)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    typedef std::map<std::string, indicator>::const_iterator iterator;
    iterator const it = wrapper->use_indicators.find(name);
    if (it == wrapper->use_indicators.end())
    {
        wrapper->is_ok = false;
        wrapper->error_message = "Invalid name.";
        return 0;
    }

    wrapper->is_ok = true;
    return wrapper->use_indicators[name] == i_ok ? 1 : 0;
}

SOCI_DECL char const * soci_get_use_string(statement_handle st, char const * name)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (name_exists_check_failed(*wrapper,
            name, dt_string, statement_wrapper::bulk, "string"))
    {
        return "";
    }

    return wrapper->use_strings[name].c_str();
}

SOCI_DECL int soci_get_use_int(statement_handle st, char const * name)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (name_exists_check_failed(*wrapper,
            name, dt_integer, statement_wrapper::bulk, "int"))
    {
        return 0;
    }

    return wrapper->use_ints[name];
}

SOCI_DECL long long soci_get_use_long_long(statement_handle st, char const * name)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (name_exists_check_failed(*wrapper,
            name, dt_long_long, statement_wrapper::bulk, "long long"))
    {
        return 0LL;
    }

    return wrapper->use_longlongs[name];
}

SOCI_DECL double soci_get_use_double(statement_handle st, char const * name)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (name_exists_check_failed(*wrapper,
            name, dt_double, statement_wrapper::bulk, "double"))
    {
        return 0.0;
    }

    return wrapper->use_doubles[name];
}

SOCI_DECL char const * soci_get_use_date(statement_handle st, char const * name)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    if (name_exists_check_failed(*wrapper,
            name, dt_date, statement_wrapper::bulk, "date"))
    {
        return "";
    }

    // format is: "YYYY MM DD hh mm ss"
    std::tm const & d = wrapper->use_dates[name];
    std::sprintf(wrapper->date_formatted, "%d %d %d %d %d %d",
        d.tm_year + 1900, d.tm_mon + 1, d.tm_mday,
        d.tm_hour, d.tm_min, d.tm_sec);

    return wrapper->date_formatted;
}

SOCI_DECL void soci_prepare(statement_handle st, char const * query)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    try
    {
        wrapper->statement_state = statement_wrapper::executing;

        // bind all into elements

        int const into_elements = static_cast<int>(wrapper->into_types.size());
        if (wrapper->into_kind == statement_wrapper::single)
        {
            for (int i = 0; i != into_elements; ++i)
            {
                switch (wrapper->into_types[i])
                {
                case dt_string:
                    wrapper->st.exchange(
                        into(wrapper->into_strings[i], wrapper->into_indicators[i]));
                    break;
                case dt_integer:
                    wrapper->st.exchange(
                        into(wrapper->into_ints[i], wrapper->into_indicators[i]));
                    break;
                case dt_long_long:
                    wrapper->st.exchange(
                        into(wrapper->into_longlongs[i], wrapper->into_indicators[i]));
                    break;
                case dt_double:
                    wrapper->st.exchange(
                        into(wrapper->into_doubles[i], wrapper->into_indicators[i]));
                    break;
                case dt_date:
                    wrapper->st.exchange(
                        into(wrapper->into_dates[i], wrapper->into_indicators[i]));
                    break;
                default:
                    assert(false);
                }
            }
        }
        else
        {
            // vector elements
            for (int i = 0; i != into_elements; ++i)
            {
                switch (wrapper->into_types[i])
                {
                case dt_string:
                    wrapper->st.exchange(
                        into(wrapper->into_strings_v[i], wrapper->into_indicators_v[i]));
                    break;
                case dt_integer:
                    wrapper->st.exchange(
                        into(wrapper->into_ints_v[i], wrapper->into_indicators_v[i]));
                    break;
                case dt_long_long:
                    wrapper->st.exchange(
                        into(wrapper->into_longlongs_v[i], wrapper->into_indicators_v[i]));
                    break;
                case dt_double:
                    wrapper->st.exchange(
                        into(wrapper->into_doubles_v[i], wrapper->into_indicators_v[i]));
                    break;
                case dt_date:
                    wrapper->st.exchange(
                        into(wrapper->into_dates_v[i], wrapper->into_indicators_v[i]));
                    break;
                default:
                    assert(false);
                }
            }
        }

        // bind all use elements
        {
            // strings
            typedef std::map<std::string, std::string>::iterator iterator;
            iterator uit = wrapper->use_strings.begin();
            iterator const uend = wrapper->use_strings.end();
            for ( ; uit != uend; ++uit)
            {
                std::string const & use_name = uit->first;
                std::string & use_string = uit->second;
                indicator & use_ind = wrapper->use_indicators[use_name];
                wrapper->st.exchange(use(use_string, use_ind, use_name));
            }
        }
        {
            // ints
            typedef std::map<std::string, int>::iterator iterator;
            iterator uit = wrapper->use_ints.begin();
            iterator const uend = wrapper->use_ints.end();
            for ( ; uit != uend; ++uit)
            {
                std::string const & use_name = uit->first;
                int & use_int = uit->second;
                indicator & use_ind = wrapper->use_indicators[use_name];
                wrapper->st.exchange(use(use_int, use_ind, use_name));
            }
        }
        {
            // longlongs
            typedef std::map<std::string, long long>::iterator iterator;
            iterator uit = wrapper->use_longlongs.begin();
            iterator const uend = wrapper->use_longlongs.end();
            for ( ; uit != uend; ++uit)
            {
                std::string const & use_name = uit->first;
                long long & use_longlong = uit->second;
                indicator & use_ind = wrapper->use_indicators[use_name];
                wrapper->st.exchange(use(use_longlong, use_ind, use_name));
            }
        }
        {
            // doubles
            typedef std::map<std::string, double>::iterator iterator;
            iterator uit = wrapper->use_doubles.begin();
            iterator const uend = wrapper->use_doubles.end();
            for ( ; uit != uend; ++uit)
            {
                std::string const & use_name = uit->first;
                double & use_double = uit->second;
                indicator & use_ind = wrapper->use_indicators[use_name];
                wrapper->st.exchange(use(use_double, use_ind, use_name));
            }
        }
        {
            // dates
            typedef std::map<std::string, std::tm>::iterator iterator;
            iterator uit = wrapper->use_dates.begin();
            iterator const uend = wrapper->use_dates.end();
            for ( ; uit != uend; ++uit)
            {
                std::string const & use_name = uit->first;
                std::tm & use_date = uit->second;
                indicator & use_ind = wrapper->use_indicators[use_name];
                wrapper->st.exchange(use(use_date, use_ind, use_name));
            }
        }

        // bind all use vecctor elements
        {
            // strings
            typedef std::map<std::string,
                std::vector<std::string> >::iterator iterator;
            iterator uit = wrapper->use_strings_v.begin();
            iterator const uend = wrapper->use_strings_v.end();
            for ( ; uit != uend; ++uit)
            {
                std::string const & use_name = uit->first;
                std::vector<std::string> & use_string = uit->second;
                std::vector<indicator> & use_ind =
                    wrapper->use_indicators_v[use_name];
                wrapper->st.exchange(use(use_string, use_ind, use_name));
            }
        }
        {
            // ints
            typedef std::map<std::string,
                std::vector<int> >::iterator iterator;
            iterator uit = wrapper->use_ints_v.begin();
            iterator const uend = wrapper->use_ints_v.end();
            for ( ; uit != uend; ++uit)
            {
                std::string const & use_name = uit->first;
                std::vector<int> & use_int = uit->second;
                std::vector<indicator> & use_ind =
                    wrapper->use_indicators_v[use_name];
                wrapper->st.exchange(use(use_int, use_ind, use_name));
            }
        }
        {
            // longlongs
            typedef std::map<std::string,
                std::vector<long long> >::iterator iterator;
            iterator uit = wrapper->use_longlongs_v.begin();
            iterator const uend = wrapper->use_longlongs_v.end();
            for ( ; uit != uend; ++uit)
            {
                std::string const & use_name = uit->first;
                std::vector<long long> & use_longlong = uit->second;
                std::vector<indicator> & use_ind =
                    wrapper->use_indicators_v[use_name];
                wrapper->st.exchange(use(use_longlong, use_ind, use_name));
            }
        }
        {
            // doubles
            typedef std::map<std::string,
                std::vector<double> >::iterator iterator;
            iterator uit = wrapper->use_doubles_v.begin();
            iterator const uend = wrapper->use_doubles_v.end();
            for ( ; uit != uend; ++uit)
            {
                std::string const & use_name = uit->first;
                std::vector<double> & use_double = uit->second;
                std::vector<indicator> & use_ind =
                    wrapper->use_indicators_v[use_name];
                wrapper->st.exchange(use(use_double, use_ind, use_name));
            }
        }
        {
            // dates
            typedef std::map<std::string,
                std::vector<std::tm> >::iterator iterator;
            iterator uit = wrapper->use_dates_v.begin();
            iterator const uend = wrapper->use_dates_v.end();
            for ( ; uit != uend; ++uit)
            {
                std::string const & use_name = uit->first;
                std::vector<std::tm> & use_date = uit->second;
                std::vector<indicator> & use_ind =
                    wrapper->use_indicators_v[use_name];
                wrapper->st.exchange(use(use_date, use_ind, use_name));
            }
        }

        wrapper->st.alloc();
        wrapper->st.prepare(query);
        wrapper->st.define_and_bind();

        wrapper->is_ok = true;
    }
    catch (std::exception const & e)
    {
        wrapper->is_ok = false;
        wrapper->error_message = e.what();
    }
}

SOCI_DECL int soci_execute(statement_handle st, int withDataExchange)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    try
    {
        bool const gotData = wrapper->st.execute(withDataExchange != 0);

        wrapper->is_ok = true;

        return gotData ? 1 : 0;
    }
    catch (std::exception const & e)
    {
        wrapper->is_ok = false;
        wrapper->error_message = e.what();

        return 0;
    }
}

SOCI_DECL long long soci_get_affected_rows(statement_handle st)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    return wrapper->st.get_affected_rows();
}

SOCI_DECL int soci_fetch(statement_handle st)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    try
    {
        bool const gotData = wrapper->st.fetch();

        wrapper->is_ok = true;

        return gotData ? 1 : 0;
    }
    catch (std::exception const & e)
    {
        wrapper->is_ok = false;
        wrapper->error_message = e.what();

        return 0;
    }
}

SOCI_DECL int soci_got_data(statement_handle st)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    return wrapper->st.got_data() ? 1 : 0;
}

SOCI_DECL int soci_statement_state(statement_handle st)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    return wrapper->is_ok ? 1 : 0;
}

SOCI_DECL char const * soci_statement_error_message(statement_handle st)
{
    statement_wrapper * wrapper = static_cast<statement_wrapper *>(st);

    return wrapper->error_message.c_str();
}
