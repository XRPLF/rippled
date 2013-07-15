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

    virtual ~ValidatorList () { }

    /** Retrieve the number of items.
    */
    virtual int size () const noexcept = 0;

    virtual Validator::Ptr operator[] (int index) = 0;

    /** Add a validator to the list.
    */
    virtual void add (Validator::Ptr validator) = 0;
};

#endif
