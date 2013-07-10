#ifndef RIPPLE_HASHSTOREBE_H
#define RIPPLE_HASHSTOREBE_H

/** Back end for storing objects indexed by 256-bit hash
*/
class HashStoreBE
{
public:
    typedef boost::shared_ptr<HashStoreBE> pointer;

    HashStoreBE()                { ; }
    virtual ~HashStoreBE()       { ; }

    virtual std::string getBackEndName() = 0;
    virtual std::string getDataBaseName() = 0;

    // Store/retrieve a single object
    // These functions must be thread safe
    virtual bool store(HashedObject::ref) = 0;
    virtual HashedObject::pointer retrieve(uint256 const &hash) = 0;

    // Store a group of objects
    // This function will only be called from a single thread
    virtual bool bulkStore(const std::vector< HashedObject::pointer >&) = 0;

    // Visit every object in the database
    // This function will only be called during an import operation
    virtual void visitAll(FUNCTION_TYPE<void (HashedObject::pointer)>) = 0;
};

#endif
