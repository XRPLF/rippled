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

/*

TODO

- Check consistency / range checking on read

- Cache top level tree nodes

- Coalesce I/O in RandomAccessFile

- Delete / file compaction

*/

class KeyvaDBImp : public KeyvaDB
{
private:
    // These are stored in big endian format in the file.

    // A file offset.
    typedef int64 FileOffset;

    // Index of a key.
    //
    // The value is broken up into two parts. The key block index,
    // and a 1 based index within the keyblock corresponding to the
    // internal key number.
    //
    typedef int32 KeyIndex;
    typedef int32 KeyBlockIndex;

    // Size of a value.
    typedef uint32 ByteSize;

private:
    // returns the number of keys in a key block with the specified depth
    static int calcKeysAtDepth (int depth)
    {
        return (1U << depth) - 1;
    }

    // returns the number of bytes in a key record
    static int calcKeyRecordBytes (int keyBytes)
    {
        // This depends on the format of a serialized key record
        return
            sizeof (FileOffset) +
            sizeof (ByteSize) +
            sizeof (KeyIndex) +
            sizeof (KeyIndex) +
            keyBytes
            ;
    }

    // returns the number of bytes in a key block
    static int calcKeyBlockBytes (int depth, int keyBytes)
    {
        return calcKeysAtDepth (depth) * calcKeyRecordBytes (keyBytes);
    }

public:
    enum
    {
        currentVersion = 1
    };


    //--------------------------------------------------------------------------

    struct KeyAddress
    {
        // 1 based key block number
        uint32 blockNumber;

        // 1 based key index within the block, breadth-first left to right
        uint32 keyNumber;
    };

    enum
    {
        // The size of the fixed area at the beginning of the key file.
        // This is used to store some housekeeping information like the
        // key size and version number.
        //
        masterHeaderBytes = 1000
    };

    // The master record is at the beginning of the key file
    struct MasterRecord
    {
        // version number, starting from 1
        int32 version;

        KeyBlockIndex nextKeyBlockIndex;

        void write (OutputStream& stream)
        {
            stream.writeTypeBigEndian (version);
        }

        void read (InputStream& stream)
        {
            stream.readTypeBigEndianInto (&version);
        }
    };

    // Key records are indexed starting at one.
    struct KeyRecord : public Uncopyable
    {
        explicit KeyRecord (void* const keyStorage)
            : key (keyStorage)
        {
        }

        // Absolute byte FileOffset in the value file.
        FileOffset valFileOffset;

        // Size of the corresponding value, in bytes.
        ByteSize valSize;

        // Key record index of left node, or 0.
        KeyIndex leftIndex;

        // Key record index of right node, or 0.
        KeyIndex rightIndex;

        // Points to keyBytes storage of the key.
        void* const key;
    };

    //--------------------------------------------------------------------------

    // A complete keyblock. The contents of the memory for the key block
    // are identical to the format on disk. Therefore it is necessary to
    // use the serialization routines to extract or update the key records.
    //
    class KeyBlock : public Uncopyable
    {
    public:
        KeyBlock (int depth, int keyBytes)
            : m_depth  (depth)
            , m_keyBytes (keyBytes)
            , m_storage (calcKeyBlockBytes (depth, keyBytes))
        {
        }

        void read (InputStream& stream)
        {
            stream.read (m_storage.getData (), calcKeyBlockBytes (m_depth, m_keyBytes));
        }

        void write (OutputStream& stream)
        {
            stream.write (m_storage.getData (), calcKeyBlockBytes (m_depth, m_keyBytes));
        }

        void readKeyRecord (KeyRecord* keyRecord, int keyIndex)
        {
            bassert (keyIndex >=1 && keyIndex <= calcKeysAtDepth (m_depth));

            size_t const byteOffset = (keyIndex - 1) * calcKeyRecordBytes (m_keyBytes);

            MemoryInputStream stream (
                addBytesToPointer (m_storage.getData (), byteOffset),
                calcKeyRecordBytes (m_keyBytes),
                false);

            stream.readTypeBigEndianInto (&keyRecord->valFileOffset);
            stream.readTypeBigEndianInto (&keyRecord->valSize);
            stream.readTypeBigEndianInto (&keyRecord->leftIndex);
            stream.readTypeBigEndianInto (&keyRecord->rightIndex);
            stream.read (keyRecord->key, m_keyBytes);
        }

#if 0
        void writeKeyRecord (KeyRecord const& keyRecord, int keyIndex)
        {
            bassert (keyIndex >=1 && keyIndex <= calcKeysAtDepth (m_depth));

#if 0
            size_t const byteOffset = (keyIndex - 1) * calcKeyRecordBytes (m_keyBytes);

            MemoryOutputStream stream (
                addBytesToPointer (m_storage.getData (), byteOffset),
                calcKeyRecordBytes (m_keyBytes));

            stream.writeTypeBigEndian (keyRecord.valFileOffset);
            stream.writeTypeBigEndian (keyRecord.valSize);
            stream.writeTypeBigEndian (keyRecord.leftIndex);
            stream.writeTypeBigEndian (keyRecord.rightIndex);
            stream.write (keyRecord.key, m_keyBytes);
#endif
        }
#endif

