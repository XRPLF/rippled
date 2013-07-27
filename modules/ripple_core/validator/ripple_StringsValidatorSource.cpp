//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class StringsValidatorSourceImp : public StringsValidatorSource
{
public:
    StringsValidatorSourceImp (StringArray const& strings)
    {
    }

    ~StringsValidatorSourceImp ()
    {
    }

    Array <Validator::Info> fetch ()
    {
        return Array <Validator::Info> ();
    }

private:
};

StringsValidatorSource* StringsValidatorSource::New (StringArray const& strings)
{
    return new StringsValidatorSourceImp (strings);
}
