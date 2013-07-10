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

    bool store(NodeObject::ref);
    bool bulkStore(const std::vector< NodeObject::pointer >&);

    NodeObject::pointer retrieve(uint256 const& hash);

    void visitAll(FUNCTION_TYPE<void (NodeObject::pointer)>);

private:
    std::string      mName;
    leveldb::DB*     mDB;

    Blob toBlob(NodeObject::ref);
    NodeObject::pointer fromBinary(uint256 const& hash, char const* data, int size);
};

#endif