    private:
        int const m_depth;
        int const m_keyBytes;
        MemoryBlock m_storage;
    };

    //--------------------------------------------------------------------------

    // Concurrent data
    //
    struct State
    {
        RandomAccessFile keyFile;
        RandomAccessFile valFile;
        MasterRecord masterRecord;
        KeyIndex newKeyIndex;
        FileOffset valFileSize;

        bool hasKeys () const noexcept
        {
            return newKeyIndex > 1;
        }
    };

    typedef SharedData <State> SharedState;

    //--------------------------------------------------------------------------

    int const m_keyBytes;
    int const m_keyBlockDepth;
    SharedState m_state;
    HeapBlock <char> m_keyStorage;

    //--------------------------------------------------------------------------

    KeyvaDBImp (int keyBytes,
                int keyBlockDepth,
                File keyPath,
                File valPath)
        : m_keyBytes (keyBytes)
        , m_keyBlockDepth (keyBlockDepth)
        , m_keyStorage (keyBytes)
    {
        SharedState::Access state (m_state);

        openFile (&state->keyFile, keyPath);

        int64 const fileSize = state->keyFile.getFile ().getSize ();

        if (fileSize == 0)
        {
            // VFALCO TODO Better error handling here
            // initialize the key file
            Result result = state->keyFile.setPosition (masterHeaderBytes - 1);
            if (result.wasOk ())
            {
                char byte = 0;

                result = state->keyFile.write (&byte, 1);

                if (result.wasOk ())
                {
                    state->keyFile.flush ();
                }
            }
        }

        state->newKeyIndex = 1 + static_cast <KeyIndex> ((state->keyFile.getFile ().getSize () - masterHeaderBytes)
                               / calcKeyRecordBytes (m_keyBytes));

        openFile (&state->valFile, valPath);

        state->valFileSize = state->valFile.getFile ().getSize ();
    }

    ~KeyvaDBImp ()
    {
        SharedState::Access state (m_state);

        flushInternal (state);
    }

    // Open a file for reading and writing.
    // Creates the file if it doesn't exist.
    static void openFile (RandomAccessFile* file, File path)
    {
        Result const result = file->open (path, RandomAccessFile::readWrite);

        if (! result)
        {
            String s;
            s << "KeyvaDB: Couldn't open " << path.getFileName () << " for writing.";
            Throw (std::runtime_error (s.toStdString ()));
        }
    }

    //--------------------------------------------------------------------------

    Result createMasterRecord (SharedState::Access& state)
    {
        MemoryBlock buffer (masterHeaderBytes, true);

        Result result = state->keyFile.setPosition (0);

        if (result.wasOk ())
        {
            //MasterRecord mr;

            //mr.version = 1;

            result = state->keyFile.write (buffer.getData (), buffer.getSize ());
        }

        return result;
    }

    //--------------------------------------------------------------------------

    FileOffset calcKeyRecordOffset (KeyIndex keyIndex)
    {
        bassert (keyIndex > 0);

        FileOffset const byteOffset = masterHeaderBytes + (keyIndex - 1) * calcKeyRecordBytes (m_keyBytes);

        return byteOffset;
    }

