//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_TYPE_CONVERSION_H_INCLUDED
#define SOCI_TYPE_CONVERSION_H_INCLUDED

#include "type-conversion-traits.h"
#include "into-type.h"
#include "use-type.h"
// std
#include <cassert>
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
    typename type_conversion<T>::base_type val_;
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
        : into_type<base_type>(details::base_value_holder<T>::val_, ownInd_)
        , value_(value)
        , ownInd_()
        , ind_(ownInd_)
    {
        assert(ownInd_ == ind_);
    }
    
    conversion_into_type(T & value, indicator & ind)
        : into_type<base_type>(details::base_value_holder<T>::val_, ind)
        , value_(value)
        , ownInd_(ind) // unused, just keep the pair of indicator(s) consistent
        , ind_(ind)
    {
        assert(ownInd_ == ind_);
    }


private:
    void convert_from_base()
    {
        type_conversion<T>::from_base(
            details::base_value_holder<T>::val_, ind_, value_);
    }

    T & value_;

    indicator ownInd_;

    // ind_ refers to either ownInd_, or the one provided by the user
    // in any case, ind_ refers to some valid indicator
    // and can be used by conversion routines
    indicator & ind_;
};

// Automatically create use_type from a type_conversion

template <typename T>
class conversion_use_type
    : private details::base_value_holder<T>,
      public use_type<typename type_conversion<T>::base_type>
{
public:
    typedef typename type_conversion<T>::base_type base_type;

    conversion_use_type(T & value, std::string const & name = std::string())
        : use_type<base_type>(details::base_value_holder<T>::val_, ownInd_, name)
        , value_(value)
        , ownInd_()
        , ind_(ownInd_)
        , readOnly_(false)
    {
        assert(ownInd_ == ind_);

        // TODO: likely to be removed (SHA: c166625a28f7c907318134f625ff5acea7d9a1f8)
        //convert_to_base();
    }
    
    conversion_use_type(T const & value, std::string const & name = std::string())
        : use_type<base_type>(details::base_value_holder<T>::val_, ownInd_, name)
        , value_(const_cast<T &>(value))
        , ownInd_()
        , ind_(ownInd_)
        , readOnly_(true)
    {
        assert(ownInd_ == ind_);

        // TODO: likely to be removed (SHA: c166625a28f7c907318134f625ff5acea7d9a1f8)
        //convert_to_base();
    }
    
    conversion_use_type(T & value, indicator & ind,
            std::string const & name = std::string())
        : use_type<base_type>(details::base_value_holder<T>::val_, ind, name)
        , value_(value)
        , ind_(ind)
        , readOnly_(false)
    {
        // TODO: likely to be removed (SHA: c166625a28f7c907318134f625ff5acea7d9a1f8)
        //convert_to_base();
    }
    
    conversion_use_type(T const & value, indicator & ind,
            std::string const & name = std::string())
        : use_type<base_type>(details::base_value_holder<T>::val_, ind, name)
        , value_(const_cast<T &>(value))
        , ind_(ind)
        , readOnly_(true)
    {
        // TODO: likely to be removed (SHA: c166625a28f7c907318134f625ff5acea7d9a1f8)
        //convert_to_base();
    }

    void convert_from_base()
    {
        // NOTE:
        // readOnly_ flag indicates that use_type object has been generated
        // based on non-const object passed by user as input argument.
        // For const objects, this is effectively no-op conversion.
        // See standard_use_type::post_use() for more details.

        if (readOnly_ == false)
        {
            type_conversion<T>::from_base(
                details::base_value_holder<T>::val_, ind_, value_);
        }
    }

    void convert_to_base()
    {
        type_conversion<T>::to_base(value_,
            details::base_value_holder<T>::val_, ind_);
    }

private:
    T & value_;

    indicator ownInd_;

    // ind_ refers to either ownInd_, or the one provided by the user
    // in any case, ind_ refers to some valid indicator
    // and can be used by conversion routines
    indicator & ind_;

    bool readOnly_;
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
    : private details::base_vector_holder<T>,
      public into_type<std::vector<typename type_conversion<T>::base_type> >
{
public:
    typedef typename std::vector
        <
            typename type_conversion<T>::base_type
        > base_type;

    conversion_into_type(std::vector<T> & value)
        : details::base_vector_holder<T>(value.size())
        , into_type<base_type>(details::base_vector_holder<T>::vec_, ownInd_)
        , value_(value)
        , ownInd_()
        , ind_(ownInd_)
    {
        assert(ownInd_ == ind_);
    }

    conversion_into_type(std::vector<T> & value, std::vector<indicator> & ind)
        : details::base_vector_holder<T>(value.size())
        , into_type<base_type>(details::base_vector_holder<T>::vec_, ind)
        , value_(value)
        , ind_(ind)
    {}

    virtual std::size_t size() const
    {
        // the user might have resized his vector in the meantime
        // -> synchronize the base-value mirror to have the same size

        std::size_t const userSize = value_.size();
        details::base_vector_holder<T>::vec_.resize(userSize);
        return userSize;
    }

    virtual void resize(std::size_t sz)
    {
        value_.resize(sz);
        ind_.resize(sz);
        details::base_vector_holder<T>::vec_.resize(sz);
    }

private:
    void convert_from_base()
    {
        std::size_t const sz = details::base_vector_holder<T>::vec_.size();

        for (std::size_t i = 0; i != sz; ++i)
        {
            type_conversion<T>::from_base(
                details::base_vector_holder<T>::vec_[i], ind_[i], value_[i]);
        }
    }

    std::vector<T> & value_;

    std::vector<indicator> ownInd_;

    // ind_ refers to either ownInd_, or the one provided by the user
    // in any case, ind_ refers to some valid vector of indicators
    // and can be used by conversion routines
    std::vector<indicator> & ind_;
};


// Automatically create a std::vector based use_type from a type_conversion

template <typename T>
class conversion_use_type<std::vector<T> >
     : private details::base_vector_holder<T>,
       public use_type<std::vector<typename type_conversion<T>::base_type> >
{
public:
    typedef typename std::vector
        <
            typename type_conversion<T>::base_type
        > base_type;

    conversion_use_type(std::vector<T> & value,
            std::string const & name=std::string())
        : details::base_vector_holder<T>(value.size())
        , use_type<base_type>(
            details::base_vector_holder<T>::vec_, ownInd_, name)
        , value_(value)
        , ownInd_()
        , ind_(ownInd_)
    {
        assert(ownInd_ == ind_);
    }

    conversion_use_type(std::vector<T> & value,
            std::vector<indicator> & ind,
            std::string const & name = std::string())
        : details::base_vector_holder<T>(value.size())
        , use_type<base_type>(
            details::base_vector_holder<T>::vec_, ind, name)
        , value_(value)
        , ind_(ind)
    {}

private:
    void convert_from_base()
    {
        std::size_t const sz = details::base_vector_holder<T>::vec_.size();
        value_.resize(sz);
        ind_.resize(sz);
        for (std::size_t i = 0; i != sz; ++i)
        {
            type_conversion<T>::from_base(
                details::base_vector_holder<T>::vec_[i], value_[i], ind_[i]);
        }
    }

    void convert_to_base()
    {
        std::size_t const sz = value_.size();
        details::base_vector_holder<T>::vec_.resize(sz);
        ind_.resize(sz);
        for (std::size_t i = 0; i != sz; ++i)
        {
            type_conversion<T>::to_base(value_[i],
                details::base_vector_holder<T>::vec_[i], ind_[i]);
        }
    }

    std::vector<T> & value_;

    std::vector<indicator> ownInd_;

    // ind_ refers to either ownInd_, or the one provided by the user
    // in any case, ind_ refers to some valid vector of indicators
    // and can be used by conversion routines
    std::vector<indicator> & ind_;
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

} // namespace details

} // namespace soci

#endif // SOCI_TYPE_CONVERSION_H_INCLUDED
