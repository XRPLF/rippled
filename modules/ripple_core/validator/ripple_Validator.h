//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATOR_H_INCLUDED
#define RIPPLE_VALIDATOR_H_INCLUDED

//------------------------------------------------------------------------------

/** Identifies a validator.

    A validator signs ledgers and participates in the consensus process.
    These are kept in a map so we can retrieve a unique Validator object
    given the public key in the ValidatorInfo.
*/
class Validator : public SharedObject
{
public:
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

        // VFALCO TODO magic number argh!!!
        //             This type should be located elsewhere.
        //
        typedef UnsignedInteger <33> PublicKey;

        PublicKey publicKey;
        //String friendlyName;
        //String organizationType;
        //String jurisdicton;
    };

    typedef SharedObjectPtr <Validator> Ptr;

    typedef Info::PublicKey PublicKey;

    //--------------------------------------------------------------------------

    /** A list of Validator that comes from a source of validators.

        Sources include trusted URIs, or a local file. The list may be
        signed.
    */
    class List : public SharedObject
    {
    public:
        typedef SharedObjectPtr <List> Ptr;

        explicit List (SharedObjectArray <Validator>& list)
        {
            m_list.swapWithArray (list);
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

    explicit Validator (PublicKey const& publicKey);

    PublicKey const& getPublicKey () const { return m_publicKey; }

private:
    PublicKey const m_publicKey;
};

#endif
