//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATORLIST_H_INCLUDED
#define RIPPLE_VALIDATORLIST_H_INCLUDED

/** A list of Validator that comes from a source of validators.

    Sources include trusted URIs, or a local file.

    The list may be signed.
*/
class ValidatorList : public SharedObject
{
public:
    typedef SharedObjectPtr <ValidatorList> Ptr;

    /** Create an empty list.
    */
    ValidatorList ();

    ~ValidatorList ();

    /** Retrieve the number of items.
    */
    int size () const;

    Validator& operator[] (int index);

    bool isSigned () const;

    /** Add a validator to the list.
    */
    void add (Validator const& validator);

private:
    bool m_isSigned;
    Array <Validator> m_list;
};

#endif
