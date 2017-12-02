# Transactions

The SOCI library provides the following members of the `session` class for transaction management:

* `void begin();`
* `void commit();`
* `void rollback();`

In addition to the above there is a RAII wrapper that allows to associate the transaction with the given scope of code:

```cpp
class transaction
{
public:
    explicit transaction(session & sql);

    ~transaction();

    void commit();
    void rollback();

private:
    // ...
};
```

The object of class `transaction` will roll back automatically when the object is destroyed
(usually as a result of leaving the scope) *and* when the transaction was not explicitly committed before that.

A typical usage pattern for this class might be:

```cpp
{
    transaction tr(sql);

    sql << "insert into ...";
    sql << "more sql queries ...";
    // ...

    tr.commit();
}
```

With the above pattern the transaction is committed only when the code successfully reaches the end of block.
If some exception is thrown before that, the scope will be left without reaching the final statement and the transaction object will automatically roll back in its destructor.
