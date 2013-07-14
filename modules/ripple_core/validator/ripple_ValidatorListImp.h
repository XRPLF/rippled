//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATORLISTIMP_H_INCLUDED
#define RIPPLE_VALIDATORLISTIMP_H_INCLUDED

// This is a private header

/** Private implementation of ValidatorList.
*/
class ValidatorListImp : public ValidatorList
{
public:
    ValidatorListImp ()
    {
    }

    ~ValidatorListImp ()
    {
    }

    int size () const noexcept
    {
        return m_list.size ();
    }

    Validator::Ptr operator[] (int index)
    {
        return m_list.getObjectPointer (index);
    }

    void add (Validator::Ptr validator)
    {
        m_list.add (validator);
    }

private:
    SharedObjectArray <Validator> m_list;
};

#endif
