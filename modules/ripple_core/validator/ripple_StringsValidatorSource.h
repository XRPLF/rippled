//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_STRINGSVALIDATORSOURCE_H_INCLUDED
#define RIPPLE_STRINGSVALIDATORSOURCE_H_INCLUDED

/** Provides validators from a set of Validator strings.

    Typically this will come from a local configuration file.
*/
class StringsValidatorSource : public Validators::Source
{
public:
    static StringsValidatorSource* New (StringArray const& strings);
};

#endif
