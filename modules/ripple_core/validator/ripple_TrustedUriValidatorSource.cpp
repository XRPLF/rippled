//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class TrustedUriValidatorSourceImp : public TrustedUriValidatorSource
{
public:
    TrustedUriValidatorSourceImp (String const url)
        : m_url (url)
    {
    }

    ~TrustedUriValidatorSourceImp ()
    {
    }

    ValidatorList::Ptr fetch ()
    {
        ValidatorList::Ptr list = new ValidatorList;

        return list;
    }

private:
    String const m_url;
};

TrustedUriValidatorSource* TrustedUriValidatorSource::New (String url)
{
    return new TrustedUriValidatorSourceImp (url);
}
