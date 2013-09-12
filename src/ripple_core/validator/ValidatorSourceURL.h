//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_VALIDATOR_VALIDATORSOURCETRUSTEDURL_H_INCLUDED
#define RIPPLE_CORE_VALIDATOR_VALIDATORSOURCETRUSTEDURL_H_INCLUDED

/** Provides validators from a trusted URI (e.g. HTTPS)
*/
class ValidatorSourceURL : public Validators::Source
{
public:
    static ValidatorSourceURL* New (UniformResourceLocator const& url);
};

#endif
