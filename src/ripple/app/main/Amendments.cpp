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

#include <string>
#include <vector>

namespace ripple {

namespace detail {

/** Amendments that this server supports and enables by default */
std::vector<std::string>
preEnabledAmendments ()
{
    return
    {
    };
}

/** Amendments that this server supports, but doesn't enable by default */
std::vector<std::string>
supportedAmendments ()
{
    return
    {
        { "4C97EBA926031A7CF7D7B36FDE3ED66DDA5421192D63DE53FFB46E43B9DC8373 MultiSign" },
        { "C1B8D934087225F509BEB5A8EC24447854713EE447D277F69545ABFA0E0FD490 Tickets" },
        { "DA1BD556B42D85EA9C84066D028D355B52416734D3283F85E216EA5DA6DB7E13 SusPay" },
        { "6781F8368C4771B83E8B821D88F580202BCB4228075297B19E4FDC5233F1EFDC TrustSetAuth" },
        { "42426C4D4F1009EE67080A9B7965B44656D7714D104A72F9B4369F97ABF044EE FeeEscalation" }
    };
}

}

}

