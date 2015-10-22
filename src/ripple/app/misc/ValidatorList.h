//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2015 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_MISC_VALIDATORLIST_H_INCLUDED
#define RIPPLE_APP_MISC_VALIDATORLIST_H_INCLUDED

#include <ripple/basics/BasicConfig.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/protocol/PublicKey.h>
#include <boost/optional.hpp>
#include <functional>
#include <memory>
#include <mutex>

namespace ripple {

class ValidatorList
{
private:
    /** The non-ephemeral public keys from the configuration file. */
    hash_map<PublicKey, std::string> permanent_;

    /** The ephemeral public keys from manifests. */
    hash_map<PublicKey, std::string> ephemeral_;

    std::mutex mutable mutex_;
    beast::Journal mutable j_;

public:
    explicit
    ValidatorList (beast::Journal j);

    /** Determines whether a node is in the UNL
        @return boost::none if the node isn't a member,
                otherwise, the comment associated with the
                node (which may be an empty string).
    */
    boost::optional<std::string>
    member (
        PublicKey const& identity) const;

    /** Determines whether a node is in the UNL */
    bool
    trusted (
        PublicKey const& identity) const;

    /** Insert a short-term validator key published in a manifest. */
    bool
    insertEphemeralKey (
        PublicKey const& identity,
        std::string const& comment);

    /** Remove a short-term validator revoked in a manifest. */
    bool
    removeEphemeralKey (
        PublicKey const& identity);

    /** Insert a long-term validator key. */
    bool
    insertPermanentKey (
        PublicKey const& identity,
        std::string const& comment);

    /** Remove a long-term validator key. */
    bool
    removePermanentKey (
        PublicKey const& identity);

    /** The number of installed permanent and ephemeral keys */
    std::size_t
    size () const;

    /** Invokes the callback once for every node in the UNL.
        @note You are not allowed to insert or remove any
              nodes in the UNL from within the callback.

        The arguments passed into the lambda are:
            - The public key of the validator;
            - A (possibly empty) comment.
            - A boolean indicating whether this is a
              permanent or ephemeral key;
    */
    void
    for_each (
        std::function<void(PublicKey const&, std::string const&, bool)> func) const;

    /** Load the list of trusted validators.

        The section contains entries consisting of a base58
        encoded validator public key, optionally followed by
        a comment.

        @return false if an entry could not be parsed or
                contained an invalid validator public key,
                true otherwise.
    */
    bool
    load (
        Section const& validators);
};

}

#endif
