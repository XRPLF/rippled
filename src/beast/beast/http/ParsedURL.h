//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_HTTP_PARSEDURL_H_INCLUDED
#define BEAST_HTTP_PARSEDURL_H_INCLUDED

#include <beast/Strings.h>
#include <beast/http/URL.h>

namespace beast {

/** Parses a String containing a URL. */
class ParsedURL
{
public:
    ParsedURL ();
    explicit ParsedURL (String const& url);
    ParsedURL (int error, URL const& url);
    ParsedURL (ParsedURL const& other);
    ParsedURL& operator= (ParsedURL const& other);

    /** Zero for success, else a non zero value indicating failure. */
    int error () const;

    /** The parsed URL if there was no error. */
    URL url () const;

private:
    int m_error;
    URL m_url;
};

}

#endif
