//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_CONTAINER_CONST_CONTAINER_H_INCLUDED
#define BEAST_CONTAINER_CONST_CONTAINER_H_INCLUDED

namespace beast {

/** Adapter to constrain a container interface.
    The interface allows for limited read only operations. Derived classes
    provide additional behavior.
*/
template <class Container>
class const_container
{
private:
    typedef Container cont_type;

    cont_type m_cont;

protected:
    cont_type& cont()
    {
        return m_cont;
    }

    cont_type const& cont() const
    {
        return m_cont;
    }

public:
    typedef typename cont_type::value_type value_type;
    typedef typename cont_type::size_type size_type;
    typedef typename cont_type::difference_type difference_type;
    typedef typename cont_type::const_iterator iterator;
    typedef typename cont_type::const_iterator const_iterator;

    /** Returns `true` if the container is empty. */
    bool
    empty() const
    {
        return m_cont.empty();
    }

    /** Returns the number of items in the container. */
    size_type
    size() const
    {
        return m_cont.size();
    }

    /** Returns forward iterators for traversal. */
    /** @{ */
    const_iterator
    begin() const
    {
        return m_cont.cbegin();
    }

    const_iterator
    cbegin() const
    {
        return m_cont.cbegin();
    }

    const_iterator
    end() const
    {
        return m_cont.cend();
    }

    const_iterator
    cend() const
    {
        return m_cont.cend();
    }
    /** @} */
};

} // beast

#endif
