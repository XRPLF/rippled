//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_VALIDATOR_VALIDATOR_H_INCLUDED
#define RIPPLE_CORE_VALIDATOR_VALIDATOR_H_INCLUDED

//------------------------------------------------------------------------------

/** Identifies a validator.

    A validator signs ledgers and participates in the consensus process.
    These are kept in a map so we can retrieve a unique Validator object
    given the public key in the ValidatorInfo.
*/
class Validator : public SharedObject
{
public:
    typedef SharedObjectPtr <Validator> Ptr;

    /** Fixed information on a validator.

        This describes a validator.
    */
    struct Info
    {
        struct Compare
        {
            static int compareElements (Info const& lhs, Info const& rhs)
            {
                int result;

                if (lhs.publicKey < rhs.publicKey)
                    result = -1;
                else if (lhs.publicKey > rhs.publicKey)
                    result = 1;
                else
                    result = 0;

                return result;
            }
        };

        RipplePublicKey publicKey;
        //String friendlyName;
        //String organizationType;
        //String jurisdicton;

        static void sortAndRemoveDuplicates (Array <Info>& arrayToSort)
        {
            Array <Info> sorted;

            sorted.ensureStorageAllocated (arrayToSort.size ());

            for (int i = 0; i < arrayToSort.size (); ++i)
            {
                Compare compare;
                
                int const index = sorted.addSorted (compare, arrayToSort [i]);

                if (index > 0 && Compare::compareElements (sorted [index], sorted [index-1]) == 0)
                {
                    // duplicate
                    sorted.remove (index);
                }
            }

            arrayToSort.swapWith (sorted);
        }
    };

    //--------------------------------------------------------------------------

    // Comparison function for Validator objects
    //
    struct Compare
    {
        static int compareElements (Validator const* lhs, Validator const* rhs)
        {
            return lhs->getPublicKey().compare (rhs->getPublicKey ());
        }
    };

    /** A list of Validator that comes from a source of validators.

        Sources include trusted URIs, or a local file. The list may be
        signed.

        @note The list is immutable and guaranteed to be sorted.
    */
    class List : public SharedObject
    {
    public:
        typedef SharedObjectPtr <List> Ptr;

        explicit List (SharedObjectArray <Validator>& list)
        {
            m_list.swapWith (list);

            Validator::Compare compare;

            m_list.sort (compare);
        }

        ~List ()
        {
        }

        /** Retrieve the number of items. */
        int size () const noexcept
        {
            return m_list.size ();
        }

        /** Retrieve an item by index. */
        Validator::Ptr operator[] (int index) const
        {
            return m_list.getObjectPointer (index);
        }

    private:
        SharedObjectArray <Validator> m_list;
    };

    class ListImp;

    //--------------------------------------------------------------------------

    explicit Validator (RipplePublicKey const& publicKey);

    RipplePublicKey const& getPublicKey () const { return m_publicKey; }

private:
    RipplePublicKey const m_publicKey;

};

#endif
