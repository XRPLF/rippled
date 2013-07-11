//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CHOSENVALIDATORS_H_INCLUDED
#define RIPPLE_CHOSENVALIDATORS_H_INCLUDED

/** A subset of validators chosen from a list of well known validators.

    @see Validators
*/
class ChosenValidators : public SharedObject
{
public:
    typedef SharedObjectPtr <ChosenValidators> Ptr;

    ChosenValidators (ValidatorList::Ptr chosenValidators,
                      ValidatorList::Ptr wellKnownValidators);

    ~ChosenValidators ();

    /** Retrieve the list of chosen validators.

        This is the subset of validators that we care about.
    */
    ValidatorList::Ptr getList ();

    /** Retrieve the list of well known validators.

        This is the set from which the chosen validators were chosen.
    */
    ValidatorList::Ptr getWellKnownList ();

private:
    ValidatorList::Ptr m_list;
    ValidatorList::Ptr m_wellKnownList;
};

#endif
