//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace Validators
{

class SourceURLImp : public SourceURL
{
public:
    explicit SourceURLImp (UniformResourceLocator const& url)
        : m_url (url)
    {
    }

    ~SourceURLImp ()
    {
    }

    String name ()
    {
        return "URL: '" + m_url.full() + "'";
    }

    String uniqueID ()
    {
        return "URL," + m_url.full();
    }

    String createParam ()
    {
        return m_url.full();
    }

    Result fetch (CancelCallback&, Journal journal)
    {
        Result result;

        ScopedPointer <HTTPClientBase> client (HTTPClientBase::New ());

        HTTPClientBase::Result httpResult (client->get (m_url));

        if (httpResult.error == 0)
        {
            //Logger::outputDebugString (httpResult.response->toString ());
        }
        else
        {
            journal.error() <<
                "HTTP GET to " << m_url.full().toStdString() <<
                " failed: '" << httpResult.error.message () << "'";
        }

        return result;
    }

private:
    UniformResourceLocator m_url;
};

//------------------------------------------------------------------------------

SourceURL* SourceURL::New (
    UniformResourceLocator const& url)
{
    ScopedPointer <SourceURL> object (
        new SourceURLImp (url));

    return object.release ();
}

}
