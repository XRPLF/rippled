//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace Validators
{

class SourceStringsImp : public SourceStrings
{
public:
    SourceStringsImp (
        String name, StringArray const& strings)
        : m_name (name)
        , m_strings (strings)
    {
    }

    ~SourceStringsImp ()
    {
    }

    String name ()
    {
        return m_name;
    }

    String uniqueID ()
    {
        return String::empty;
    }

    String createParam ()
    {
        return String::empty;
    }

    Result fetch (CancelCallback&, Journal journal)
    {
        Result result;

        result.list.ensureStorageAllocated (m_strings.size ());

        for (int i = 0; i < m_strings.size (); ++i)
        {
            std::string const s (m_strings [i].toStdString ());
            Utilities::parseResultLine (result, s);
        }

        result.success = result.list.size () > 0;
        result.expirationTime = Time::getCurrentTime () + RelativeTime::hours (24);
        return result;
    }

private:
    String m_name;
    StringArray m_strings;
};

//------------------------------------------------------------------------------

SourceStrings* SourceStrings::New (
    String name, StringArray const& strings)
{
    ScopedPointer <SourceStrings> object (
        new SourceStringsImp (name, strings));

    return object.release ();
}

}