    // Read a key record into memory.
    // VFALCO TODO Return a Result and do validity checking on all inputs
    //
    void readKeyRecord (KeyRecord* const keyRecord,
                        KeyIndex const keyIndex,
                        SharedState::Access& state)
    {
        FileOffset const byteOffset = calcKeyRecordOffset (keyIndex);

        Result result = state->keyFile.setPosition (byteOffset);

        if (result.wasOk ())
        {
            MemoryBlock data (calcKeyRecordBytes (m_keyBytes));

            size_t bytesRead;

            result = state->keyFile.read (data.getData (), calcKeyRecordBytes (m_keyBytes), &bytesRead);

            if (result.wasOk ())
            {
                if (bytesRead == static_cast <size_t> (calcKeyRecordBytes (m_keyBytes)))
                {
                    MemoryInputStream stream (data, false);

                    // This defines the file format!
                    stream.readTypeBigEndianInto (&keyRecord->valFileOffset);
                    stream.readTypeBigEndianInto (&keyRecord->valSize);
                    stream.readTypeBigEndianInto (&keyRecord->leftIndex);
                    stream.readTypeBigEndianInto (&keyRecord->rightIndex);

                    // Grab the key
                    stream.read (keyRecord->key, m_keyBytes);
                }
                else
                {
                    result = Result::fail ("KeyvaDB: amountRead != calcKeyRecordBytes()");
                }
            }
        }

        if (! result.wasOk ())
        {
            String s;
            s << "KeyvaDB readKeyRecord failed in " << state->keyFile.getFile ().getFileName ();
            Throw (std::runtime_error (s.toStdString ()));
        }
    }

    // Write a key record from memory
    void writeKeyRecord (KeyRecord const& keyRecord,
                         KeyIndex const keyIndex,
                         SharedState::Access& state,
                         bool includingKey)
    {
        FileOffset const byteOffset = calcKeyRecordOffset (keyIndex);

        int const bytes = calcKeyRecordBytes (m_keyBytes) - (includingKey ? 0 : m_keyBytes);

        // VFALCO TODO Recycle this buffer
        MemoryBlock data (bytes);

        {
            MemoryOutputStream stream (data, false);

            // This defines the file format!
            stream.writeTypeBigEndian (keyRecord.valFileOffset);
            stream.writeTypeBigEndian (keyRecord.valSize);
            stream.writeTypeBigEndian (keyRecord.leftIndex);
            stream.writeTypeBigEndian (keyRecord.rightIndex);

            // Write the key
            if (includingKey)
                stream.write (keyRecord.key, m_keyBytes);
        }

        Result result = state->keyFile.setPosition (byteOffset);

        if (result.wasOk ())
        {
            size_t bytesWritten;

            result = state->keyFile.write (data.getData (), bytes, &bytesWritten);

            if (result.wasOk ())
            {
                if (bytesWritten != static_cast <size_t> (bytes))
                {
                    result = Result::fail ("KeyvaDB: bytesWritten != bytes");
                }
            }
        }

        if (!result.wasOk ())
        {
            String s;
            s << "KeyvaDB: writeKeyRecord failed in " << state->keyFile.getFile ().getFileName ();
            Throw (std::runtime_error (s.toStdString ()));
        }
    }

    // Append a value to the value file.
    // VFALCO TODO return a Result
    void writeValue (void const* const value, ByteSize valueBytes, SharedState::Access& state)
    {
        Result result = state->valFile.setPosition (state->valFileSize);

        if (result.wasOk ())
        {
            size_t bytesWritten;

            result = state->valFile.write (value, valueBytes, &bytesWritten);

            if (result.wasOk ())
            {
                if (bytesWritten == valueBytes)
                {
                    state->valFileSize += valueBytes;
                }
                else
                {
                    result = Result::fail ("KeyvaDB: bytesWritten != valueBytes");
                }
            }
        }

        if (! result.wasOk ())
        {
            String s;
            s << "KeyvaDB: writeValue failed in " << state->valFile.getFile ().getFileName ();
            Throw (std::runtime_error (s.toStdString ()));
        }
    }

    //--------------------------------------------------------------------------

    struct FindResult : public Uncopyable
    {
        FindResult (void* const keyStorage)
            : keyRecord (keyStorage)
        {
        }

        int compare;         // result of the last comparison
        KeyIndex keyIndex;   // index we looked at last
        //KeyBlock keyBlock;   // KeyBlock we looked at last
        KeyRecord keyRecord; // KeyRecord we looked at last
    };

