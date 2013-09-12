//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class ValidatorSourceFileImp : public ValidatorSourceFile
{
public:
    ValidatorSourceFileImp (File const& file)
        : m_file (file)
    {
    }

    ~ValidatorSourceFileImp ()
    {
    }

    Result fetch (CancelCallback&)
    {
        Result result;
        
        return result;
    }

private:
    File m_file;
};

//------------------------------------------------------------------------------

ValidatorSourceFile* ValidatorSourceFile::New (File const& file)
{
    ScopedPointer <ValidatorSourceFile> object (
        new ValidatorSourceFileImp (file));

    return object.release ();
}
