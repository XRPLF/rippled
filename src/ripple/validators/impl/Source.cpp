//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace ripple {
namespace Validators {

Source::Result::Result ()
    : success (false)
    , message ("uninitialized")
{
}

void Source::Result::swapWith (Result& other)
{
    std::swap (success, other.success);
    std::swap (message, other.message);
    list.swapWith (other.list);
}

}
}
