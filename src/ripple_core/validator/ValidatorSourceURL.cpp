//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class ValidatorSourceURLImp : public ValidatorSourceURL
{
public:
    explicit ValidatorSourceURLImp (UniformResourceLocator const& url)
        : m_url (url)
    {
    }

    ~ValidatorSourceURLImp ()
    {
    }

    Result fetch (CancelCallback&)
    {
        Result result;

        ScopedPointer <HTTPClientBase> client (HTTPClientBase::New ());

        HTTPClientBase::Result httpResult (client->get (m_url));
        
        if (httpResult.error == 0)
        {
            Logger::outputDebugString (httpResult.response->toString ());
        }

        return result;
    }

private:
    UniformResourceLocator m_url;
};

//------------------------------------------------------------------------------

ValidatorSourceURL* ValidatorSourceURL::New (
    UniformResourceLocator const& url)
{
    ScopedPointer <ValidatorSourceURL> object (
        new ValidatorSourceURLImp (url));

    return object.release ();
}
