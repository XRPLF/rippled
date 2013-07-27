//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class TrustedUriValidatorSourceImp : public TrustedUriValidatorSource
{
public:
    explicit TrustedUriValidatorSourceImp (String const url)
        : m_url (url)
    {
    }

    ~TrustedUriValidatorSourceImp ()
    {
    }

    Array <Validator::Info> fetch ()
    {
        return Array <Validator::Info> ();
    }

private:
    String const m_url;
};

TrustedUriValidatorSource* TrustedUriValidatorSource::New (String url)
{
    return new TrustedUriValidatorSourceImp (url);
}
