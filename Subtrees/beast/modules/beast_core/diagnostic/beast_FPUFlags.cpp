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

void FPUFlags::clearUnsetFlagsFrom (FPUFlags const& flags)
{
    if (!flags.getMaskNaNs ().is_set ())         m_maskNaNs.clear ();

    if (!flags.getMaskDenormals ().is_set ())    m_maskDenormals.clear ();

    if (!flags.getMaskZeroDivides ().is_set ())  m_maskZeroDivides.clear ();

    if (!flags.getMaskOverflows ().is_set ())    m_maskOverflows.clear ();

    if (!flags.getMaskUnderflows ().is_set ())   m_maskUnderflows.clear ();

    //if (!flags.getMaskInexacts().is_set ())     m_maskInexacts.clear ();
    if (!flags.getFlushDenormals ().is_set ())   m_flushDenormals.clear ();

    if (!flags.getInfinitySigned ().is_set ())   m_infinitySigned.clear ();

    if (!flags.getRounding ().is_set ())         m_rounding.clear ();

    if (!flags.getPrecision ().is_set ())        m_precision.clear ();
}
