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

#ifndef BEAST_SHAREDTABLE_BEASTHEADER
#define BEAST_SHAREDTABLE_BEASTHEADER

/** Handle to a reference counted fixed size table.

    @note Currently, ElementType must be an aggregate of POD.

    @tparam ElementType The type of element.

    @ingroup beast_gui
*/
template <class ElementType>
class SharedTable
{
public:
    typedef ElementType Entry;

    static SharedTable <ElementType> const null;

    /** Creates a null table.
    */
    SharedTable ()
    {
    }

    /** Creates a table with the specified number of entries.

        The entries are uninitialized.

        @param numEntries The number of entries in the table.

        @todo Initialize the data if ElementType is not POD.
    */
    explicit SharedTable (int numEntries)
        : m_data (new Data (numEntries))
    {
    }

    /** Creates a shared reference to another table.
    */
    SharedTable (SharedTable const& other)
        : m_data (other.m_data)
    {
    }

    /** Makes this table refer to another table.
    */
    SharedTable& operator= (SharedTable const& other)
    {
        m_data = other.m_data;
        return *this;
    }

#if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
    SharedTable (SharedTable&& other) noexcept
:
    m_data (static_cast < typename Data::Ptr&& > (other.m_data))
    {
    }

    SharedTable& operator= (SharedTable && other) noexcept
    {
        m_data = static_cast < typename Data::Ptr && > (other.m_data);
        return *this;
    }
#endif

    /** Destructor.
    */
    ~SharedTable ()
    {
    }

    /** Returns true if the two tables share the same set of entries.
    */
    bool operator== (SharedTable const& other) const noexcept
    {
        return m_data == other.m_data;
    }

    /** Returns true if the two tables do not share the same set of entries.
    */
    bool operator!= (SharedTable const& other) const noexcept
    {
        return m_data != other.m_data;
    }

    /** Returns true if the table is not null.
    */
    inline bool isValid () const noexcept
    {
        return m_data != nullptr;
    }

    /** Returns true if the table is null.
    */
    inline bool isNull () const noexcept
    {
        return m_data == nullptr;
    }

    /** Returns the number of tables referring to the same shared entries.
    */
    int getReferenceCount () const noexcept
    {
        return m_data == nullptr ? 0 : m_data->getReferenceCount ();
    }

    /** Create a physical duplicate of the table.
    */
    SharedTable createCopy () const
    {
        return SharedTable (m_data != nullptr ? m_data->clone () : nullptr);
    }

    /** Makes sure no other tables share the same entries as this table.
    */
    void duplicateIfShared ()
    {
        if (m_data != nullptr && m_data->getReferenceCount () > 1)
            m_data = m_data->clone ();
    }

    /** Return the number of entries in this table.
    */
    inline int getNumEntries () const noexcept
    {
        return m_data->getNumEntries ();
    }

    /** Retrieve a table entry.

        @param index The index of the entry, from 0 to getNumEntries ().
    */
    inline ElementType& operator [] (int index) const noexcept
    {
        return m_data->getReference (index);
    }

private:
    class Data : public ReferenceCountedObject
    {
    public:
        typedef ReferenceCountedObjectPtr <Data> Ptr;

        explicit Data (int numEntries)
            : m_numEntries (numEntries)
            , m_table (numEntries)
        {
        }

        inline Data* clone () const
        {
            Data* data = new Data (m_numEntries);

            memcpy (
                data->m_table.getData (),
                m_table.getData (),
                m_numEntries * sizeof (ElementType));

            return data;
        }

        inline int getNumEntries () const
        {
            return m_numEntries;
        }

        inline ElementType& getReference (int index) const
        {
            bassert (index >= 0 && index < m_numEntries);
            return m_table [index];
        }

    private:
        int const m_numEntries;
        HeapBlock <ElementType> const m_table;
    };

    explicit SharedTable (Data* data)
        : m_data (data)
    {
    }

    ReferenceCountedObjectPtr <Data> m_data;
};

template <class ElementType>
SharedTable <ElementType> const SharedTable <ElementType>::null;

#endif

//------------------------------------------------------------------------------

