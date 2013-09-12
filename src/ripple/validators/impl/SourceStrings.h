//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATORS_SOURCESTRINGS_H_INCLUDED
#define RIPPLE_VALIDATORS_SOURCESTRINGS_H_INCLUDED

namespace Validators
{

/** Provides validators from a set of Validator strings.
    Typically this will come from a local configuration file.
*/
class SourceStrings : public Source
{
public:
    static SourceStrings* New (
        String name, StringArray const& strings);
};

}

#endif
