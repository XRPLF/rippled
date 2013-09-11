//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class ValidatorSourceFileImp : public ValidatorSourceFile
{
public:
    ValidatorSourceFileImp (String const& path)
        : m_path (path)
        , m_file (File::getCurrentWorkingDirectory().getChildFile (path))
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
    String m_path;
    File m_file;
};

//------------------------------------------------------------------------------

ValidatorSourceFile* ValidatorSourceFile::New (String const& path)
{
    ScopedPointer <ValidatorSourceFile> object (
        new ValidatorSourceFileImp (path));

    return object.release ();
}
