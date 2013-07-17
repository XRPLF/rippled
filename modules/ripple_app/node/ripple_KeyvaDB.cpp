//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class KeyvaDBImp : public KeyvaDB
{
private:
    // These are stored in big endian format in the file.

    // A file offset.
    typedef int64 FileOffset;

    // Index of a key.
    typedef int32 KeyIndex;

    // Size of a value.
    typedef int32 ByteSize;

private:
    enum
    {
        // The size of the fixed area at the beginning of the key file.
        // This is used to store some housekeeping information like the
        // key size and version number.
        //
        keyFileHeaderBytes = 1024
    };

    // Accessed by multiple threads
    struct State
    {
        State ()
            : keyFile (0)//16384) // buffer size
            , valFile (0)//16384) // buffer size
        {
        }

        RandomAccessFile keyFile;
        RandomAccessFile valFile;
        KeyIndex newKeyIndex;
        FileOffset valFileSize;

        bool hasKeys () const noexcept
        {
            return newKeyIndex > 1;
        }
    };

    typedef SharedData <State> SharedState;

    // Key records are indexed starting at one.
    struct KeyRecord
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

public:
    KeyvaDBImp (int keyBytes,
                File keyPath,
                File valPath,
                bool filesAreTemporary)
        : m_keyBytes (keyBytes)
        , m_keyRecordBytes (getKeyRecordBytes ())
        , m_filesAreTemporary (filesAreTemporary)
        , m_keyStorage (keyBytes)
    {
        SharedState::WriteAccess state (m_state);

        openFile (&state->keyFile, keyPath);

        int64 const fileSize = state->keyFile.getFile ().getSize ();

        if (fileSize == 0)
        {
            // initialize the key file
            RandomAccessFileOutputStream stream (state->keyFile);
            stream.setPosition (keyFileHeaderBytes - 1);
            stream.writeByte (0);
            stream.flush ();
        }

        state->newKeyIndex = 1 + (state->keyFile.getFile ().getSize () - keyFileHeaderBytes) / m_keyRecordBytes;

        openFile (&state->valFile, valPath);

        state->valFileSize = state->valFile.getFile ().getSize ();
    }

    ~KeyvaDBImp ()
    {
        SharedState::WriteAccess state (m_state);

        flushInternal (state);

        // Delete the database files if requested.
        //
        if (m_filesAreTemporary)
        {
            {
                File const path = state->keyFile.getFile ();
                state->keyFile.close ();
                path.deleteFile ();
            }

            {
                File const path = state->valFile.getFile ();
                state->valFile.close ();
                path.deleteFile ();
            }
        }
    }

    //--------------------------------------------------------------------------

    // Returns the number of physical bytes in a key record.
    // This is specific to the format of the data.
    //
    int getKeyRecordBytes () const noexcept
    {
        int bytes = 0;

        bytes += sizeof (FileOffset);       // valFileOffset
        bytes += sizeof (ByteSize);         // valSize
        bytes += sizeof (KeyIndex);         // leftIndex
        bytes += sizeof (KeyIndex);         // rightIndex

        bytes += m_keyBytes;

        return bytes;
    }

    FileOffset calcKeyRecordOffset (KeyIndex keyIndex)
    {
        bassert (keyIndex > 0);

        FileOffset const byteOffset = keyFileHeaderBytes + (keyIndex - 1) * m_keyRecordBytes;

        return byteOffset;
    }

    // Read a key record into memory.
    // VFALCO TODO Return a Result and do validity checking on all inputs
    //
    void readKeyRecord (KeyRecord* const keyRecord,
                        KeyIndex const keyIndex,
                        SharedState::WriteAccess& state)
    {
        FileOffset const byteOffset = calcKeyRecordOffset (keyIndex);

        RandomAccessFileInputStream stream (state->keyFile);

        bool const success = stream.setPosition (byteOffset);

        if (success)
        {
            // This defines the file format!
            keyRecord->valFileOffset = stream.readInt64BigEndian ();
            keyRecord->valSize = stream.readIntBigEndian ();
            keyRecord->leftIndex = stream.readIntBigEndian ();
            keyRecord->rightIndex = stream.readIntBigEndian ();

            // Grab the key
            stream.read (keyRecord->key, m_keyBytes);
        }
        else
        {
            String s;
            s << "KeyvaDB: Seek failed in " << state->keyFile.getFile ().getFileName ();
            Throw (std::runtime_error (s.toStdString ()));
        }
    }

    // Write a key record from memory
    void writeKeyRecord (KeyRecord const& keyRecord,
                         KeyIndex const keyIndex,
                         SharedState::WriteAccess& state,
                         bool includingKey)
    {
        FileOffset const byteOffset = calcKeyRecordOffset (keyIndex);

        RandomAccessFileOutputStream stream (state->keyFile);

        bool const success = stream.setPosition (byteOffset);

        if (success)
        {
            // This defines the file format!
            // VFALCO TODO Make OutputStream return the bool errors here
            //
            stream.writeInt64BigEndian (keyRecord.valFileOffset);
            stream.writeIntBigEndian (keyRecord.valSize);
            stream.writeIntBigEndian (keyRecord.leftIndex);
            stream.writeIntBigEndian (keyRecord.rightIndex);

            // Write the key
            if (includingKey)
            {
                bool const success = stream.write (keyRecord.key, m_keyBytes);

                if (! success)
                {
                    String s;
                    s << "KeyvaDB: Write failed in " << state->keyFile.getFile ().getFileName ();
                    Throw (std::runtime_error (s.toStdString ()));
                }
            }

            //stream.flush ();
        }
        else
        {
            String s;
            s << "KeyvaDB: Seek failed in " << state->keyFile.getFile ().getFileName ();
            Throw (std::runtime_error (s.toStdString ()));
        }
    }

    // Append a value to the value file.
    // VFALCO TODO return a Result
    void writeValue (void const* const value, ByteSize valueBytes, SharedState::WriteAccess& state)
    {
        RandomAccessFileOutputStream stream (state->valFile);

        bool const success = stream.setPosition (state->valFileSize);

        if (success)
        {
            bool const success = stream.write (value, static_cast <size_t> (valueBytes));

            if (! success)
            {
                String s;
                s << "KeyvaDB: Write failed in " << state->valFile.getFile ().getFileName ();
                Throw (std::runtime_error (s.toStdString ()));
            }

            state->valFileSize += valueBytes;

            //stream.flush ();
        }
        else
        {
            String s;
            s << "KeyvaDB: Seek failed in " << state->valFile.getFile ().getFileName ();
            Throw (std::runtime_error (s.toStdString ()));
        }
    }

    //--------------------------------------------------------------------------

    struct FindResult
    {
        FindResult (void* const keyStorage)
            : keyRecord (keyStorage)
        {
        }

        int compare;         // result of the last comparison
        KeyIndex keyIndex;   // index we looked at last
        KeyRecord keyRecord; // KeyRecord we looked at last
    };

    // Find a key. If the key doesn't exist, enough information
    // is left behind in the result to perform an insertion.
    //
    // Returns true if the key was found.
    //
    bool find (FindResult* findResult, void const* key, SharedState::WriteAccess& state)
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
        // VFALCO TODD Swap these two lines
        SharedState::WriteAccess state (m_state);
        FindResult findResult (m_keyStorage.getData ());

        bool found = false;

        if (state->hasKeys ())
        {
            found = find (&findResult, key, state);

            if (found)
            {
                void* const destStorage = callback->createStorageForValue (findResult.keyRecord.valSize);

                RandomAccessFileInputStream stream (state->valFile);

                bool const success = stream.setPosition (findResult.keyRecord.valFileOffset);

                if (! success)
                {
                    String s;
                    s << "KeyvaDB: Seek failed in " << state->valFile.getFile ().getFileName ();
                    Throw (std::runtime_error (s.toStdString ()));
                }

                int const bytesRead = stream.read (destStorage, findResult.keyRecord.valSize);

                if (bytesRead != findResult.keyRecord.valSize)
                {
                    String s;
                    s << "KeyvaDB: Couldn't read a value from " << state->valFile.getFile ().getFileName ();
                    Throw (std::runtime_error (s.toStdString ()));
                }
            }
        }

        return found;
    }

    //--------------------------------------------------------------------------

    void put (void const* key, void const* value, int valueBytes)
    {
        bassert (valueBytes > 0);

        SharedState::WriteAccess state (m_state);

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
                // Do nothing
                /*
                String s;
                s << "KeyvaDB: Attempt to write a duplicate key!";
                Throw (std::runtime_error (s.toStdString ()));
                */
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
        SharedState::WriteAccess state (m_state);

        flushInternal (state);
    }

    void flushInternal (SharedState::WriteAccess& state)
    {
        state->keyFile.flush ();
        state->valFile.flush ();
    }

    //--------------------------------------------------------------------------

