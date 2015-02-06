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

#ifndef RIPPLE_PROTOCOL_INNER_OBJECT_FORMATS_H_INCLUDED
#define RIPPLE_PROTOCOL_INNER_OBJECT_FORMATS_H_INCLUDED

#include <ripple/protocol/KnownFormats.h>

namespace ripple {

/** Manages the list of known inner object formats.
*/
class InnerObjectFormats : public KnownFormats <int>
{
private:
    void addCommonFields (Item& item);

public:
    /** Create the object.
        This will load the object will all the known inner object formats.
    */
    InnerObjectFormats ();

    static InnerObjectFormats const& getInstance ();

    SOTemplate const* findSOTemplateBySField (SField const& sField) const;
};

} // ripple

#endif
