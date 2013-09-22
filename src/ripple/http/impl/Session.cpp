//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace ripple {
namespace HTTP {

Session::Session ()
    : headersComplete (false)
    , tag (nullptr)
{
    content.reserve (1000);
}

ScopedStream Session::operator<< (
    std::ostream& manip (std::ostream&))
{
    return ScopedStream (*this, manip);
}

}
}
