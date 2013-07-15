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

    void fetch (Array <ValidatorInfo>& results)
    {
    }

private:
};

StringsValidatorSource* StringsValidatorSource::New (StringArray const& strings)
{
    return new StringsValidatorSourceImp (strings);
}
