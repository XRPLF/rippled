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

#ifndef CPPTL_JSON_FEATURES_H_INCLUDED
#define CPPTL_JSON_FEATURES_H_INCLUDED

namespace Json
{

/** \brief Configuration passed to reader and writer.
 * This configuration object can be used to force the Reader or Writer
 * to behave in a standard conforming way.
 */
class JSON_API Features
{
public:
    /** \brief A configuration that allows all features and assumes all strings are UTF-8.
     * - C & C++ comments are allowed
     * - Root object can be any JSON value
     * - Assumes Value strings are encoded in UTF-8
     */
    static Features all ();

    /** \brief A configuration that is strictly compatible with the JSON specification.
     * - Comments are forbidden.
     * - Root object must be either an array or an object value.
     * - Assumes Value strings are encoded in UTF-8
     */
    static Features strictMode ();

    /** \brief Initialize the configuration like JsonConfig::allFeatures;
     */
    Features ();

    /// \c true if comments are allowed. Default: \c true.
    bool allowComments_;

    /// \c true if root must be either an array or an object value. Default: \c false.
    bool strictRoot_;
};

} // namespace Json

#endif // CPPTL_JSON_FEATURES_H_INCLUDED
