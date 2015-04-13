//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_OVERLAY_MANIFEST_H_INCLUDED
#define RIPPLE_OVERLAY_MANIFEST_H_INCLUDED

#include <ripple/basics/BasicConfig.h>
#include <ripple/basics/hardened_hash.h>
#include <ripple/protocol/AnyPublicKey.h>
#include <ripple/protocol/STExchange.h>
#include <beast/crypto/base64.h>
#include <beast/http/rfc2616.h>
#include <beast/hash/hash_append.h>
#include <beast/utility/Journal.h>
#include <beast/utility/noexcept.h>
#include <ed25519-donna/ed25519.h>
#include <boost/unordered_map.hpp>
#include <boost/optional.hpp>
#include <cstring>
#include <unordered_map>

namespace ripple {

//------------------------------------------------------------------------------

struct Manifest
{
    std::string serialized;
    AnyPublicKey masterKey;
    AnyPublicKey signingKey;
    std::size_t seq;

    Manifest(std::string s, AnyPublicKey pk, AnyPublicKey spk, std::size_t seq)
        : serialized(s)
        , masterKey(pk)
        , signingKey(spk)
        , seq(seq)
    {
    }
};

/** Remembers manifests with the highest sequence number. */
class ManifestCache
{
private:
    struct MappedType
    {
        MappedType() = default;

        std::string comment;
        boost::optional<Manifest> m;
    };

    using MapType = boost::unordered_map<
        AnyPublicKey, MappedType, hardened_hash<>>;

    mutable std::mutex mutex_;
    MapType map_;

public:
    ManifestCache() = default;
    ManifestCache (ManifestCache const&) = delete;
    ManifestCache& operator= (ManifestCache const&) = delete;
    ~ManifestCache() = default;

    void configValidatorKey(std::string const& line, beast::Journal const& journal);
    void configManifest(std::string const& s, beast::Journal const& journal);

    void insertTrustedKey (AnyPublicKey const& pk, std::string const& comment);

    bool isTrustedKey (AnyPublicKey const& pk) const;

    // Returns true if seq introduces a higher sequence number for pk
    bool
    would_accept (AnyPublicKey const& pk, std::size_t seq) const;

    // Returns `true` if its a new, verified manifest
    bool
    maybe_insert (AnyPublicKey const& pk, std::size_t seq,
        std::string const& s, beast::Journal const& journal);

    // Returns `true` if its a new, verified manifest
    bool
    maybe_accept (AnyPublicKey const& pk, std::size_t seq,
        std::string const& s, STObject const& st, beast::Journal const& journal);

    // A "for_each" for populated manifests only
    template <class Function>
    void
    for_each_manifest(Function&& f) const
    {
        std::lock_guard<std::mutex> lock (mutex_);
        for (auto const& e : map_)
        {
            if (auto const& m = e.second.m)
                f(*m);
        }
    }
};

} // ripple

#endif
