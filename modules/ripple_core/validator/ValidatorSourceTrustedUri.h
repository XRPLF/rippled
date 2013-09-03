//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_VALIDATOR_VALIDATORSOURCETRUSTEDURI_H_INCLUDED
#define RIPPLE_CORE_VALIDATOR_VALIDATORSOURCETRUSTEDURI_H_INCLUDED

/** Provides validators from a trusted URI (e.g. HTTPS)
*/
class ValidatorSourceTrustedUri : public Validators::Source
{
public:
    static ValidatorSourceTrustedUri* New (String const& uri);
};

#endif
