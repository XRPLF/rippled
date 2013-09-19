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

#ifndef BEAST_SORTEDLOOKUPTABLE_H_INCLUDED
#define BEAST_SORTEDLOOKUPTABLE_H_INCLUDED

/** Sorted map for fast lookups.

    This container is optimized for a data set with fixed elements.

    SchemaType obeys this concept:

    @code

    struct SchemaType
    {
        typename KeyType;
        typename ValueType;

        // Retrieve the key for a specified value.
        KeyType getKey (Value const& value);
    };

    @endcode

    To use the table, reserve space with reserveSpaceForValues() if the number
    of elements is known ahead of time. Then, call insert() for  all the your
    elements. Call prepareForLookups() once then call lookupValueByKey ()
*/
template <class SchemaType>
class SortedLookupTable
{
private:
    typedef typename SchemaType::KeyType KeyType;
    typedef typename SchemaType::ValueType ValueType;
    typedef std::vector <ValueType> values_t;

private:
    struct SortCompare
    {
        bool operator () (ValueType const& lhs, ValueType const& rhs) const
        {
            return SchemaType ().getKey (lhs) < SchemaType ().getKey (rhs);
        }
    };

    struct FindCompare
    {
        bool operator () (ValueType const& lhs, ValueType const& rhs)
        {
            return SchemaType ().getKey (lhs) < SchemaType ().getKey (rhs);
        }

        bool operator () (KeyType const& key, ValueType const& rhs)
        {
            return key < SchemaType ().getKey (rhs);
        }

        bool operator () (ValueType const& lhs, KeyType const& key)
        {
            return SchemaType ().getKey (lhs) < key;
        }
    };

public:
    typedef typename values_t::size_type size_type;

    /** Reserve space for values.

        Although not necessary, this can help with memory usage if the
        number of values is known ahead of time.

        @param numberOfValues The amount of space to reserve.
    */
    void reserveSpaceForValues (size_type numberOfValues)
    {
        m_values.reserve (numberOfValues);
    }

    /** Insert a value into the index.

        @invariant The value must not already exist in the index.

        @param valueToInsert The value to insert.
    */
    void insert (ValueType const& valueToInsert)
    {
        m_values.push_back (valueToInsert);
    }

    /** Prepare the index for lookups.

        This must be called at least once after calling insert()
        and before calling find().
    */
    void prepareForLookups ()
    {
        std::sort (m_values.begin (), m_values.end (), SortCompare ());
    }

    /** Find the value for a key.

        Quickly locates a value matching the key, or returns false
        indicating no value was found.

        @invariant You must call prepareForLookups() once, after all
                   insertions, before calling this function.

        @param key         The key to locate.
        @param pFoundValue Pointer to store the value if a matching
                           key was found.
        @return `true` if the value was found.
    */
    bool lookupValueByKey (KeyType const& key, ValueType* pFoundValue)
    {
        bool found;

        std::pair <typename values_t::iterator, typename values_t::iterator> result =
            std::equal_range (m_values.begin (), m_values.end (), key, FindCompare ());

        if (result.first != result.second)
        {
            *pFoundValue = *result.first;
            found = true;
        }
        else
        {
            found = false;
        }

        return found;
    }

private:
    values_t m_values;
};

#endif
