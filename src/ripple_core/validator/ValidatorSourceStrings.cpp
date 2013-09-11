//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class ValidatorSourceStringsImp : public ValidatorSourceStrings
{
public:
    ValidatorSourceStringsImp (StringArray const& strings)
    {
    }

    ~ValidatorSourceStringsImp ()
    {
    }

    Result fetch (CancelCallback&)
    {
        Result result;
        
        return result;
    }

private:
};

//------------------------------------------------------------------------------

ValidatorSourceStrings* ValidatorSourceStrings::New (StringArray const& strings)
{
    ScopedPointer <ValidatorSourceStrings> object (
        new ValidatorSourceStringsImp (strings));

    return object.release ();
}
