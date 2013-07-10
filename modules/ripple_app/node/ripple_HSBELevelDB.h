#ifndef HSBELEVELDB_H
#define HSBELEVELDB_H

class HSBELevelDB : public HashStoreBE
{
public:
    HSBELevelDB(std::string const& path);
    ~HSBELevelDB();

    std::string getBackEndName()
    {
        return "LevelDB";
    }

    std::string getDataBaseName();

    bool store(HashedObject::ref);
    bool bulkStore(const std::vector< HashedObject::pointer >&);

    HashedObject::pointer retrieve(uint256 const& hash);

    void visitAll(FUNCTION_TYPE<void (HashedObject::pointer)>);

private:
    std::string      mName;
    leveldb::DB*     mDB;

    Blob toBlob(HashedObject::ref);
    HashedObject::pointer fromBinary(uint256 const& hash, char const* data, int size);
};

#endif
