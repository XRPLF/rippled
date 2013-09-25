//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATORS_SOURCEURL_H_INCLUDED
#define RIPPLE_VALIDATORS_SOURCEURL_H_INCLUDED

namespace ripple {
namespace Validators {

/** Provides validators from a trusted URI (e.g. HTTPS) */
class SourceURL : public Source
{
public:
    static SourceURL* New (URL const& url);
};

}
}

#endif
