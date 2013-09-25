//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

bool ValidatorsUtilities::parseInfoLine (Validators::Source::Info& info, String line)
{
    bool success (false);

    return success;
}

void ValidatorsUtilities::parseResultLine (
    Validators::Source::Result& result,
    String line)
{
    bool success = false;

    if (! success)
    {
        Validators::Source::Info info;

        success = parseInfoLine (info, line);
        if (success)
            result.list.add (info);
    }

   
}
