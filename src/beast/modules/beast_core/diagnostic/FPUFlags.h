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

#ifndef BEAST_FPUFLAGS_H_INCLUDED
#define BEAST_FPUFLAGS_H_INCLUDED

/*============================================================================*/
/**
    A set of IEEE FPU flags.

    Description.

    @ingroup beast_core
*/
class BEAST_API FPUFlags
{
public:
    /** An individual FPU flag */
    struct Flag
    {
        Flag () : m_set (false) { }
        Flag (Flag const& flag) : m_set (flag.m_set), m_value (flag.m_value) { }
        Flag& operator= (Flag const& flag)
        {
            m_set = flag.m_set;
            m_value = flag.m_value;
            return *this;
        }
        bool is_set () const
        {
            return m_set;
        }
        bool value () const
        {
            assert (m_set);
            return m_value;
        }
        void set_value (bool value)
        {
            m_set = true;
            m_value = value;
        }
        void clear ()
        {
            m_set = false;
        }

    private:
        bool m_set : 1;
        bool m_value : 1;
    };

    /** A multi-valued FPU setting */
    template <typename Constants>
    struct Enum
    {
        Enum () : m_set (false) { }
        Enum (Enum const& value) : m_set (value.m_set), m_value (value.m_value) { }
        Enum& operator= (Enum const& value)
        {
            m_set = value.m_set;
            m_value = value.m_value;
            return *this;
        }
        bool is_set () const
        {
            return m_set;
        }
        Constants value () const
        {
            return m_value;
        }
        void set_value (Constants value)
        {
            m_set = true;
            m_value = value;
        }
        void clear ()
        {
            m_set = false;
        }

    private:
        bool m_set : 1;
        Constants m_value;
    };

public:
    //
    // Exception masks
    //

    void setMaskNaNs        (bool mask = true)
    {
        m_maskNaNs.set_value (mask);
    }
    void setMaskDenormals   (bool mask = true)
    {
        m_maskDenormals.set_value (mask);
    }
    void setMaskZeroDivides (bool mask = true)
    {
        m_maskZeroDivides.set_value (mask);
    }
    void setMaskOverflows   (bool mask = true)
    {
        m_maskOverflows.set_value (mask);
    }
    void setMaskUnderflows  (bool mask = true)
    {
        m_maskUnderflows.set_value (mask);
    }
    //void setMaskInexacts    (bool mask = true) { m_maskInexacts.set_value (mask); }

    void setUnmaskAllExceptions (bool unmask = true)
    {
        setMaskNaNs (!unmask);
        setMaskDenormals (!unmask);
        setMaskZeroDivides (!unmask);
        setMaskOverflows (!unmask);
        setMaskUnderflows (!unmask);
        //setMaskInexacts (!unmask);
    }

    //
    // Denormal control
    //

    void setFlushDenormals (bool flush = true)
    {
        m_flushDenormals.set_value (flush);
    }

    //
    // Infinity control
    //

    void setInfinitySigned (bool is_signed = true)
    {
        m_infinitySigned.set_value (is_signed);
    }

    //
    // Rounding control
    //

    enum Rounding
    {
        roundChop,
        roundUp,
        roundDown,
        roundNear
    };

    void setRounding (Rounding rounding)
    {
        m_rounding.set_value (rounding);
    }

    //
    // Precision control
    //

    enum Precision
    {
        bits24,
        bits53,
        bits64
    };

    void setPrecision (Precision precision)
    {
        m_precision.set_value (precision);
    }

    //
    // Retrieval
    //

    const Flag getMaskNaNs () const
    {
        return m_maskNaNs;
    }
    const Flag getMaskDenormals () const
    {
        return m_maskDenormals;
    }
    const Flag getMaskZeroDivides () const
    {
        return m_maskZeroDivides;
    }
    const Flag getMaskOverflows () const
    {
        return m_maskOverflows;
    }
    const Flag getMaskUnderflows () const
    {
        return m_maskUnderflows;
    }
    //const Flag getMaskInexacts () const           { return m_maskInexacts; }
    const Flag getFlushDenormals () const
    {
        return m_flushDenormals;
    }
    const Flag getInfinitySigned () const
    {
        return m_infinitySigned;
    }
    const Enum <Rounding> getRounding () const
    {
        return m_rounding;
    }
    const Enum <Precision> getPrecision () const
    {
        return m_precision;
    }

    Flag& getMaskNaNs ()
    {
        return m_maskNaNs;
    }
    Flag& getMaskDenormals ()
    {
        return m_maskDenormals;
    }
    Flag& getMaskZeroDivides ()
    {
        return m_maskZeroDivides;
    }
    Flag& getMaskOverflows ()
    {
        return m_maskOverflows;
    }
    Flag& getMaskUnderflows ()
    {
        return m_maskUnderflows;
    }
    //Flag& getMaskInexacts ()                      { return m_maskInexacts; }
    Flag& getFlushDenormals ()
    {
        return m_flushDenormals;
    }
    Flag& getInfinitySigned ()
    {
        return m_infinitySigned;
    }
    Enum <Rounding>& getRounding ()
    {
        return m_rounding;
    }
    Enum <Precision>& getPrecision ()
    {
        return m_precision;
    }

    // Clears our flags if they are not set in another object
    void clearUnsetFlagsFrom (FPUFlags const& flags);

    // Retrieve the current flags fron the FPU
    static FPUFlags getCurrent ();

    // Change the current FPU flags based on what is set in flags
    static void setCurrent (FPUFlags const& flags);

private:
    Flag m_maskNaNs;
    Flag m_maskDenormals;
    Flag m_maskZeroDivides;
    Flag m_maskOverflows;
    Flag m_maskUnderflows;
    //Flag m_maskInexacts;
    Flag m_flushDenormals;
    Flag m_infinitySigned;
    Enum <Rounding> m_rounding;
    Enum <Precision> m_precision;
};

//------------------------------------------------------------------------------

/*============================================================================*/
/**
    IEEE FPU flag modifications with scoped lifetime.

    An instance of the class saves the FPU flags and updates

        FPUFlags flags;
        flags.setUnmaskAllExceptions ();

        {
          ScopedFPUFlags fpu (flags);

          // Perform floating point calculations
        }

        // FPU flags are back to what they were now

    @ingroup beast_core
*/
class ScopedFPUFlags
{
public:
    ScopedFPUFlags (FPUFlags const& flagsToSet)
    {
        m_savedFlags = FPUFlags::getCurrent ();
        m_savedFlags.clearUnsetFlagsFrom (flagsToSet);
        FPUFlags::setCurrent (flagsToSet);
    }

    ~ScopedFPUFlags ()
    {
        FPUFlags::setCurrent (m_savedFlags);
    }

private:
    FPUFlags m_savedFlags;
};

#endif

