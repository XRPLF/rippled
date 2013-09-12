//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_VALIDATOR_VALIDATORSOURCESTRINGS_H_INCLUDED
#define RIPPLE_CORE_VALIDATOR_VALIDATORSOURCESTRINGS_H_INCLUDED

/** Provides validators from a set of Validator strings.
    Typically this will come from a local configuration file.
*/
class ValidatorSourceStrings : public Validators::Source
{
public:
    static ValidatorSourceStrings* New (StringArray const& strings);
};

#endif
