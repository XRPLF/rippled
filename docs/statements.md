## Statements, procedures and transactions

* [Statement preparation and repeated execution](#preparation)
* [Rowset and iterator-based access](#rowset)
* [Bulk operations](#bulk)
* [Stored procedures](#procedures)
* [Transactions](#transactions)
* [Basic logging support](#logging)

### <a name="preparation"></a> Statement preparation and repeated execution

Consider the following examples:

    // Example 1.
    for (int i = 0; i != 100; ++i)
    {
        sql << "insert into numbers(value) values(" << i << ")";
    }

    // Example 2.
    for (int i = 0; i != 100; ++i)
    {
        sql << "insert into numbers(value) values(:val)", use(i);
    }


Both examples will populate the table `numbers` with the values from `0` to `99`.

The problem is that in both examples, not only the statement execution is repeated 100 times, but also the statement parsing and preparation. This means unnecessary overhead, even if some of the database servers are likely to optimize the second case. In fact, more complicated queries are likely to suffer in terms of lower performance, because finding the optimal execution plan is quite expensive and here it would be needlessly repeated.

The following example uses the class `statement` explicitly, by preparing the statement only once and repeating its execution with changing data (note the use of `prepare` member of `session` class):

    int i;
    statement st = (sql.prepare <<
                    "insert into numbers(value) values(:val)",
                    use(i));
    for (i = 0; i != 100; ++i)
    {
        st.execute(true);
    }

The `true` parameter given to the `execute` method indicates that the actual data exchange is wanted, so that the meaning of the whole example is "prepare the statement and exchange the data for each value of variable `i`".

#####Portability note:

The above syntax is supported for all backends, even if some database server does not actually provide this functionality - in which case the library will internally execute the query in a single phase, without really separating the statement preparation from execution.

For PostgreSQL servers older than 8.0 it is necessary to define the `SOCI_POSTGRESQL_NOPREPARE` macro while compiling the library to fall back to this one-phase behaviour. Simply, pass `-DSOCI_POSTGRESQL_NOPREPARE=ON` variable to CMake.


### <a name="rowset"></a> Rowset and iterator-based access

The `rowset` class provides an alternative means of executing queries and accessing results using STL-like iterator interface.

The `rowset_iterator` type is compatible with requirements defined for input iterator category and is available via `iterator` and `const_iterator` definitions in the `rowset` class.

The `rowset` itself can be used only with select queries.

The following example creates an instance of the `rowset` class and binds query results into elements of `int` type - in this query only one result column is expected. After executing the query the code iterates through the query result using `rowset_iterator`:


    rowset<int> rs = (sql.prepare << "select values from numbers");

    for (rowset<int>::const_iterator it = rs.begin(); it != rs.end(); ++it)
    {
         cout << *it << '\n';
    }

Another example shows how to retrieve more complex results, where `rowset` elements are of type `row` and therefore use [dynamic bindings](exchange.html#dynamic):


    // person table has 4 columns

    rowset<row> rs = (sql.prepare << "select id, firstname, lastname, gender from person");

    // iteration through the resultset:
    for (rowset<row>::const_iterator it = rs.begin(); it != rs.end(); ++it)
    {
        row const& row = *it;

        // dynamic data extraction from each row:
        cout << "Id: " << row.get<int>(0) << '\n'
             << "Name: " << row.get<string>(1) << " " << row.get<string>(2) << '\n'
             << "Gender: " << row.get<string>(3) << endl;
    }

`rowset_iterator` can be used with standard algorithms as well:


    rowset<string> rs = (sql.prepare << "select firstname from person");

    std::copy(rs.begin(), rs.end(), std::ostream_iterator<std::string>(std::cout, "\n"));

Above, the query result contains a single column which is bound to `rowset` element of type of `std::string`. All records are sent to standard output using the `std::copy` algorithm.

### Bulk operations

When using some databases, further performance improvements may be possible by having the underlying database API group operations together to reduce network roundtrips. SOCI makes such bulk operations possible by supporting `std::vector` based types.

The following example presents how to insert 100 records in 4 batches. It is also important to note, that size of vector remains equal in every batch interaction. This ensures vector is not reallocated and, what's crucial for the bulk trick, new data should be pushed to the vector before every call to `statement::execute`:


    // Example 3.
    void fill_ids(std::vector<int>& ids)
    {
       for (std::size_t i = 0; i < ids.size(); ++i)
          ids[i] = i; // mimics source of a new ID
    }

    const int BATCH_SIZE = 25;
    std::vector<int> ids(BATCH_SIZE);

    statement st = (sql.prepare <<
                    "insert into numbers(value) values(:val)",
                    use(ids));
    for (int i = 0; i != 4; ++i)
    {
        fill_ids(ids);
        st.execute(true);
    }

Given batch size is 25, this example should insert 4 x 25 = 100 records.

(Of course, the size of the vector that will achieve optimum performance will vary, depending on many environmental factors, such as network speed.)

It is also possible to read all the numbers written in the above examples:

    int i;
    statement st = (sql.prepare <<
                    "select value from numbers order by value",
                    into(i));
    st.execute();
    while (st.fetch())
    {
        cout << i << '\n';
    }

In the above example, the `execute` method is called with the default parameter `false`. This means that the statement should be executed, but the actual data exchange will be performed later.

Further `fetch` calls perform the actual data retrieval and cursor traversal. The *end-of-cursor* condition is indicated by the `fetch` function returning `false`.

The above code example should be treated as an idiomatic way of reading many rows of data, *one at a time*.

It is further possible to select records in batches into `std::vector` based types, with the size of the vector specifying the number of records to retrieve in each round trip:

    std::vector<int> valsOut(100);
    sql << "select val from numbers", into(valsOut);

Above, the value `100` indicates that no more values should be retrieved, even if it would be otherwise possible. If there are less rows than asked for, the vector will be appropriately down-sized.

The `statement::execute()` and `statement::fetch()` functions can also be used to repeatedly select all rows returned by a query into a vector based type:


    const int BATCH_SIZE = 30;
    std::vector<int> valsOut(BATCH_SIZE);
    statement st = (sql.prepare <<
                    "select value from numbers",
                    into(valsOut));
    st.execute();
    while (st.fetch())
    {
        std::vector<int>::iterator pos;
        for(pos = valsOut.begin(); pos != valsOut.end(); ++pos)
        {
            cout << *pos << '\n';
        }

        valsOut.resize(BATCH_SIZE);
    }

Assuming there are 100 rows returned by the query, the above code will retrieve and print all of them. Since the output vector was created with size 30, it will take (at least) 4 calls to `fetch()` to retrieve all 100 values. Each call to `fetch()` can potentially resize the vector to a size less than its initial size
- how often this happens depends on the underlying database implementation. This explains why the `resize(BATCH_SIZE)` operation is needed - it is there to ensure that each time the `fetch()` is called, the vector is ready to accept the next bunch of values. Without this operation, the vector *might* be getting smaller with subsequent iterations of the loop, forcing more iterations to be performed (because *all* rows will be read anyway), than really needed.


Note the following details about the above examples:

* After performing `fetch()`, the vector's size might be *less* than requested, but `fetch()` returning true means that there was *at least one* row retrieved.
* It is forbidden to manually resize the vector to the size *higher* than it was initially (this can cause the vector to reallocate its internal buffer and the library can lose track of it).

Taking these points under consideration, the above code example should be treated as an idiomatic way of reading many rows by bunches of requested size.

### Statement caching

Some backends have some facilities to improve statement parsing and compilation to limit overhead when creating commonly used query. But for backends that does not support this kind optimization you can keep prepared statement and use it later with new references. To do such, prepare a statement as usual, you have to use `exchange` to bind new variables to statement object, then `execute` statement and finish by cleaning bound references with `bind_clean_up`.

    sql << "CREATE TABLE test(a INTEGER)";

    {
        // prepare statement
        soci::statement stmt = (db.prepare << "INSERT INTO numbers(value) VALUES(:val)");

        {
            // first insert
            int a0 = 0;

            // update reference
            stmt.exchange(soci::use(a0));

            stmt.define_and_bind();
            stmt.execute(true);
            stmt.bind_clean_up();
        }

        {
            // come later, second insert
            int a1 = 1;

            // update reference
            stmt.exchange(soci::use(a1));

            stmt.define_and_bind();
            stmt.execute(true);
            stmt.bind_clean_up();
        }
    }

    {
        std::vector<int> v(10);
        db << "SELECT value FROM numbers", soci::into(v);
        for (int i = 0; i < v.size(); ++i)
            std::cout << "value " << i << ": " << v[i] << std::endl;
    }


#####Portability note:
Actually, all supported backends guarantee that the requested number of rows will be read with each fetch and that the vector will never be down-sized, unless for the last fetch, when the end of rowset condition is met. This means that the manual vector resizing is in practice not needed - the vector will keep its size until the end of rowset. The above idiom, however, is provided with future backends in mind, where the constant size of the vector might be too expensive to guarantee and where allowing `fetch` to down-size the vector even before reaching the end of rowset might buy some performance gains.


### <a name="procedures"></a> Stored procedures

The `procedure` class provides a convenient mechanism for calling stored procedures:

    sql << "create or replace procedure echo(output out varchar2,"
           "input in varchar2) as "
           "begin output := input; end;";

    std::string in("my message");
    std::string out;
    procedure proc = (sql.prepare << "echo(:output, :input)",
                                     use(out, "output"),
                                     use(in, "input"));
    proc.execute(true);
    assert(out == "my message");


#####Portability note:

The above way of calling stored procedures is provided for portability of the code that might need it. It is of course still possible to call procedures or functions using the syntax supported by the given database server.


### <a name="transactions"></a> Transactions

The SOCI library provides the following members of the `session` class for transaction management:

* `void begin();`
* `void commit();`
* `void rollback();`

In addition to the above there is a RAII wrapper that allows to associate the transaction with the given scope of code:

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

The object of class `transaction` will roll back automatically when the object is destroyed
(usually as a result of leaving the scope) *and* when the transaction was not explicitly committed before that.

A typical usage pattern for this class might be:

    {
        transaction tr(sql);

        sql << "insert into ...";
        sql << "more sql queries ...";
        // ...

        tr.commit();
    }

With the above pattern the transaction is committed only when the code successfully reaches the end of block. If some exception is thrown before that, the scope will be left without reaching the final statement and the transaction object will automatically roll back in its destructor.

### <a name="logging"></a> Basic logging support

The following members of the `session` class support the basic logging functionality:

* `void set_log_stream(std::ostream * s);`
* `std::ostream * get_log_stream() const;`
* `std::string get_last_query() const;`

The first two functions allow to set the user-provided output stream object for logging. The `NULL` value, which is the default, means that there is no logging. An example use might be:

    session sql(oracle, "...");

    ofstream file("my_log.txt");
    sql.set_log_stream(&file);

    // ...

Each statement logs its query string before the preparation step (whether explicit or implicit) and therefore logging is effective whether the query succeeds or not. Note that each prepared query is logged only once, independent on how many times it is executed.

The `get_last_query` function allows to retrieve the last used query.