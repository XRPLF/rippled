//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

// included by json_value.cpp

#include <ripple/json/json_value.h>

namespace Json {

// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////
// class ValueIteratorBase
// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////

ValueIteratorBase::ValueIteratorBase ()
    : current_ ()
    , isNull_ ( true )
{
}

ValueIteratorBase::ValueIteratorBase ( const Value::ObjectValues::iterator& current )
    : current_ ( current )
    , isNull_ ( false )
{
}

Value&
ValueIteratorBase::deref () const
{
    return current_->second;
}


void
ValueIteratorBase::increment ()
{
    ++current_;
}


void
ValueIteratorBase::decrement ()
{
    --current_;
}


ValueIteratorBase::difference_type
ValueIteratorBase::computeDistance ( const SelfType& other ) const
{
    // Iterator for null value are initialized using the default
    // constructor, which initialize current_ to the default
    // std::map::iterator. As begin() and end() are two instance
    // of the default std::map::iterator, they can not be compared.
    // To allow this, we handle this comparison specifically.
    if ( isNull_  &&  other.isNull_ )
    {
        return 0;
    }


    // Usage of std::distance is not portable (does not compile with Sun Studio 12 RogueWave STL,
    // which is the one used by default).
    // Using a portable hand-made version for non random iterator instead:
    //   return difference_type( std::distance( current_, other.current_ ) );
    difference_type myDistance = 0;

    for ( Value::ObjectValues::iterator it = current_; it != other.current_; ++it )
    {
        ++myDistance;
    }

    return myDistance;
}


bool
ValueIteratorBase::isEqual ( const SelfType& other ) const
{
    if ( isNull_ )
    {
        return other.isNull_;
    }

    return current_ == other.current_;
}


void
ValueIteratorBase::copy ( const SelfType& other )
{
    current_ = other.current_;
}


Value
ValueIteratorBase::key () const
{
    const Value::CZString czstring = (*current_).first;

    if ( czstring.c_str () )
    {
        if ( czstring.isStaticString () )
            return Value ( StaticString ( czstring.c_str () ) );

        return Value ( czstring.c_str () );
    }

    return Value ( czstring.index () );
}


UInt
ValueIteratorBase::index () const
{
    const Value::CZString czstring = (*current_).first;

    if ( !czstring.c_str () )
        return czstring.index ();

    return Value::UInt ( -1 );
}


const char*
ValueIteratorBase::memberName () const
{
    const char* name = (*current_).first.c_str ();
    return name ? name : "";
}


// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////
// class ValueConstIterator
// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////

ValueConstIterator::ValueConstIterator ()
{
}

ValueConstIterator::ValueConstIterator ( const Value::ObjectValues::iterator& current )
    : ValueIteratorBase ( current )
{
}

ValueConstIterator&
ValueConstIterator::operator = ( const ValueIteratorBase& other )
{
    copy ( other );
    return *this;
}


// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////
// class ValueIterator
// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////

ValueIterator::ValueIterator ()
{
}


ValueIterator::ValueIterator ( const Value::ObjectValues::iterator& current )
    : ValueIteratorBase ( current )
{
}

ValueIterator::ValueIterator ( const ValueConstIterator& other )
    : ValueIteratorBase ( other )
{
}

ValueIterator::ValueIterator ( const ValueIterator& other )
    : ValueIteratorBase ( other )
{
}

ValueIterator&
ValueIterator::operator = ( const SelfType& other )
{
    copy ( other );
    return *this;
}

} // Json
