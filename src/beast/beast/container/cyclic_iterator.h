//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Based on work with these copyrights:
        Copyright Carl Philipp Reh 2009 - 2013.
        Copyright Philipp Middendorf 2009 - 2013.
        Distributed under the Boost Software License, Version 1.0.
        (See accompanying file LICENSE_1_0.txt or copy at
        http://www.boost.org/LICENSE_1_0.txt)

    Original code taken from
        https://github.com/freundlich/fcppt

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef BEAST_CONTAINER_CYCLIC_ITERATOR_H_INCLUDED
#define BEAST_CONTAINER_CYCLIC_ITERATOR_H_INCLUDED

#include <iterator>
#include <boost/iterator/iterator_facade.hpp>

namespace beast {

//
// cyclic_iterator_fwd.hpp
//

template<
        typename ContainerIterator
>
class cyclic_iterator;

//
// cyclic_iterator_category.hpp
//

namespace detail
{

template<
        typename SourceCategory
>
struct cyclic_iterator_category;

template<>
struct cyclic_iterator_category<
        std::forward_iterator_tag
>
{
        typedef std::forward_iterator_tag type;
};

template<>
struct cyclic_iterator_category<
        std::bidirectional_iterator_tag
>
{
        typedef std::bidirectional_iterator_tag type;
};

template<>
struct cyclic_iterator_category<
        std::random_access_iterator_tag
>
{
        typedef std::bidirectional_iterator_tag type;
};

}

//
// cyclic_iterator_base.hpp
//

namespace detail
{

template<
        typename ContainerIterator
>
struct cyclic_iterator_base
{
        typedef boost::iterator_facade<
                cyclic_iterator<
                        ContainerIterator
                >,
                typename std::iterator_traits<
                        ContainerIterator
                >::value_type,
                typename detail::cyclic_iterator_category<
                        typename std::iterator_traits<
                                ContainerIterator
                        >::iterator_category
                >::type,
                typename std::iterator_traits<
                        ContainerIterator
                >::reference
        > type;
};

}

//
// cyclic_iterator_decl.hpp 
//

/**
\brief An iterator adaptor that cycles through a range

\ingroup fcpptmain

\tparam ContainerIterator The underlying iterator which must be at least a
forward iterator

A cyclic iterator can be useful in cases where you want <code>end()</code> to
become <code>begin()</code> again. For example, imagine a cycling through a
list of items which means if you skip over the last, you will return to the
first one.

This class can only increment or decrement its underlying iterator, random
access is not supported. The iterator category will be at most bidirectional.
It inherits all capabilities from <code>boost::iterator_facade</code> which
means that it will have the usual iterator operations with their semantics.

Here is a short example demonstrating its use.

\snippet cyclic_iterator.cpp cyclic_iterator
*/
template<
    typename ContainerIterator
>
class cyclic_iterator
:
    public detail::cyclic_iterator_base<
        ContainerIterator
    >::type
{
public:
    /**
    \brief The base type which is a <code>boost::iterator_facade</code>
    */
    typedef typename detail::cyclic_iterator_base<
        ContainerIterator
    >::type base_type;

    /**
    \brief The underlying iterator type
    */
    typedef ContainerIterator container_iterator_type;

    /**
    \brief The value type adapted from \a ContainerIterator
    */
    typedef typename base_type::value_type value_type;

    /**
    \brief The reference type adapted from \a ContainerIterator
    */
    typedef typename base_type::reference reference;

    /**
    \brief The pointer type adapted from \a ContainerIterator
    */
    typedef typename base_type::pointer pointer;

    /**
    \brief The difference type adapted from \a ContainerIterator
    */
    typedef typename base_type::difference_type difference_type;

    /**
    \brief The iterator category, either Forward or Bidirectional
    */
    typedef typename base_type::iterator_category iterator_category;

    /**
    \brief Creates a singular iterator
    */
    cyclic_iterator();

    /**
    \brief Copy constructs from another cyclic iterator

    Copy constructs from another cyclic iterator \a other. This only works
    if the underlying iterators are convertible.

    \param other The iterator to copy construct from
    */
    template<
        typename OtherIterator
    >
    explicit
    cyclic_iterator(
        cyclic_iterator<OtherIterator> const &other
    );

    /**
    \brief Constructs a new cyclic iterator

    Constructs a new cyclic iterator, starting at \a it, inside
    a range from \a begin to \a end.

    \param pos The start of the iterator
    \param begin The beginning of the range
    \param end The end of the range

    \warning The behaviour is undefined if \a pos isn't between \a begin
    and \a end. Also, the behaviour is undefined, if \a begin and \a end
    don't form a valid range.
    */
    cyclic_iterator(
        container_iterator_type const &pos,
        container_iterator_type const &begin,
        container_iterator_type const &end
    );

    /**
    \brief Assigns from another cyclic iterator

    Assigns from another cyclic iterator \a other. This only works if the
    underlying iterators are convertible.

    \param other The iterator to assign from

    \return <code>*this</code>
    */
    template<
        typename OtherIterator
    >
    cyclic_iterator<ContainerIterator> &
    operator=(
        cyclic_iterator<OtherIterator> const &other
    );

    /**
    \brief Returns the beginning of the range
    */
    container_iterator_type
    begin() const;

    /**
    \brief Returns the end of the range
    */
    container_iterator_type
    end() const;

    /**
    \brief Returns the underlying iterator
    */
    container_iterator_type
    get() const;
private:
    friend class boost::iterator_core_access;

    void
    increment();

    void
    decrement();

    bool
    equal(
        cyclic_iterator const &
    ) const;

    reference
    dereference() const;

    difference_type
    distance_to(
        cyclic_iterator const &
    ) const;
private:
    container_iterator_type
        it_,
        begin_,
        end_;
};

//
// cyclic_iterator_impl.hpp
//

template<
        typename ContainerIterator
>
cyclic_iterator<
        ContainerIterator
>::cyclic_iterator()
:
        it_(),
        begin_(),
        end_()
{
}

template<
        typename ContainerIterator
>
template<
        typename OtherIterator
>
cyclic_iterator<
        ContainerIterator
>::cyclic_iterator(
        cyclic_iterator<
                OtherIterator
        > const &_other
)
:
        it_(
                _other.it_
        ),
        begin_(
                _other.begin_
        ),
        end_(
                _other.end_
        )
{
}

template<
        typename ContainerIterator
>
cyclic_iterator<
        ContainerIterator
>::cyclic_iterator(
        container_iterator_type const &_it,
        container_iterator_type const &_begin,
        container_iterator_type const &_end
)
:
        it_(
                _it
        ),
        begin_(
                _begin
        ),
        end_(
                _end
        )
{
}

template<
        typename ContainerIterator
>
template<
        typename OtherIterator
>
cyclic_iterator<
        ContainerIterator
> &
cyclic_iterator<
        ContainerIterator
>::operator=(
        cyclic_iterator<
                OtherIterator
        > const &_other
)
{
        it_ = _other.it_;

        begin_ = _other.begin_;

        end_ = _other.end_;

        return *this;
}

template<
        typename ContainerIterator
>
typename cyclic_iterator<
        ContainerIterator
>::container_iterator_type
cyclic_iterator<
        ContainerIterator
>::begin() const
{
        return begin_;
}

template<
        typename ContainerIterator
>
typename cyclic_iterator<
        ContainerIterator
>::container_iterator_type
cyclic_iterator<
        ContainerIterator
>::end() const
{
        return end_;
}

template<
        typename ContainerIterator
>
typename cyclic_iterator<
        ContainerIterator
>::container_iterator_type
cyclic_iterator<
        ContainerIterator
>::get() const
{
        return it_;
}

template<
        typename ContainerIterator
>
void
cyclic_iterator<
        ContainerIterator
>::increment()
{
        if(
                begin_ != end_
                && ++it_ == end_
        )
                it_ = begin_;
}

template<
        typename ContainerIterator
>
void
cyclic_iterator<
        ContainerIterator
>::decrement()
{
        if(
                begin_ == end_
        )
                return;

        if(
                it_ == begin_
        )
                it_ =
                        std::prev(
                                end_
                        );
        else
                --it_;
}

template<
        typename ContainerIterator
>
bool
cyclic_iterator<
        ContainerIterator
>::equal(
        cyclic_iterator const &_other
) const
{
        return it_ == _other.it;
}

template<
        typename ContainerIterator
>
typename cyclic_iterator<
        ContainerIterator
>::reference
cyclic_iterator<
        ContainerIterator
>::dereference() const
{
        return *it_;
}

template<
        typename ContainerIterator
>
typename cyclic_iterator<
        ContainerIterator
>::difference_type
cyclic_iterator<
        ContainerIterator
>::distance_to(
        cyclic_iterator const &_other
) const
{
        return _other.it_ - it_;
}

// Convenience function for template argument deduction
template <typename ContainerIterator>
cyclic_iterator <ContainerIterator> make_cyclic (
    ContainerIterator const& pos,
    ContainerIterator const& begin,
    ContainerIterator const& end);
}

#endif
