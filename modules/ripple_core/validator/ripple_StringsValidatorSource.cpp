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

    ValidatorList::Ptr fetch ()
    {
        ValidatorList::Ptr list = new ValidatorList;

        return list;
    }

private:
};

StringsValidatorSource* StringsValidatorSource::New (StringArray const& strings)
{
    return new StringsValidatorSourceImp (strings);
}
