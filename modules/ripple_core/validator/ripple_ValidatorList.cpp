//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

ValidatorList::ValidatorList ()
    : m_isSigned (false)
{
}

ValidatorList::~ValidatorList ()
{
}

int ValidatorList::size () const
{
    return m_list.size ();
}

Validator& ValidatorList::operator [] (int index)
{
    return m_list.getReference (index);
}

bool ValidatorList::isSigned () const
{
    return m_isSigned;
}

void ValidatorList::add (Validator const& validator)
{
    m_list.add (validator);
}

