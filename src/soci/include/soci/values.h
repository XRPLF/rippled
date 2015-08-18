//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_VALUES_H_INCLUDED
#define SOCI_VALUES_H_INCLUDED

#include "soci/statement.h"
#include "soci/into-type.h"
#include "soci/use-type.h"
// std
#include <cstddef>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace soci
{

namespace details
{

class copy_base
{
public:
    virtual ~copy_base() {}
};

template <typename T>
struct copy_holder : public copy_base
{
    copy_holder(T const & v) : value_(v) {}

    T value_;
};

} // namespace details

class SOCI_DECL values
{
    friend class details::statement_impl;
    friend class details::into_type<values>;
    friend class details::use_type<values>;

public:

    values() : row_(NULL), currentPos_(0), uppercaseColumnNames_(false) {}

    indicator get_indicator(std::size_t pos) const;
    indicator get_indicator(std::string const & name) const;

    template <typename T>
    T get(std::size_t pos) const
    {
        if (row_ != NULL)
        {
            return row_->get<T>(pos);
        }
        else if (*indicators_[pos] != i_null)
        {
            return get_from_uses<T>(pos);
        }
        else
        {
            std::ostringstream msg;
            msg << "Column at position "
                << static_cast<unsigned long>(pos)
                << " contains NULL value and no default was provided";
            throw soci_error(msg.str());
        }
    }

    template <typename T>
    T get(std::size_t pos, T const & nullValue) const
    {
        if (row_ != NULL)
        {
            return row_->get<T>(pos, nullValue);
        }
        else if (*indicators_[pos] == i_null)
        {
            return nullValue;
        }
        else
        {
            return get_from_uses<T>(pos);
        }
    }

    template <typename T>
    T get(std::string const & name) const
    {
        return row_ != NULL ? row_->get<T>(name) : get_from_uses<T>(name);
    }

    template <typename T>
    T get(std::string const & name, T const & nullValue) const
    {
        return row_ != NULL
            ? row_->get<T>(name, nullValue)
            : get_from_uses<T>(name, nullValue);
    }

    template <typename T>
    values const & operator>>(T & value) const
    {
        if (row_ != NULL)
        {
            // row maintains its own position counter
            // which is automatically reset when needed

            *row_ >> value;
        }
        else if (*indicators_[currentPos_] != i_null)
        {
            // if there is no row object, then the data can be
            // extracted from the locally stored use elements,
            // but for this the position counter has to be maintained
            // as well

            value = get_from_uses<T>(currentPos_);
            ++currentPos_;
        }
        else
        {
            std::ostringstream msg;
            msg << "Column at position "
                << static_cast<unsigned long>(currentPos_)
                << " contains NULL value and no default was provided";
            throw soci_error(msg.str());
        }

        return *this;
    }

    void skip(std::size_t num = 1) const
    {
        if (row_ != NULL)
        {
            row_->skip(num);
        }
        else
        {
            currentPos_ += num;
        }
    }

    void reset_get_counter() const
    {
        if (row_ != NULL)
        {
            row_->reset_get_counter();
        }
        else
        {
            currentPos_ = 0;
        }
    }

    template <typename T>
    void set(std::string const & name, T const & value, indicator indic = i_ok)
    {
        typedef typename type_conversion<T>::base_type base_type;
        if(index_.find(name) == index_.end())
        {
            index_.insert(std::make_pair(name, uses_.size()));

            indicator * pind = new indicator(indic);
            indicators_.push_back(pind);

            base_type baseValue;
            if (indic == i_ok)
            {
                type_conversion<T>::to_base(value, baseValue, *pind);
            }

            details::copy_holder<base_type> * pcopy =
                    new details::copy_holder<base_type>(baseValue);
            deepCopies_.push_back(pcopy);

            uses_.push_back(new details::use_type<base_type>(
                    pcopy->value_, *pind, name));
        }
        else
        {
            size_t index = index_.find(name)->second;
            *indicators_[index] = indic;
            if (indic == i_ok)
            {
                type_conversion<T>::to_base(
                        value,
                        static_cast<details::copy_holder<base_type>*>(deepCopies_[index])->value_,
                        *indicators_[index]);
            }
        }
    }

    template <typename T>
    void set(const T & value, indicator indic = i_ok)
    {
        indicator * pind = new indicator(indic);
        indicators_.push_back(pind);

        typedef typename type_conversion<T>::base_type base_type;
        base_type baseValue;
        type_conversion<T>::to_base(value, baseValue, *pind);

        details::copy_holder<base_type> * pcopy =
            new details::copy_holder<base_type>(baseValue);
        deepCopies_.push_back(pcopy);

        uses_.push_back(new details::use_type<base_type>(
                pcopy->value_, *pind));
    }

    template <typename T>
    values & operator<<(T const & value)
    {
        set(value);
        return *this;
    }

    void uppercase_column_names(bool forceToUpper)
    {
        uppercaseColumnNames_ = forceToUpper;
    }

    std::size_t get_number_of_columns() const
    {
        return row_ ? row_->size() : 0;
    }

    column_properties const& get_properties(std::size_t pos) const;
    column_properties const& get_properties(std::string const &name) const;

private:

    //TODO To make values generally usable outside of type_conversion's,
    // these should be reference counted smart pointers
    row * row_;
    std::vector<details::standard_use_type *> uses_;
    std::map<details::use_type_base *, indicator *> unused_;
    std::vector<indicator *> indicators_;
    std::map<std::string, std::size_t> index_;
    std::vector<details::copy_base *> deepCopies_;

    mutable std::size_t currentPos_;

    bool uppercaseColumnNames_;

    // When type_conversion::to() is called, a values object is created
    // without an underlying row object.  In that case, get_from_uses()
    // returns the underlying field values
    template <typename T>
    T get_from_uses(std::string const & name, T const & nullValue) const
    {
        std::map<std::string, std::size_t>::const_iterator pos = index_.find(name);
        if (pos != index_.end())
        {
            if (*indicators_[pos->second] == i_null)
            {
                return nullValue;
            }

            return get_from_uses<T>(pos->second);
        }
        throw soci_error("Value named " + name + " not found.");
    }

    template <typename T>
    T get_from_uses(std::string const & name) const
    {
        std::map<std::string, std::size_t>::const_iterator pos = index_.find(name);
        if (pos != index_.end())
        {
            return get_from_uses<T>(pos->second);
        }
        throw soci_error("Value named " + name + " not found.");
    }

    template <typename T>
    T get_from_uses(std::size_t pos) const
    {
        details::standard_use_type* u = uses_[pos];

        typedef typename type_conversion<T>::base_type base_type;

        if (dynamic_cast<details::use_type<base_type> *>(u))
        {
            base_type const & baseValue = *static_cast<base_type*>(u->get_data());

            T val;
            indicator ind = *indicators_[pos];
            type_conversion<T>::from_base(baseValue, ind, val);
            return val;
        }
        else
        {
            std::ostringstream msg;
            msg << "Value at position "
                << static_cast<unsigned long>(pos)
                << " was set using a different type"
                   " than the one passed to get()";
            throw soci_error(msg.str());
        }
    }

    row& get_row()
    {
        row_ = new row();
        row_->uppercase_column_names(uppercaseColumnNames_);

        return * row_;
    }

    // this is called by Statement::bind(values)
    void add_unused(details::use_type_base * u, indicator * i)
    {
        static_cast<details::standard_use_type *>(u)->convert_to_base();
        unused_.insert(std::make_pair(u, i));
    }

    // this is called by details::into_type<values>::clean_up()
    // and use_type<values>::clean_up()
    void clean_up()
    {
        delete row_;
        row_ = NULL;

        // delete any uses and indicators which were created  by set() but
        // were not bound by the Statement
        // (bound uses and indicators are deleted in Statement::clean_up())
        for (std::map<details::use_type_base *, indicator *>::iterator pos =
            unused_.begin(); pos != unused_.end(); ++pos)
        {
            delete pos->first;
            delete pos->second;
        }

        for (std::size_t i = 0; i != deepCopies_.size(); ++i)
        {
            delete deepCopies_[i];
        }
    }
};

} // namespace soci

#endif // SOCI_VALUES_H_INCLUDED