    // Find a key. If the key doesn't exist, enough information
    // is left behind in the result to perform an insertion.
    //
    // Returns true if the key was found.
    //
    bool find (FindResult* findResult, void const* key, SharedState::Access& state)
    {
        // Not okay to call this with an empty key file!
        bassert (state->hasKeys ());

        // This performs a standard binary search

        findResult->keyIndex = 1;

        do
        {
            readKeyRecord (&findResult->keyRecord, findResult->keyIndex, state);

            findResult->compare = memcmp (key, findResult->keyRecord.key, m_keyBytes);

            if (findResult->compare < 0)
            {
                if (findResult->keyRecord.leftIndex != 0)
                {
                    // Go left
                    findResult->keyIndex = findResult->keyRecord.leftIndex;
                }
                else
                {
                    // Insert position is to the left
                    break;
                }
            }
            else if (findResult->compare > 0)
            {
                if (findResult->keyRecord.rightIndex != 0)
                {
                    // Go right
                    findResult->keyIndex = findResult->keyRecord.rightIndex;
                }
                else
                {
                    // Insert position is to the right
                    break;
                }
            }
        }
        while (findResult->compare != 0);

        return findResult->compare == 0;
    }

    //--------------------------------------------------------------------------

    bool get (void const* key, GetCallback* callback)
    {
        FindResult findResult (m_keyStorage.getData ());

        SharedState::Access state (m_state);

        bool found = false;

        if (state->hasKeys ())
        {
            found = find (&findResult, key, state);

            if (found)
            {
                void* const destStorage = callback->getStorageForValue (findResult.keyRecord.valSize);

                Result result = state->valFile.setPosition (findResult.keyRecord.valFileOffset);

                if (result.wasOk ())
                {
                    size_t bytesRead;

                    result = state->valFile.read (destStorage, findResult.keyRecord.valSize, &bytesRead);

                    if (result.wasOk ())
                    {
                        if (bytesRead != findResult.keyRecord.valSize)
                        {
                            result = Result::fail ("KeyvaDB: bytesRead != valSize");
                        }
                    }
                }

                if (! result.wasOk ())
                {
                    String s;
                    s << "KeyvaDB: get in " << state->valFile.getFile ().getFileName ();
                    Throw (std::runtime_error (s.toStdString ()));
                }
            }
        }

        return found;
    }

    //--------------------------------------------------------------------------

    // Write a key value pair. Does nothing if the key exists.
    void put (void const* key, void const* value, int valueBytes)
    {
        bassert (valueBytes > 0);

        SharedState::Access state (m_state);

        if (state->hasKeys ())
        {
            // Search for the key

            FindResult findResult (m_keyStorage.getData ());

            bool const found = find (&findResult, key, state);

            if (! found )
            {
                bassert (findResult.compare != 0);

                // Binary tree insertion.
                // Link the last key record to the new key
                {
                    if (findResult.compare < 0)
                    {
                        findResult.keyRecord.leftIndex = state->newKeyIndex;
                    }
                    else
                    {
                        findResult.keyRecord.rightIndex = state->newKeyIndex;
                    }

                    writeKeyRecord (findResult.keyRecord, findResult.keyIndex, state, false);
                }

                // Write the new key
                {
                    findResult.keyRecord.valFileOffset = state->valFileSize;
                    findResult.keyRecord.valSize = valueBytes;
                    findResult.keyRecord.leftIndex = 0;
                    findResult.keyRecord.rightIndex = 0;

                    memcpy (findResult.keyRecord.key, key, m_keyBytes);

                    writeKeyRecord (findResult.keyRecord, state->newKeyIndex, state, true);
                }

                // Key file has grown by one.
                ++state->newKeyIndex;

                // Write the value
                writeValue (value, valueBytes, state);
            }
            else
            {
                // Key already exists, do nothing.
                // We could check to make sure the payloads are the same.
            }
        }
        else
        {
            //
            // Write first key
            //

            KeyRecord keyRecord (m_keyStorage.getData ());

            keyRecord.valFileOffset = state->valFileSize;
            keyRecord.valSize = valueBytes;
            keyRecord.leftIndex = 0;
            keyRecord.rightIndex = 0;

            memcpy (keyRecord.key, key, m_keyBytes);

            writeKeyRecord (keyRecord, state->newKeyIndex, state, true);

            // Key file has grown by one.
            ++state->newKeyIndex;

            //
            // Write value
            //

            bassert (state->valFileSize == 0);

            writeValue (value, valueBytes, state);
        }
    }

    //--------------------------------------------------------------------------

    void flush ()
    {
        SharedState::Access state (m_state);

        flushInternal (state);
    }

    void flushInternal (SharedState::Access& state)
    {
        state->keyFile.flush ();
        state->valFile.flush ();
    }
};

