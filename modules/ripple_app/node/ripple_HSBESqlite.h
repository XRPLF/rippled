#ifndef HSBESQLITE_H
#define HSBESQLITE_H

class HSBESQLite : public NodeStore::Backend
{
public:
    HSBESQLite(std::string const& path);
    ~HSBESQLite();

    std::string getBackEndName()
    {
        return "SQLite";
    }

    std::string getDataBaseName();

    bool store(NodeObject::ref);
    bool bulkStore(const std::vector< NodeObject::pointer >&);

    NodeObject::pointer retrieve(uint256 const& hash);

    void visitAll(FUNCTION_TYPE<void (NodeObject::pointer)>);

private:
    std::string      mName;
    DatabaseCon*     mDb;

    void bind(SqliteStatement& statement, NodeObject::ref object);
    NodeObjectType getType(std::string const&);
};

#endif
