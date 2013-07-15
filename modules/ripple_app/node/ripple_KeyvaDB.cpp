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
        ScopedPointer <FileInputStream> keyIn;
        ScopedPointer <FileOutputStream> keyOut;
        KeyIndex newKeyIndex;

        ScopedPointer <FileInputStream> valIn;
        ScopedPointer <FileOutputStream> valOut;
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

        // Output must be opened first, in case it has
        // to created, or else opening for input will fail.
        state->keyOut = openForWrite (keyPath);
        state->keyIn = openForRead (keyPath);

        int64 const fileSize = state->keyIn->getFile ().getSize ();

        if (fileSize == 0)
        {
            // initialize the key file
            state->keyOut->setPosition (keyFileHeaderBytes - 1);
            state->keyOut->writeByte (0);
            state->keyOut->flush ();
        }

        state->newKeyIndex = 1 + (state->keyIn->getFile ().getSize () - keyFileHeaderBytes) / m_keyRecordBytes;

        state->valOut = openForWrite (valPath);
        state->valIn = openForRead (valPath);
        state->valFileSize = state->valIn->getFile ().getSize ();
    }

    ~KeyvaDBImp ()
    {
        SharedState::WriteAccess state (m_state);

        flushInternal (state);

        state->keyOut = nullptr;
        state->valOut = nullptr;

        // Delete the database files if requested.
        //
        if (m_filesAreTemporary)
        {
            {
                File const path = state->keyIn->getFile ();
                state->keyIn = nullptr;
                path.deleteFile ();
            }

            {
                File const path = state->valIn->getFile ();
                state->valIn = nullptr;
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
    void readKeyRecord (KeyRecord* const keyRecord,
                        KeyIndex const keyIndex,
                        SharedState::WriteAccess& state)
    {
        FileOffset const byteOffset = calcKeyRecordOffset (keyIndex);

        bool const success = state->keyIn->setPosition (byteOffset);

        if (success)
        {
            // This defines the file format!
            keyRecord->valFileOffset = state->keyIn->readInt64BigEndian ();
            keyRecord->valSize = state->keyIn->readIntBigEndian ();
            keyRecord->leftIndex = state->keyIn->readIntBigEndian ();
            keyRecord->rightIndex = state->keyIn->readIntBigEndian ();

            // Grab the key
            state->keyIn->read (keyRecord->key, m_keyBytes);
        }
        else
        {
            String s;
            s << "KeyvaDB: Seek failed in " << state->valOut->getFile ().getFileName ();
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

        bool const success = state->keyOut->setPosition (byteOffset);

        if (success)
        {
            // This defines the file format!
            // VFALCO TODO Make OutputStream return the bool errors here
            //
            state->keyOut->writeInt64BigEndian (keyRecord.valFileOffset);
            state->keyOut->writeIntBigEndian (keyRecord.valSize);
            state->keyOut->writeIntBigEndian (keyRecord.leftIndex);
            state->keyOut->writeIntBigEndian (keyRecord.rightIndex);

            // Write the key
            if (includingKey)
            {
                bool const success = state->keyOut->write (keyRecord.key, m_keyBytes);

                if (! success)
                {
                    String s;
                    s << "KeyvaDB: Write failed in " << state->valOut->getFile ().getFileName ();
                    Throw (std::runtime_error (s.toStdString ()));
                }
            }

            state->keyOut->flush ();
        }
        else
        {
            String s;
            s << "KeyvaDB: Seek failed in " << state->valOut->getFile ().getFileName ();
            Throw (std::runtime_error (s.toStdString ()));
        }
    }

    // Append a value to the value file.
    void writeValue (void const* const value, ByteSize valueBytes, SharedState::WriteAccess& state)
    {
        bool const success = state->valOut->setPosition (state->valFileSize);

        if (success)
        {
            bool const success = state->valOut->write (value, static_cast <size_t> (valueBytes));

            if (! success)
            {
                String s;
                s << "KeyvaDB: Write failed in " << state->valOut->getFile ().getFileName ();
                Throw (std::runtime_error (s.toStdString ()));
            }

            state->valFileSize += valueBytes;

            state->valOut->flush ();
        }
        else
        {
            String s;
            s << "KeyvaDB: Seek failed in " << state->valOut->getFile ().getFileName ();
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
        FindResult findResult (m_keyStorage.getData ());

        SharedState::WriteAccess state (m_state);

        bool found = false;

        if (state->hasKeys ())
        {
            found = find (&findResult, key, state);

            if (found)
            {
                void* const destStorage = callback->createStorageForValue (findResult.keyRecord.valSize);

                bool const success = state->valIn->setPosition (findResult.keyRecord.valFileOffset);

                if (! success)
                {
                    String s;
                    s << "KeyvaDB: Seek failed in " << state->valOut->getFile ().getFileName ();
                    Throw (std::runtime_error (s.toStdString ()));
                }

                int const bytesRead = state->valIn->read (destStorage, findResult.keyRecord.valSize);

                if (bytesRead != findResult.keyRecord.valSize)
                {
                    String s;
                    s << "KeyvaDB: Couldn't read a value from " << state->valIn->getFile ().getFileName ();
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
                    if (findResult.compare == -1)
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
                String s;
                s << "KeyvaDB: Attempt to write a duplicate key!";
                Throw (std::runtime_error (s.toStdString ()));
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
        state->keyOut->flush ();
        state->valOut->flush ();
    }

    //--------------------------------------------------------------------------

private:
    // Open a file for reading.
    static FileInputStream* openForRead (File path)
    {
        FileInputStream* stream = path.createInputStream ();

        if (stream == nullptr)
        {
            String s;
            s << "KeyvaDB: Couldn't open " << path.getFileName () << " for reading.";
            Throw (std::runtime_error (s.toStdString ()));
        }

        return stream;
    }

    // Open a file for writing.
    static FileOutputStream* openForWrite (File path)
    {
        FileOutputStream* stream = path.createOutputStream ();

        if (stream == nullptr)
        {
            String s;
            s << "KeyvaDB: Couldn't open " << path.getFileName () << " for writing.";
            Throw (std::runtime_error (s.toStdString ()));
        }

        return stream;
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
    KeyvaDBTests () : UnitTest ("KevyaDB")
    {
    }

    template <class T>
    void repeatableShuffle (int const numberOfItems, HeapBlock <T>& items)
    {
        Random r (69);

        for (int i = numberOfItems - 1; i > 0; --i)
        {
            int const choice = r.nextInt (i + 1);

            std::swap (items [i], items [choice]);
        }
    }

    template <unsigned int KeyBytes>
    void testSize (unsigned int const maxItems)
    {
        typedef UnsignedInteger <KeyBytes> KeyType;

        String s;
        s << "keyBytes=" << String (KeyBytes);
        beginTest (s);

        // Set up the key and value files and open the db.
        File const keyPath = File::createTempFile ("").withFileExtension (".key");
        File const valPath = File::createTempFile ("").withFileExtension (".val");
        ScopedPointer <KeyvaDB> db (KeyvaDB::New (KeyBytes, keyPath, valPath, true));

        {
            // Create an array of ascending integers.
            HeapBlock <unsigned int> items (maxItems);
            for (unsigned int i = 0; i < maxItems; ++i)
                items [i] = i;

            // Now shuffle it deterministically.
            repeatableShuffle (maxItems, items);

            // Write all the keys of integers.
            for (unsigned int i = 0; i < maxItems; ++i)
            {
                unsigned int const num = items [i];
                KeyType const v = KeyType::createFromInteger (num);

                // The value is the same as the key, for ease of comparison.
                db->put (v.cbegin (), v.cbegin (), KeyBytes);
            }
        }

        {
            // This callback creates storage for the value.
            struct MyGetCallback : KeyvaDB::GetCallback
            {
                KeyType v;

                void* createStorageForValue (int valueBytes)
                {
                    bassert (valueBytes == KeyBytes);

                    return v.begin ();
                }
            };

            // Go through all of our keys and try to retrieve them.
            // since this is done in ascending order, we should get
            // random seeks at this point.
            //
            for (unsigned int i = 0; i < maxItems; ++i)
            {
                KeyType const v = KeyType::createFromInteger (i);

                MyGetCallback cb;

                bool const found = db->get (v.cbegin (), &cb);

                expect (found, "Should be found");

                expect (v == cb.v, "Should be equal");
            }
        }
    }

    void runTest ()
    {
        testSize <4> (512);
        testSize <32> (4096);
    }
};

static KeyvaDBTests keyvaDBTests;
