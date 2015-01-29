//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_USE_TYPE_H_INCLUDED
#define SOCI_USE_TYPE_H_INCLUDED

#include "soci-backend.h"
#include "type-ptr.h"
#include "exchange-traits.h"
// std
#include <cstddef>
#include <string>
#include <vector>

namespace soci { namespace details {

class statement_impl;

// this is intended to be a base class for all classes that deal with
// binding input data (and OUT PL/SQL variables)
class SOCI_DECL use_type_base
{
public:
    virtual ~use_type_base() {}

    virtual void bind(statement_impl & st, int & position) = 0;
    virtual void pre_use() = 0;
    virtual void post_use(bool gotData) = 0;
    virtual void clean_up() = 0;

    virtual std::size_t size() const = 0;  // returns the number of elements
};

typedef type_ptr<use_type_base> use_type_ptr;

class SOCI_DECL standard_use_type : public use_type_base
{
public:
    standard_use_type(void* data, exchange_type type,
        bool readOnly, std::string const& name = std::string())
        : data_(data)
        , type_(type)
        , ind_(NULL)
        , readOnly_(readOnly)
        , name_(name)
        , backEnd_(NULL)
    {
        // FIXME: This was added with Ilia's patch
        // https://github.com/SOCI/soci/commit/c166625a28f7c907318134f625ff5acea7d9a1f8
        // but it seems to be a troublemaker, causing duplicated conversions
        //convert_to_base();
    }

    standard_use_type(void* data, exchange_type type, indicator& ind,
        bool readOnly, std::string const& name = std::string())
        : data_(data)
        , type_(type)
        , ind_(&ind)
        , readOnly_(readOnly)
        , name_(name)
        , backEnd_(NULL)
    {
        // FIXME
        //convert_to_base();
    }

    virtual ~standard_use_type();
    virtual void bind(statement_impl & st, int & position);
    std::string get_name() const { return name_; }
    virtual void * get_data() { return data_; }

    // conversion hook (from arbitrary user type to base type)
    virtual void convert_to_base() {}
    virtual void convert_from_base() {}

protected:
    virtual void pre_use();

private:
    virtual void post_use(bool gotData);
    virtual void clean_up();
    virtual std::size_t size() const { return 1; }

    void* data_;
    exchange_type type_;
    indicator* ind_;
    bool readOnly_;
    std::string name_;

    standard_use_type_backend* backEnd_;
};

class SOCI_DECL vector_use_type : public use_type_base
{
public:
    vector_use_type(void* data, exchange_type type,
        std::string const& name = std::string())
        : data_(data)
        , type_(type)
        , ind_(NULL)
        , name_(name)
        , backEnd_(NULL)
    {}

    vector_use_type(void* data, exchange_type type,
        std::vector<indicator> const& ind,
        std::string const& name = std::string())
        : data_(data)
        , type_(type)
        , ind_(&ind)
        , name_(name)
        , backEnd_(NULL)
    {}

    ~vector_use_type();

private:
    virtual void bind(statement_impl& st, int & position);
    virtual void pre_use();
    virtual void post_use(bool) { /* nothing to do */ }
    virtual void clean_up();
    virtual std::size_t size() const;

    void* data_;
    exchange_type type_;
    std::vector<indicator> const* ind_;
    std::string name_;

    vector_use_type_backend * backEnd_;

    virtual void convert_to_base() {}
};

// implementation for the basic types (those which are supported by the library
// out of the box without user-provided conversions)

template <typename T>
class use_type : public standard_use_type
{
public:
    use_type(T& t, std::string const& name = std::string())
        : standard_use_type(&t,
            static_cast<exchange_type>(exchange_traits<T>::x_type), false, name)
    {}
    
    use_type(T const& t, std::string const& name = std::string())
        : standard_use_type(const_cast<T*>(&t),
            static_cast<exchange_type>(exchange_traits<T>::x_type), true, name)
    {}
    
    use_type(T& t, indicator& ind, std::string const& name = std::string())
        : standard_use_type(&t,
            static_cast<exchange_type>(exchange_traits<T>::x_type), ind, false, name)
    {}
    
    use_type(T const& t, indicator& ind, std::string const& name = std::string())
        : standard_use_type(const_cast<T*>(&t),
            static_cast<exchange_type>(exchange_traits<T>::x_type), ind, false, name)
    {}
};

template <typename T>
class use_type<std::vector<T> > : public vector_use_type
{
public:
    use_type(std::vector<T>& v, std::string const& name = std::string())
        : vector_use_type(&v,
            static_cast<exchange_type>(exchange_traits<T>::x_type), name)
    {}
    
    use_type(std::vector<T> const& v, std::string const& name = std::string())
        : vector_use_type(const_cast<std::vector<T>*>(&v),
            static_cast<exchange_type>(exchange_traits<T>::x_type), name)
    {}
    
    use_type(std::vector<T>& v, std::vector<indicator> const& ind,
        std::string const& name = std::string())
        : vector_use_type(&v,
            static_cast<exchange_type>(exchange_traits<T>::x_type), ind, name)
    {}
    
    use_type(std::vector<T> const& v, std::vector<indicator> const& ind,
        std::string const& name = std::string())
        : vector_use_type(const_cast<std::vector<T> *>(&v),
            static_cast<exchange_type>(exchange_traits<T>::x_type), ind, name)
    {}
};

// helper dispatchers for basic types

template <typename T>
use_type_ptr do_use(T & t, std::string const & name, basic_type_tag)
{
    return use_type_ptr(new use_type<T>(t, name));
}

template <typename T>
use_type_ptr do_use(T const & t, std::string const & name, basic_type_tag)
{
    return use_type_ptr(new use_type<T>(t, name));
}

template <typename T>
use_type_ptr do_use(T & t, indicator & ind,
    std::string const & name, basic_type_tag)
{
    return use_type_ptr(new use_type<T>(t, ind, name));
}

template <typename T>
use_type_ptr do_use(T const & t, indicator & ind,
    std::string const & name, basic_type_tag)
{
    return use_type_ptr(new use_type<T>(t, ind, name));
}

template <typename T>
use_type_ptr do_use(T & t, std::vector<indicator> & ind,
    std::string const & name, basic_type_tag)
{
    return use_type_ptr(new use_type<T>(t, ind, name));
}

template <typename T>
use_type_ptr do_use(T const & t, std::vector<indicator> & ind,
    std::string const & name, basic_type_tag)
{
    return use_type_ptr(new use_type<T>(t, ind, name));
}

} // namespace details

} // namesapce soci

#endif // SOCI_USE_TYPE_H_INCLUDED