private:
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

private:
    int const m_keyBytes;
    int const m_keyRecordBytes;
    bool const m_filesAreTemporary;
    SharedState m_state;
    HeapBlock <char> m_keyStorage;
};

KeyvaDB* KeyvaDB::New (int keyBytes, File keyPath, File valPath, bool filesAreTemporary)
{
    return new KeyvaDBImp (keyBytes, keyPath, valPath, filesAreTemporary);
}

//------------------------------------------------------------------------------

class KeyvaDBTests : public UnitTest
{
public:
    enum
    {
        maxPayloadBytes = 8 * 1024
    };

    KeyvaDBTests () : UnitTest ("KeyvaDB")
    {
    }

    // Retrieval callback stores the value in a Payload object for comparison
    struct PayloadGetCallback : KeyvaDB::GetCallback
    {
        UnitTestUtilities::Payload payload;

        PayloadGetCallback () : payload (maxPayloadBytes)
        {
        }

        void* createStorageForValue (int valueBytes)
        {
            bassert (valueBytes <= maxPayloadBytes);

            payload.bytes = valueBytes;

            return payload.data.getData ();
        }
    };

    template <unsigned int KeyBytes>
    void testKeySize (unsigned int const maxItems)
    {
        using namespace UnitTestUtilities;

        typedef UnsignedInteger <KeyBytes> KeyType;

        int64 const seedValue = 50;
   
        String s;
        s << "keyBytes=" << String (KeyBytes) << ", maxItems=" << String (maxItems);
        beginTest (s);

        // Set up the key and value files and open the db.
        File const keyPath = File::createTempFile ("").withFileExtension (".key");
        File const valPath = File::createTempFile ("").withFileExtension (".val");
        ScopedPointer <KeyvaDB> db (KeyvaDB::New (KeyBytes, keyPath, valPath, true));

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

                payload.repeatableRandomFill (1, maxPayloadBytes, keyIndex + seedValue);

                expect (payload == cb.payload, "Should be equal");
            }
        }
    }

    void runTest ()
    {
        testKeySize <4> (512);
        testKeySize <32> (4096);
    }
};

static KeyvaDBTests keyvaDBTests;
