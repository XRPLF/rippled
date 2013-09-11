//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_HASHMAPS_H
#define RIPPLE_HASHMAPS_H

/** Management helper of hash functions used in hash map containers.

    The nonce is used to prevent attackers from feeding carefully crafted
    inputs in order to cause denegerate hash map data structures. This is
    done by seeding the hashing function with a random number generated
    at program startup.
*/
class HashMaps : public Uncopyable
{
public:
    /** Golden ratio constant used in hashing functions.

        The magic number is supposed to be 32 random bits, where each is
        equally likely to be 0 or 1, and with no simple correlation between
        the bits. A common way to find a string of such bits is to use the
        binary expansion of an irrational number; in this case, that number
        is the reciprocal of the golden ratio:

        @code

        phi = (1 + sqrt(5)) / 2
        2^32 / phi = 0x9e3779b9

        @endcode

        References:

            http://stackoverflow.com/questions/4948780/magic-number-in-boosthash-combine
            http://burtleburtle.net/bob/hash/doobs.html
    */
    static std::size_t const goldenRatio = 0x9e3779b9;

    /** Retrieve the singleton.

        @return The global instance of the singleton.
    */
    static HashMaps const& getInstance ()
    {
        static HashMaps instance;

        return instance;
    }

    /** Instantiate a nonce for a type.

        @note This may be used during program initialization
              to avoid concurrency issues. Only C++11 provides thread
              safety guarantees for function-local static objects.
    */
    template <class T>
    void initializeNonce () const
    {
        getNonceHolder <T> ();
    }

    /** Get the nonce for a type.

        The nonces are generated when they are first used.
        This code is thread safe.
    */
    template <class T>
    T getNonce () const
    {
        return getNonceHolder <T> ().getNonce ();
    }

private:
    HashMaps ()
    {
    }

    ~HashMaps ()
    {
    }

    /** Creates and holds a nonce for a type.
    */
    template <class T>
    class NonceHolder
    {
    public:
        NonceHolder ()
        {
            // VFALCO NOTE this can be dangerous if T is an object type
            RandomNumbers::getInstance ().fill (&m_nonce);
        }

        inline T getNonce () const
        {
            return m_nonce;
        }

    private:
        T m_nonce;
    };

    /** Retrieve the nonce holder for a type.

        @note This routine will be called concurrently.
    */
    template <class T>
    NonceHolder <T> const& getNonceHolder () const
    {
        static NonceHolder <T> nonceHolder;

        return nonceHolder;
    }
};

#endif
