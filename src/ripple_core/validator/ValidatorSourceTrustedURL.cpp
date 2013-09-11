//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class ValidatorSourceTrustedURLImp : public ValidatorSourceTrustedURL
{
public:
    explicit ValidatorSourceTrustedURLImp (UniformResourceLocator const& url)
        : m_url (url)
    {
    }

    ~ValidatorSourceTrustedURLImp ()
    {
    }

    Result fetch (CancelCallback&)
    {
        Result result;

        ScopedPointer <HTTPClientBase> client (HTTPClientBase::New ());

        HTTPClientBase::Result httpResult (client->get (m_url));
        
        if (httpResult.error == 0)
        {
        }

        return result;
    }

private:
    UniformResourceLocator m_url;
};

//------------------------------------------------------------------------------

ValidatorSourceTrustedURL* ValidatorSourceTrustedURL::New (
    UniformResourceLocator const& url)
{
    ScopedPointer <ValidatorSourceTrustedURL> object (
        new ValidatorSourceTrustedURLImp (url));

    return object.release ();
}
