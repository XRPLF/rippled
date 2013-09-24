//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace ripple {

std::size_t hash_value (const uint160& u)
{
    std::size_t seed = HashMaps::getInstance ().getNonce <std::size_t> ();

    return u.hash_combine (seed);
}

}
