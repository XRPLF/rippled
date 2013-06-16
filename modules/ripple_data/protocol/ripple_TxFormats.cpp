//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

TxFormats& TxFormats::getInstance ()
{
    static TxFormats instance;

    return instance;
}

TxFormat* TxFormats::add (TxFormat* txFormat)
{
    // VFALCO TODO Figure out when and how to delete the TxFormat objects later?
    m_types [txFormat->getType ()] = txFormat;
    m_names [txFormat->getName ()] = txFormat;

    return txFormat;
}

TxFormat* TxFormats::findByType (TransactionType type)
{
    TxFormat* result = NULL;

    TypeMap::iterator const iter = m_types.find (type);

    if (iter != m_types.end ())
    {
        result = iter->second;
    }

    return result;
}

TxFormat* TxFormats::findByName (std::string const& name)
{
    TxFormat* result = NULL; // VFALCO TODO replace all NULL with nullptr

    NameMap::iterator const iter = m_names.find (name);

    if (iter != m_names.end ())
    {
        result = iter->second;
    }

    return result;
}

TxFormats::TxFormats ()
{
}

