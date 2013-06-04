
//------------------------------------------------------------------------------

#ifndef RIPPLE_HASHMAPS_H
#define RIPPLE_HASHMAPS_H

/** Management helper of hash functions used in hash map containers.

    The nonce is used to prevent attackers from feeding carefully crafted
    inputs in order to cause denegerate hash map data structures. This is
    done by seeding the hashing function with a random number generated
    at program startup.
*/
// VFALCO: TODO derive from Uncopyable
class HashMaps // : beast::Uncopayble
{
public:
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
            // VFALCO: NOTE, this can be dangerous if T is an object type
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
    NonceHolder <T> const& getNonceHolder ()
    {
        static NonceHolder <T> nonceHolder;

        return nonceHolder;
    }
};

#endif
