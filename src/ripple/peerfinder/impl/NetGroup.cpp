//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2021 Ripple Labs Inc.

    Copyright (c) 2020 The Bitcoin Core developers
    Distributed under the MIT software license, see
    http://www.opensource.org/licenses/mit-license.php.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/peerfinder/impl/CSipHasher.h>
#include <ripple/peerfinder/impl/NetGroup.h>

#ifdef _WIN32
#include <winsock.h>
#else
#include <arpa/inet.h>
#endif

namespace ripple {

namespace {
static constexpr u_char pchOnionCat[] = {0xFD, 0x87, 0xD8, 0x7E, 0xEB, 0x43};
static constexpr u_char internalPrefix[] = {0xFD, 0x6B, 0x88, 0xC0, 0x87, 0x24};
static constexpr std::uint64_t RANDOMIZER_ID_NETGROUP = 0x6c0edd8036ef4036ULL;
static constexpr std::array<u_char, 12> pchIPv4 =
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
}  // namespace

uint32_t static ReadBE32(const unsigned char* ptr)
{
    std::uint32_t x;
    std::memcpy((char*)&x, ptr, 4);
    return ntohl(x);  // be32toh()
}

NetGroup::NetGroup(beast::IP::Endpoint const& ep)
    : isV4_(ep.address().is_v4()), isLoopback_(ep.address().is_loopback())
{
    assert(rawBytes_.size() == 16);
    if (ep.is_v4())
    {  // netaddress.cpp:CNetAddr::SetRaw
        network_ = Network::IPv4;
        std::copy(pchIPv4.begin(), pchIPv4.end(), rawBytes_.begin());
        auto bytesv4{ep.address().to_v4().to_bytes()};
        std::copy(bytesv4.begin(), bytesv4.end(), rawBytes_.begin() + 12);
    }
    else
    {
        auto bytes16{ep.address().to_v6().to_bytes()};
        if (std::memcmp(bytes16.data(), pchOnionCat, sizeof(pchOnionCat)) == 0)
            network_ = Network::NetOnion;
        else if (
            std::memcmp(
                bytes16.data(), internalPrefix, sizeof(internalPrefix)) == 0)
            network_ = Network::NetInternal;
        else
            network_ = Network::IPv6;
        std::copy(bytes16.begin(), bytes16.end(), rawBytes_.begin());
    }
}

std::uint32_t
NetGroup::getNetClass() const
{
    std::uint32_t netClass = Network::IPv6;
    if (isLocal())
        netClass = 255;
    if (isInternal())
        return Network::NetInternal;
    else if (!isRoutable())
        return Network::Unroutable;
    else if (hasLinkedIPv4())
        return Network::IPv4;
    else if (isTor())
        return Network::NetOnion;
    return netClass;
}

bool
NetGroup::hasLinkedIPv4() const
{
    return isRoutable() &&
        (isIPv4() || isRFC6145() || isRFC6052() || isRFC3964() || isRFC4380());
}

bool
NetGroup::isLocal() const
{
    return isLoopback_ || (isV4_ && getByte(3) == 0);
}

bool
NetGroup::isRoutable() const
{
    return !(
        isRFC1918() || isRFC2544() || isRFC3927() || isRFC4862() ||
        isRFC6598() || isRFC5737() || (isRFC4193() && !isTor()) ||
        isRFC4843() || isRFC7343() || isLocal() || isInternal());
}

bool
NetGroup::isRFC1918() const
{
    return isIPv4() &&
        (getByte(3) == 10 || (getByte(3) == 192 && getByte(2) == 168) ||
         (getByte(3) == 172 && (getByte(2) >= 16 && getByte(2) <= 31)));
}

bool
NetGroup::isRFC2544() const
{
    return isIPv4() && getByte(3) == 198 &&
        (getByte(2) == 18 || getByte(2) == 19);
}

bool
NetGroup::isRFC3927() const
{
    return isIPv4() && (getByte(3) == 169 && getByte(2) == 254);
}

bool
NetGroup::isRFC6598() const
{
    return isIPv4() && getByte(3) == 100 && getByte(2) >= 64 &&
        getByte(2) <= 127;
}

bool
NetGroup::isRFC5737() const
{
    return isIPv4() &&
        ((getByte(3) == 192 && getByte(2) == 0 && getByte(1) == 2) ||
         (getByte(3) == 198 && getByte(2) == 51 && getByte(1) == 100) ||
         (getByte(3) == 203 && getByte(2) == 0 && getByte(1) == 113));
}

bool
NetGroup::isRFC3849() const
{
    return isIPv6() && getByte(15) == 0x20 && getByte(14) == 0x01 &&
        getByte(13) == 0x0D && getByte(12) == 0xB8;
}

bool
NetGroup::isRFC3964() const
{
    return isIPv6() && getByte(15) == 0x20 && getByte(14) == 0x02;
}

bool
NetGroup::isRFC6052() const
{
    static constexpr u_char pchRFC6052[] = {
        0, 0x64, 0xFF, 0x9B, 0, 0, 0, 0, 0, 0, 0, 0};
    return isIPv6() &&
        std::memcmp(rawBytes_.data(), pchRFC6052, sizeof(pchRFC6052)) == 0;
}

bool
NetGroup::isRFC4380() const
{
    return isIPv6() && getByte(15) == 0x20 && getByte(14) == 0x01 &&
        getByte(13) == 0 && getByte(12) == 0;
}

bool
NetGroup::isRFC4862() const
{
    static constexpr u_char pchRFC4862[] = {0xFE, 0x80, 0, 0, 0, 0, 0, 0};
    return isIPv6() &&
        std::memcmp(rawBytes_.data(), pchRFC4862, sizeof(pchRFC4862)) == 0;
}

bool
NetGroup::isRFC4193() const
{
    return isIPv6() && (getByte(15) & 0xFE) == 0xFC;
}

bool
NetGroup::isRFC6145() const
{
    static constexpr u_char pchRFC6145[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0, 0};
    return isIPv6() &&
        std::memcmp(rawBytes_.data(), pchRFC6145, sizeof(pchRFC6145)) == 0;
}

bool
NetGroup::isRFC4843() const
{
    return isIPv6() && getByte(15) == 0x20 && getByte(14) == 0x01 &&
        getByte(13) == 0x00 && (getByte(12) & 0xF0) == 0x10;
}

bool
NetGroup::isRFC7343() const
{
    return isIPv6() && getByte(15) == 0x20 && getByte(14) == 0x01 &&
        getByte(13) == 0x00 && (getByte(12) & 0xF0) == 0x20;
}

std::vector<u_char>
NetGroup::getGroup() const
{
    std::vector<u_char> res;
    auto netClass = getNetClass();
    res.push_back(netClass);
    int nStartByte = 0;
    int nBits = 16;

    if (isLocal())
    {
        // all local addresses belong to the same group
        nBits = 0;
    }
    else if (isInternal())
    {
        // all internal-usage addresses get their own group
        nStartByte = sizeof(internalPrefix);
        nBits = (rawBytes_.size() - sizeof(internalPrefix)) * 8;
    }
    else if (!isRoutable())
    {
        // all other unroutable addresses belong to the same group
        nBits = 0;
    }
    else if (hasLinkedIPv4())
    {
        // ipv4 addresses (and mapped ipv4 addresses) use /16 groups
        std::uint32_t ipv4 = getLinkedIPv4();
        res.push_back((ipv4 >> 24) & 0xFF);
        res.push_back((ipv4 >> 16) & 0xFF);
        return res;
    }
    else if (isTor())
    {
        nStartByte = 6;
        nBits = 4;
    }
    else if (isHeNet())
    {
        // of he.net, use /36 groups
        nBits = 36;
    }
    else
    {
        // for the rest of the IPv6 network, use /32 groups
        nBits = 32;
    }

    // push our ip onto vchRet byte by byte...
    while (nBits >= 8)
    {
        res.push_back(getByte(15 - nStartByte));
        nStartByte++;
        nBits -= 8;
    }
    // ...for the last byte, push nBits and for the rest of the byte push 1's
    if (nBits > 0)
        res.push_back(getByte(15 - nStartByte) | ((1 << (8 - nBits)) - 1));

    return res;
}

bool
NetGroup::isHeNet() const
{
    return (
        getByte(15) == 0x20 && getByte(14) == 0x01 && getByte(13) == 0x04 &&
        getByte(12) == 0x70);
}

uint32_t
NetGroup::getLinkedIPv4() const
{
    if (isIPv4() || isRFC6145() || isRFC6052())
    {
        // IPv4, mapped IPv4, SIIT translated IPv4: the IPv4 address is the last
        // 4 bytes of the address
        return ReadBE32(rawBytes_.data() + 12);
    }
    else if (isRFC3964())
    {
        // 6to4 tunneled IPv4: the IPv4 address is in bytes 2-6
        return ReadBE32(rawBytes_.data() + 2);
    }
    else if (isRFC4380())
    {
        // Teredo tunneled IPv4: the IPv4 address is in the last 4 bytes of the
        // address, but bitflipped
        return ~ReadBE32(rawBytes_.data() + 12);
    }
    assert(false);
    return 0;
}

std::uint64_t
NetGroup::calculateKeyedNetGroup() const
{
    std::vector<u_char> netGroup(getGroup());
    CSipHasher hasher(0x1337, 0x1337);
    return hasher.Write(RANDOMIZER_ID_NETGROUP)
        .Write(netGroup.data(), netGroup.size())
        .Finalize();
}

}  // namespace ripple