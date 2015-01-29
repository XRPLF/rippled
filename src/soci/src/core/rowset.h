//
// Copyright (C) 2006-2008 Mateusz Loskot
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_ROWSET_H_INCLUDED
#define SOCI_ROWSET_H_INCLUDED

#include "statement.h"
// std
#include <iterator>
#include <memory>

namespace soci
{

//
// rowset iterator of input category.
//
template <typename T>
class rowset_iterator
{
public:

    // Standard iterator traits

    typedef std::input_iterator_tag iterator_category;
    typedef T   value_type;
    typedef T * pointer;
    typedef T & reference;
    typedef ptrdiff_t difference_type;

    // Constructors

    rowset_iterator()
        : st_(0), define_(0)
    {}

    rowset_iterator(statement & st, T & define)
        : st_(&st), define_(&define)
    {
        assert(0 != st_);
        assert(0 != define_);
        assert(0 != st_->get_backend());

        // Fetch first row to properly initialize iterator
        ++(*this);
    }
    
    // Access operators
    
    reference operator*() const
    {
        return (*define_);
    }

    pointer operator->() const
    {
        return &(operator*());
    }
    
    // Iteration operators

    rowset_iterator & operator++()
    {
        // Fetch next row from dataset
        
        if (st_->fetch() == false)
        {
            // Set iterator to non-derefencable state (pass-the-end)
            st_ = 0;
            define_ = 0;
        }

        return (*this);
    }

    rowset_iterator operator++(int)
    {
        rowset_iterator tmp(*this);
        ++(*this);
        return tmp;
    }

    // Comparison operators

    bool operator==(rowset_iterator const & rhs) const
    {
        return (st_== rhs.st_ && define_ == rhs.define_);
    }

    bool operator!=(rowset_iterator const & rhs) const
    {
        return ((*this == rhs) == false);
    }

private:

    statement * st_;
    T * define_;

}; // class rowset_iterator

namespace details
{

//
// Implementation of rowset
//
template <typename T>
class rowset_impl
{
public:

    typedef rowset_iterator<T> iterator;

    rowset_impl(details::prepare_temp_type const & prep)
        : refs_(1), st_(new statement(prep)), define_(new T())
    {
        assert(0 != st_.get());
        assert(0 != define_.get());

        st_->exchange_for_rowset(into(*define_));
        st_->execute();
    }

    void incRef()
    {
        ++refs_;
    }

    void decRef()
    {
        if (--refs_ == 0)
        {
            delete this;
        }
    }

    iterator begin() const
    {
        // No ownership transfer occurs here
        return iterator(*st_, *define_);
    }

    iterator end() const
    {
        return iterator();
    }

private:

    unsigned int refs_;

    const std::auto_ptr<statement> st_;
    const std::auto_ptr<T> define_;

    // Non-copyable
    rowset_impl(rowset_impl const &);
    rowset_impl & operator=(rowset_impl const &);

}; // class rowset_impl

} // namespace details


//
// rowset is a thin wrapper on statement and provides access to STL-like input iterator.
// The rowset_iterator can be used to easily loop through statement results and
// use STL algorithms accepting input iterators.
//
template <typename T = soci::row>
class rowset
{
public:

    typedef T value_type;
    typedef rowset_iterator<T> iterator;
    typedef rowset_iterator<T> const_iterator;
    
    // this is a conversion constructor
    rowset(details::prepare_temp_type const& prep)
        : pimpl_(new details::rowset_impl<T>(prep))
    {
        assert(0 != pimpl_);
    }

    rowset(rowset const & other)
        : pimpl_(other.pimpl_)
    {
        assert(0 != pimpl_);

        pimpl_->incRef();
    }

    ~rowset()
    {
        assert(0 != pimpl_);

        pimpl_->decRef();
    }

    rowset& operator=(rowset const& rhs)
    {
        assert(0 != pimpl_);
        assert(0 != rhs.pimpl_);

        if (&rhs != this)
        {
            rhs.pimpl_->incRef();
            pimpl_->decRef();
            pimpl_ = rhs.pimpl_;
        }
        return *this;
    }

    const_iterator begin() const
    {
        assert(0 != pimpl_);

        return pimpl_->begin();
    }
    
    const_iterator end() const
    {
        assert(0 != pimpl_);

        return pimpl_->end();
    }

private:

    // Pointer to implementation - the body
    details::rowset_impl<T>* pimpl_;

}; // class rowset

} // namespace soci

#endif // SOCI_ROWSET_H_INCLUDED
