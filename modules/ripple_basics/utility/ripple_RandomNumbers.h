//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_RANDOMNUMBERS_H
#define RIPPLE_RANDOMNUMBERS_H

/** Cryptographically secure random number source.
*/
class RandomNumbers
{
public:
    /** Retrieve the instance of the generator.
    */
    static RandomNumbers& getInstance ();

    /** Initialize the generator.

        If the generator is not manually initialized, it will be
        automatically initialized on first use. If automatic initialization
        fails, an exception is thrown.

        @return `true` if enough entropy could be retrieved.
    */
    bool initialize ();

    /** Generate secure random numbers.

        The generated data is suitable for cryptography.

        @invariant The destination buffer must be large enough or
                   undefined behavior results.

        @param destinationBuffer The place to store the bytes.
        @param numberOfBytes The number of bytes to generate.
    */
    void fillBytes (void* destinationBuffer, int numberOfBytes);

    /** Generate secure random numbers.

        The generated data is suitable for cryptography.

        Fills the memory for the object with random numbers.
        This is a type-safe alternative to the function above.

        @param object A pointer to the object to fill.

        @tparam T The type of `object`

        @note Undefined behavior results if `T` is not a POD type.
    */
    template <class T>
    void fill (T* object)
    {
        fillBytes (object, sizeof (T));
    }

private:
    RandomNumbers ();

    ~RandomNumbers ();

    bool platformAddEntropy ();

    void platformAddPerformanceMonitorEntropy ();

private:
    bool m_initialized;
};

#endif
