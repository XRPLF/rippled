/*============================================================================*/
/*
  VFLib: https://github.com/vinniefalco/VFLib

  Copyright (C) 2008 by Vinnie Falco <vinnie.falco@gmail.com>

  This library contains portions of other open source products covered by
  separate licenses. Please see the corresponding source files for specific
  terms.

  VFLib is provided under the terms of The MIT License (MIT):

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
  IN THE SOFTWARE.
*/
/*============================================================================*/

#ifndef BEAST_SORTEDLOOKUPTABLE_BEASTHEADER
#define BEAST_SORTEDLOOKUPTABLE_BEASTHEADER

//==============================================================================
/**
  Sorted map for fast lookups.

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

    values_t m_values;

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
};

#endif
