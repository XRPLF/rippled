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

class HashMapTests : public UnitTest
{
public:
    enum
    {
        numberOfItems = 100 * 1000
    };

    template <int keyBytes>
    class TestTraits
    {
    public:
        struct Value
        {
            int unused;
        };

        struct Key
        {
            class Equal
            {
            public:
                bool operator() (Key const& lhs, Key const& rhs) const noexcept
                {
                    return memcmp (lhs.data, rhs.data, keyBytes) == 0;
                }
            };

            // stateful hardened hash
            class Hash
            {
            public:
                explicit Hash (HashValue seedToUse = Random::getSystemRandom ().nextInt ())
                    : m_seed (seedToUse)
                {
                }

                HashValue generateHash (Key const& key) const noexcept
                {
                    HashValue hash;
                    Murmur::Hash (key.data, keyBytes, m_seed, &hash);
                    return hash;
                }

            private:
                HashValue m_seed;
            };

            /*
            Key ()
                : std::memset (data, 0, keyBytes)
            {
            }
            */

            uint8 data [keyBytes];
        };

        typedef Key                 key_type;
        typedef Value               value_type;
        typedef typename Key::Hash  hasher;
        typedef typename Key::Equal key_equal;
        typedef std::size_t         size_type;

        TestTraits (size_type const numberOfKeys, Random& random)
        {
            // need to static_bassert keyBytes can represent numberOfKeys. Log base 256 or something?

            m_keys.reserve (numberOfKeys);
            m_shuffled_keys.reserve (numberOfKeys);
            for (size_type i = 0; i < numberOfKeys; ++i)
            {
                // VFALCO NOTE std::vector is garbage..want to emplace_back() here
                Key key;
                memset (key.data, 0, sizeof (key.data));
                memcpy (& key.data [0], &i, std::min (sizeof (key.data), sizeof (i)));
                m_keys.push_back (key);
                m_shuffled_keys.push_back (&m_keys [i]);
            }

            UnitTestUtilities::repeatableShuffle (numberOfKeys, m_shuffled_keys, random);
        }

        Key const& getKey (size_type index) const noexcept
        {
            return *m_shuffled_keys [index]; 
        }

    private:
        std::vector <Key>  m_keys;
        std::vector <Key*> m_shuffled_keys;
    };

    template <int keyBytes>
    void testInsert (std::size_t numberOfKeys, Random& random)
    {
        beginTestCase (String
            ("insertion, numberOfKeys = ") + String::fromNumber (numberOfKeys) +
            ", keyBytes = " + String::fromNumber (keyBytes));

        typedef TestTraits <keyBytes> Traits;
        Traits traits (numberOfKeys, random);

        typedef HashMap <
            typename Traits::key_type,
            typename Traits::value_type,
            typename Traits::hasher,
            typename Traits::key_equal> Map;
        Map map;

        for (std::size_t i = 0; i < numberOfKeys; ++i)
            map.insert (traits.getKey (i));

        String s (
            "load_factor = " + String::fromNumber (map.load_factor (), 2) +
            ", bucket_count = " + String::fromNumber (map.bucket_count ()));
        this->logMessage (s);

        expect (map.size () == numberOfKeys);
    }

    void runTest ()
    {
        int64 const seedValue = 072472;
        Random random (seedValue);
        testInsert <4> (numberOfItems, random);
        testInsert <20> (numberOfItems, random);
    }

    HashMapTests () : UnitTest ("HashMap", "beast")
    {
    }
};

static HashMapTests hashMapTests;
