//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATORS_SOURCEFILE_H_INCLUDED
#define RIPPLE_VALIDATORS_SOURCEFILE_H_INCLUDED

namespace ripple {
namespace Validators {

/** Provides validators from a text file.
    Typically this will come from a local configuration file.
*/
class SourceFile : public Source
{
public:
    static SourceFile* New (File const& path);
};

}
}

#endif
