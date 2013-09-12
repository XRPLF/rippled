//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace Validators
{

class SourceFileImp : public SourceFile
{
public:
    SourceFileImp (File const& file)
        : m_file (file)
    {
    }

    ~SourceFileImp ()
    {
    }
    
    String name ()
    {
        return "File :'" + m_file.getFullPathName () + "'";
    }

    Result fetch (CancelCallback&, Journal journal)
    {
        Result result;
        
        return result;
    }

private:
    File m_file;
};

//------------------------------------------------------------------------------

SourceFile* SourceFile::New (File const& file)
{
    ScopedPointer <SourceFile> object (
        new SourceFileImp (file));

    return object.release ();
}

}