KeyvaDB* KeyvaDB::New (int keyBytes, int keyBlockDepth, File keyPath, File valPath)
{
    return new KeyvaDBImp (keyBytes, keyBlockDepth, keyPath, valPath);
}

//------------------------------------------------------------------------------

class KeyvaDBTests : public UnitTest
{
public:
    enum
    {
        maxPayloadBytes = 8 * 1024
    };

    // Retrieval callback stores the value in a Payload object for comparison
    struct PayloadGetCallback : KeyvaDB::GetCallback
    {
        UnitTestUtilities::Payload payload;

        PayloadGetCallback () : payload (maxPayloadBytes)
        {
        }

        void* getStorageForValue (int valueBytes)
        {
            bassert (valueBytes <= maxPayloadBytes);

            payload.bytes = valueBytes;

            return payload.data.getData ();
        }
    };

    KeyvaDB* createDB (unsigned int keyBytes, File const& path)
    {
        File const keyPath = path.withFileExtension (".key");
        File const valPath = path.withFileExtension (".val");

        return KeyvaDB::New (keyBytes, 1, keyPath, valPath);
    }

    void deleteDBFiles (File const& path)
    {
        File const keyPath = path.withFileExtension (".key");
        File const valPath = path.withFileExtension (".val");

        keyPath.deleteFile ();
        valPath.deleteFile ();
    }

    template <size_t KeyBytes>
    void testKeySize (unsigned int const maxItems)
    {
        using namespace UnitTestUtilities;

        typedef UnsignedInteger <KeyBytes> KeyType;

        int64 const seedValue = 50;

        String s;

        s << "keyBytes=" << String (uint64(KeyBytes)) << ", maxItems=" << String (maxItems);
        beginTestCase (s);

        // Set up the key and value files
        File const path (File::createTempFile (""));

        {
            // open the db
            ScopedPointer <KeyvaDB> db (createDB (KeyBytes, path));

            Payload payload (maxPayloadBytes);
            Payload check (maxPayloadBytes);

            {
                // Create an array of ascending integers.
                HeapBlock <unsigned int> items (maxItems);
                for (unsigned int i = 0; i < maxItems; ++i)
                    items [i] = i;

                // Now shuffle it deterministically.
                repeatableShuffle (maxItems, items, seedValue);

                // Write all the keys of integers.
                for (unsigned int i = 0; i < maxItems; ++i)
                {
                    unsigned int keyIndex = items [i];

                    KeyType const key = KeyType::createFromInteger (keyIndex);

                    payload.repeatableRandomFill (1, maxPayloadBytes, keyIndex + seedValue);

                    db->put (key.cbegin (), payload.data.getData (), payload.bytes);

                    {
                        // VFALCO TODO Check what we just wrote?
                        //db->get (key.cbegin (), check.data.getData (), payload.bytes);
                    }
                }
            }

            {
                // Go through all of our keys and try to retrieve them.
                // since this is done in ascending order, we should get
                // random seeks at this point.
                //
                PayloadGetCallback cb;
                for (unsigned int keyIndex = 0; keyIndex < maxItems; ++keyIndex)
                {
                    KeyType const v = KeyType::createFromInteger (keyIndex);

                    bool const found = db->get (v.cbegin (), &cb);

                    expect (found, "Should be found");

                    if (found)
                    {
                        payload.repeatableRandomFill (1, maxPayloadBytes, keyIndex + seedValue);

                        expect (payload == cb.payload, "Should be equal");
                    }
                }
            }
        }

        {
            // Re-open the database and confirm the data
            ScopedPointer <KeyvaDB> db (createDB (KeyBytes, path));

            Payload payload (maxPayloadBytes);
            Payload check (maxPayloadBytes);

            PayloadGetCallback cb;
            for (unsigned int keyIndex = 0; keyIndex < maxItems; ++keyIndex)
            {
                KeyType const v = KeyType::createFromInteger (keyIndex);

                bool const found = db->get (v.cbegin (), &cb);

                expect (found, "Should be found");

                if (found)
                {
                    payload.repeatableRandomFill (1, maxPayloadBytes, keyIndex + seedValue);

                    expect (payload == cb.payload, "Should be equal");
                }
            }
        }

        deleteDBFiles (path);
    }

    void runTest ()
    {
        testKeySize <4> (500);
        testKeySize <32> (4000);
    }

    KeyvaDBTests () : UnitTest ("KeyvaDB", "beast", runManual)
    {
    }
};

static KeyvaDBTests keyvaDBTests;
