//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_VALIDATOR_VALIDATORSOURCEFILE_H_INCLUDED
#define RIPPLE_CORE_VALIDATOR_VALIDATORSOURCEFILE_H_INCLUDED

/** Provides validators from a text file.
    Typically this will come from a local configuration file.
*/
class ValidatorSourceFile : public Validators::Source
{
public:
    static ValidatorSourceFile* New (File const& path);
};

#endif
