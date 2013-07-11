//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TRUSTEDURIVALIDATORSOURCE_H_INCLUDED
#define RIPPLE_TRUSTEDURIVALIDATORSOURCE_H_INCLUDED

/** Provides validators from a trusted URI (e.g. HTTPS)
*/
class TrustedUriValidatorSource : public Validators::Source
{
public:
    static TrustedUriValidatorSource* New (String url);
};

#endif
