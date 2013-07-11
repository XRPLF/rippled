//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

ChosenValidators::ChosenValidators (ValidatorList::Ptr chosenValidators,
                                    ValidatorList::Ptr wellKnownValidators)
    : m_list (chosenValidators)
    , m_wellKnownList (wellKnownValidators)
{
}

ChosenValidators::~ChosenValidators ()
{
}

ValidatorList::Ptr ChosenValidators::getList ()
{
    return m_list;
}

ValidatorList::Ptr ChosenValidators::getWellKnownList ()
{
    return m_wellKnownList;
}

