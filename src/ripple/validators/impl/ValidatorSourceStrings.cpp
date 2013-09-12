//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class ValidatorSourceStringsImp : public ValidatorSourceStrings
{
public:
    ValidatorSourceStringsImp (StringArray const& strings)
        : m_strings (strings)
    {
    }

    ~ValidatorSourceStringsImp ()
    {
    }

    Result fetch (CancelCallback&)
    {
        Result result;

        result.list.ensureStorageAllocated (m_strings.size ());

        for (int i = 0; i < m_strings.size (); ++i)
        {
            ValidatorsUtilities::parseResultLine (result, m_strings [i]);
        }

        result.success = result.list.size () > 0;
        result.expirationTime = Time::getCurrentTime () + RelativeTime::hours (24);
        return result;
    }

private:
    StringArray m_strings;
};

//------------------------------------------------------------------------------

ValidatorSourceStrings* ValidatorSourceStrings::New (StringArray const& strings)
{
    ScopedPointer <ValidatorSourceStrings> object (
        new ValidatorSourceStringsImp (strings));

    return object.release ();
}
