//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

int DatabaseCon::sCount = 0;

DatabaseCon::DatabaseCon (const std::string& strName, const char* initStrings[], int initCount)
{
    ++sCount;

    // VFALCO TODO remove this dependency on the config by making it the caller's
    //         responsibility to pass in the path. Add a member function to Application
    //         or Config to compute this path.
    //
    boost::filesystem::path pPath   = (getConfig ().RUN_STANDALONE &&
                                          ((getConfig ().START_UP != Config::LOAD) && (getConfig ().START_UP != Config::REPLAY)))
                                      ? ""                                // Use temporary files.
                                      : (getConfig ().DATA_DIR / strName);       // Use regular db files.

    mDatabase = new SqliteDatabase (pPath.string ().c_str ());
    mDatabase->connect ();

    for (int i = 0; i < initCount; ++i)
        mDatabase->executeSQL (initStrings[i], true);
}

DatabaseCon::~DatabaseCon ()
{
    mDatabase->disconnect ();
    delete mDatabase;
}
