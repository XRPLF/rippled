//
// Copyright (C) 2004-2016 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_TYPE_CONVERSION_H_INCLUDED
#define SOCI_TYPE_CONVERSION_H_INCLUDED

#include "soci/type-conversion-traits.h"
#include "soci/into-type.h"
#include "soci/use-type.h"
// std
#include <cstddef>
#include <string>
#include <vector>

namespace soci
{

namespace details
{

// this class is used to ensure correct order of construction
// of into_type and use_type elements that use type_conversion

template <typename T>
struct base_value_holder
{
    base_value_holder()
      : ownInd_(i_ok)
    {}

    typename type_conversion<T>::base_type val_;
    indicator ownInd_;
};

// Automatically create into_type from a type_conversion

template <typename T>
class conversion_into_type
    : private base_value_holder<T>,
      public into_type<typename type_conversion<T>::base_type>
{
public:
    typedef typename type_conversion<T>::base_type base_type;

    conversion_into_type(T & value)
        : into_type<base_type>(base_value_holder<T>::val_, base_value_holder<T>::ownInd_)
        , value_(value)
        , ind_(base_value_holder<T>::ownInd_)
    {
    }

    conversion_into_type(T & value, indicator & ind)
        : into_type<base_type>(base_value_holder<T>::val_, ind)
        , value_(value)
        , ind_(ind)
    {
    }


private:
    void convert_from_base() SOCI_OVERRIDE
    {
        type_conversion<T>::from_base(
            base_value_holder<T>::val_, ind_, value_);
    }

    T & value_;

    // ind_ refers to either ownInd_, or the one provided by the user
    // in any case, ind_ refers to some valid indicator
    // and can be used by conversion routines
    indicator & ind_;

    SOCI_NOT_COPYABLE(conversion_into_type)
};

// Automatically create use_type from a type_conversion

template <typename T>
class conversion_use_type
    : private base_value_holder<T>,
      public use_type<typename type_conversion<T>::base_type>
{
public:
    typedef typename type_conversion<T>::base_type base_type;

    conversion_use_type(T & value, std::string const & name = std::string())
        : use_type<base_type>(base_value_holder<T>::val_, base_value_holder<T>::ownInd_, name)
        , value_(value)
        , ind_(base_value_holder<T>::ownInd_)
        , readOnly_(false)
    {
        // TODO: likely to be removed (SHA: c166625a28f7c907318134f625ff5acea7d9a1f8)
        //convert_to_base();
    }

    conversion_use_type(T const & value, std::string const & name = std::string())
        : use_type<base_type>(base_value_holder<T>::val_, base_value_holder<T>::ownInd_, name)
        , value_(const_cast<T &>(value))
        , ind_(base_value_holder<T>::ownInd_)
        , readOnly_(true)
    {
        // TODO: likely to be removed (SHA: c166625a28f7c907318134f625ff5acea7d9a1f8)
        //convert_to_base();
    }

    conversion_use_type(T & value, indicator & ind,
            std::string const & name = std::string())
        : use_type<base_type>(base_value_holder<T>::val_, ind, name)
        , value_(value)
        , ind_(ind)
        , readOnly_(false)
    {
        // TODO: likely to be removed (SHA: c166625a28f7c907318134f625ff5acea7d9a1f8)
        //convert_to_base();
    }

    conversion_use_type(T const & value, indicator & ind,
            std::string const & name = std::string())
        : use_type<base_type>(base_value_holder<T>::val_, ind, name)
        , value_(const_cast<T &>(value))
        , ind_(ind)
        , readOnly_(true)
    {
        // TODO: likely to be removed (SHA: c166625a28f7c907318134f625ff5acea7d9a1f8)
        //convert_to_base();
    }

    void convert_from_base() SOCI_OVERRIDE
    {
        // NOTE:
        // readOnly_ flag indicates that use_type object has been generated
        // based on non-const object passed by user as input argument.
        // For const objects, this is effectively no-op conversion.
        // See standard_use_type::post_use() for more details.

        if (readOnly_ == false)
        {
            type_conversion<T>::from_base(
                base_value_holder<T>::val_, ind_, value_);
        }
    }

    void convert_to_base() SOCI_OVERRIDE
    {
        type_conversion<T>::to_base(value_,
            base_value_holder<T>::val_, ind_);
    }

private:
    T & value_;

    // ind_ refers to either ownInd_, or the one provided by the user
    // in any case, ind_ refers to some valid indicator
    // and can be used by conversion routines
    indicator & ind_;

    bool readOnly_;

    SOCI_NOT_COPYABLE(conversion_use_type)
};

// this class is used to ensure correct order of construction
// of vector based into_type and use_type elements that use type_conversion

template <typename T>
struct base_vector_holder
{
    base_vector_holder(std::size_t sz = 0) : vec_(sz) {}
    mutable std::vector<typename type_conversion<T>::base_type> vec_;
};

// Automatically create a std::vector based into_type from a type_conversion

template <typename T>
class conversion_into_type<std::vector<T> >
    : private base_vector_holder<T>,
      public into_type<std::vector<typename type_conversion<T>::base_type> >
{
public:
    typedef typename std::vector
        <
            typename type_conversion<T>::base_type
        > base_type;

    conversion_into_type(std::vector<T> & value,
        std::size_t begin = 0, std::size_t * end = NULL)
        : base_vector_holder<T>(value.size()),
        into_type<base_type>(
            base_vector_holder<T>::vec_, ownInd_, begin, end),
        value_(value),
        ownInd_(),
        ind_(ownInd_),
        begin_(begin),
        end_(end)
    {
        user_ranges_ = end != NULL;
    }

    conversion_into_type(std::vector<T> & value, std::vector<indicator> & ind,
        std::size_t begin = 0, std::size_t * end = NULL)
        : base_vector_holder<T>(value.size()),
        into_type<base_type>(
            base_vector_holder<T>::vec_, ind, begin, end),
        value_(value),
        ind_(ind),
        begin_(begin),
        end_(end)
    {
        user_ranges_ = end != NULL;
    }

    std::size_t size() const SOCI_OVERRIDE
    {
        // the user might have resized his vector in the meantime
        // -> synchronize the base-value mirror to have the same size

        std::size_t const userSize = value_.size();
        base_vector_holder<T>::vec_.resize(userSize);

        return into_type<base_type>::size();
    }

    void resize(std::size_t sz) SOCI_OVERRIDE
    {
        into_type<base_type>::resize(sz);

        std::size_t actual_size = base_vector_holder<T>::vec_.size();
        value_.resize(actual_size);
        ind_.resize(actual_size);
    }

private:
    void convert_from_base() SOCI_OVERRIDE
    {
        if (user_ranges_)
        {
            for (std::size_t i = begin_; i != *end_; ++i)
            {
                type_conversion<T>::from_base(
                    base_vector_holder<T>::vec_[i], ind_[i], value_[i]);
            }
        }
        else
        {
            std::size_t const sz = base_vector_holder<T>::vec_.size();

            for (std::size_t i = 0; i != sz; ++i)
            {
                type_conversion<T>::from_base(
                    base_vector_holder<T>::vec_[i], ind_[i], value_[i]);
            }
        }
    }

    std::vector<T> & value_;

    std::vector<indicator> ownInd_;

    // ind_ refers to either ownInd_, or the one provided by the user
    // in any case, ind_ refers to some valid vector of indicators
    // and can be used by conversion routines
    std::vector<indicator> & ind_;

    std::size_t begin_;
    std::size_t * end_;
    bool user_ranges_;

    SOCI_NOT_COPYABLE(conversion_into_type)
};


// Automatically create a std::vector based use_type from a type_conversion

template <typename T>
class conversion_use_type<std::vector<T> >
     : private base_vector_holder<T>,
       public use_type<std::vector<typename type_conversion<T>::base_type> >
{
public:
    typedef typename std::vector
        <
            typename type_conversion<T>::base_type
        > base_type;

    conversion_use_type(std::vector<T> & value,
        std::string const & name=std::string())
        : base_vector_holder<T>(value.size()),
        use_type<base_type>(
            base_vector_holder<T>::vec_, ownInd_, 0, NULL, name),
        value_(value),
        ownInd_(),
        ind_(ownInd_),
        begin_(0),
        end_(NULL),
        user_ranges_(false)
    {
    }

    conversion_use_type(std::vector<T> & value,
        std::size_t begin, std::size_t * end,
        std::string const & name=std::string())
        : base_vector_holder<T>(value.size()),
        use_type<base_type>(
            base_vector_holder<T>::vec_, ownInd_, begin, end, name),
        value_(value),
        ownInd_(),
        ind_(ownInd_),
        begin_(begin),
        end_(end)
    {
        user_ranges_ = end != NULL;
    }

    conversion_use_type(std::vector<T> & value,
        std::vector<indicator> & ind,
        std::string const & name = std::string())
        : base_vector_holder<T>(value.size()),
        use_type<base_type>(
            base_vector_holder<T>::vec_, ind, 0, NULL, name),
        value_(value),
        ind_(ind),
        begin_(0),
        end_(NULL),
        user_ranges_(false)
    {
    }

    conversion_use_type(std::vector<T> & value,
        std::vector<indicator> & ind,
        std::size_t begin, std::size_t * end,
        std::string const & name = std::string())
        : base_vector_holder<T>(value.size()),
        use_type<base_type>(
            base_vector_holder<T>::vec_, ind, begin, end, name),
        value_(value),
        ind_(ind),
        begin_(begin),
        end_(end)
    {
        user_ranges_ = end != NULL;
    }

private:
    void convert_from_base()
    {
        std::size_t const sz = base_vector_holder<T>::vec_.size();
        value_.resize(sz);
        ind_.resize(sz);

        if (user_ranges_)
        {
            for (std::size_t i = begin_; i != *end_; ++i)
            {
                type_conversion<T>::from_base(
                    base_vector_holder<T>::vec_[i], value_[i], ind_[i]);
            }
        }
        else
        {
            for (std::size_t i = 0; i != sz; ++i)
            {
                type_conversion<T>::from_base(
                    base_vector_holder<T>::vec_[i], value_[i], ind_[i]);
            }
        }
    }

    void convert_to_base() SOCI_OVERRIDE
    {
        std::size_t const sz = value_.size();
        base_vector_holder<T>::vec_.resize(sz);
        ind_.resize(sz);

        if (user_ranges_)
        {
            for (std::size_t i = begin_; i != *end_; ++i)
            {
                type_conversion<T>::to_base(value_[i],
                    base_vector_holder<T>::vec_[i], ind_[i]);
            }
        }
        else
        {
            for (std::size_t i = 0; i != sz; ++i)
            {
                type_conversion<T>::to_base(value_[i],
                    base_vector_holder<T>::vec_[i], ind_[i]);
            }
        }
    }

    std::vector<T> & value_;

    std::vector<indicator> ownInd_;

    // ind_ refers to either ownInd_, or the one provided by the user
    // in any case, ind_ refers to some valid vector of indicators
    // and can be used by conversion routines
    std::vector<indicator> & ind_;

    std::size_t begin_;
    std::size_t * end_;
    bool user_ranges_;

    SOCI_NOT_COPYABLE(conversion_use_type)
};

template <typename T>
into_type_ptr do_into(T & t, user_type_tag)
{
    return into_type_ptr(new conversion_into_type<T>(t));
}

template <typename T>
into_type_ptr do_into(T & t, indicator & ind, user_type_tag)
{
    return into_type_ptr(new conversion_into_type<T>(t, ind));
}

template <typename T>
into_type_ptr do_into(std::vector<T> & t,
    std::size_t begin, size_t * end, user_type_tag)
{
    return into_type_ptr(
        new conversion_into_type<std::vector<T> >(t, begin, end));
}

template <typename T>
into_type_ptr do_into(std::vector<T> & t, std::vector<indicator> & ind,
    std::size_t begin, size_t * end, user_type_tag)
{
    return into_type_ptr(
        new conversion_into_type<std::vector<T> >(t, ind, begin, end));
}

template <typename T>
use_type_ptr do_use(T & t, std::string const & name, user_type_tag)
{
    return use_type_ptr(new conversion_use_type<T>(t, name));
}

template <typename T>
use_type_ptr do_use(T const & t, std::string const & name, user_type_tag)
{
    return use_type_ptr(new conversion_use_type<T>(t, name));
}

template <typename T>
use_type_ptr do_use(T & t, indicator & ind,
    std::string const & name, user_type_tag)
{
    return use_type_ptr(new conversion_use_type<T>(t, ind, name));
}

template <typename T>
use_type_ptr do_use(T const & t, indicator & ind,
    std::string const & name, user_type_tag)
{
    return use_type_ptr(new conversion_use_type<T>(t, ind, name));
}

template <typename T>
use_type_ptr do_use(std::vector<T> & t,
    std::size_t begin, size_t * end,
    std::string const & name, user_type_tag)
{
    return use_type_ptr(
        new conversion_use_type<std::vector<T> >(t, begin, end, name));
}

template <typename T>
use_type_ptr do_use(const std::vector<T> & t,
    std::size_t begin, size_t * end,
    std::string const & name, user_type_tag)
{
    return use_type_ptr(
        new conversion_use_type<std::vector<T> >(t, begin, end, name));
}

template <typename T>
use_type_ptr do_use(std::vector<T> & t, std::vector<indicator> & ind,
    std::size_t begin, size_t * end,
    std::string const & name, user_type_tag)
{
    return use_type_ptr(
        new conversion_use_type<std::vector<T> >(t, ind, begin, end, name));
}

template <typename T>
use_type_ptr do_use(const std::vector<T> & t, std::vector<indicator> & ind,
    std::size_t begin, size_t * end,
    std::string const & name, user_type_tag)
{
    return use_type_ptr(
        new conversion_use_type<std::vector<T> >(t, ind, begin, end, name));
}

} // namespace details

} // namespace soci

#endif // SOCI_TYPE_CONVERSION_H_INCLUDED
