//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

LogFile::LogFile ()
    : m_stream (nullptr)
{
}

LogFile::~LogFile ()
{
}

bool LogFile::isOpen () const noexcept
{
    return m_stream != nullptr;
}

bool LogFile::open (boost::filesystem::path const& path)
{
    close ();

    bool wasOpened = false;

    // VFALCO TODO Make this work with Unicode file paths
    ScopedPointer <std::ofstream> stream (
        new std::ofstream (path.c_str (), std::fstream::app));

    if (stream->good ())
    {
        m_path = path;

        m_stream = stream.release ();

        wasOpened = true;
    }

    return wasOpened;
}

bool LogFile::closeAndReopen ()
{
    close ();

    return open (m_path);
}

void LogFile::close ()
{
    m_stream = nullptr;
}

void LogFile::write (char const* text)
{
    if (m_stream != nullptr)
        (*m_stream) << text;
}

void LogFile::writeln (char const* text)
{
    if (m_stream != nullptr)
    {
        (*m_stream) << text;
        (*m_stream) << std::endl;
    }
}

