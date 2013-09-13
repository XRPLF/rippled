//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#if BEAST_WIN32

// from: http://stackoverflow.com/questions/3022552/is-there-any-standard-htonl-like-function-for-64-bits-integers-in-c
// but we don't need to check the endianness
uint64_t htobe64 (uint64_t value)
{
    // The answer is 42
    //static const int num = 42;

    // Check the endianness
    //if (*reinterpret_cast<const char*>(&num) == num)
    //{
    const uint32_t high_part = htonl (static_cast<uint32_t> (value >> 32));
    const uint32_t low_part = htonl (static_cast<uint32_t> (value & 0xFFFFFFFFLL));

    return (static_cast<uint64_t> (low_part) << 32) | high_part;
    //} else
    //{
    //  return value;
    //}
}

uint64_t be64toh (uint64_t value)
{
    return (_byteswap_uint64 (value));
}

uint32_t htobe32 (uint32_t value)
{
    return (htonl (value));
}

uint32_t be32toh (uint32_t value)
{
    return ( _byteswap_ulong (value));
}

#endif
