#ifndef HSBESQLITE_H
#define HSBESQLITE_H

class HSBESQLite : public HashStoreBE
{
public:
    HSBESQLite(std::string const& path);
    ~HSBESQLite();

    std::string getBackEndName()
    {
        return "SQLite";
    }

    std::string getDataBaseName();

    bool store(HashedObject::ref);
    bool bulkStore(const std::vector< HashedObject::pointer >&);

    HashedObject::pointer retrieve(uint256 const& hash);

    void visitAll(FUNCTION_TYPE<void (HashedObject::pointer)>);

private:
    std::string      mName;
    DatabaseCon*     mDb;

    void bind(SqliteStatement& statement, HashedObject::ref object);
    HashedObjectType getType(std::string const&);
};

#endif
