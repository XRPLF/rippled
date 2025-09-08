//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_CONCEPTS_H_INCLUDED
#define RIPPLE_PROTOCOL_CONCEPTS_H_INCLUDED

#include <xrpl/protocol/UintTypes.h>

#include <type_traits>

namespace ripple {

class STAmount;
class Asset;
class Issue;
class MPTIssue;
class IOUAmount;
class XRPAmount;
class MPTAmount;

template <typename A>
concept StepAmount = std::is_same_v<A, XRPAmount> ||
    std::is_same_v<A, IOUAmount> || std::is_same_v<A, MPTAmount>;

template <typename TIss>
concept ValidIssueType =
    std::is_same_v<TIss, Issue> || std::is_same_v<TIss, MPTIssue>;

template <typename A>
concept AssetType =
    std::is_convertible_v<A, Asset> || std::is_convertible_v<A, Issue> ||
    std::is_convertible_v<A, MPTIssue> || std::is_convertible_v<A, MPTID>;

template <typename T>
concept ValidPathAsset =
    (std::is_same_v<T, Currency> || std::is_same_v<T, MPTID>);

template <class TTakerPays, class TTakerGets>
concept ValidTaker =
    ((std::is_same_v<TTakerPays, IOUAmount> ||
      std::is_same_v<TTakerPays, XRPAmount> ||
      std::is_same_v<TTakerPays, MPTAmount> ||
      std::is_same_v<TTakerGets, IOUAmount> ||
      std::is_same_v<TTakerGets, XRPAmount> ||
      std::is_same_v<TTakerGets, MPTAmount>) &&
     (!std::is_same_v<TTakerPays, XRPAmount> ||
      !std::is_same_v<TTakerGets, XRPAmount>));

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_CONCEPTS_H_INCLUDED
