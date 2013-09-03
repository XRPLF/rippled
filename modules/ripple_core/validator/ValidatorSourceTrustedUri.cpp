//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class ValidatorSourceTrustedUriImp : public ValidatorSourceTrustedUri
{
public:
    explicit ValidatorSourceTrustedUriImp (String const& uri)
        : m_uri (uri)
    {
    }

    ~ValidatorSourceTrustedUriImp ()
    {
    }

    Array <Info> fetch (CancelCallback&)
    {
        return Array <Info> ();
    }

private:
    String const m_uri;
};

//------------------------------------------------------------------------------

ValidatorSourceTrustedUri* ValidatorSourceTrustedUri::New (String const& uri)
{
    ScopedPointer <ValidatorSourceTrustedUri> object (
        new ValidatorSourceTrustedUriImp (uri));

    return object.release ();
}
