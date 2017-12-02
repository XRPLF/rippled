# FAQ

This part of the documentation is supposed to gather in a single place the usual questions (and answers) about SOCI with regard to the design decisions that have shaped it.

## Q: Why "SOCI"?

SOCI was initially developed in the environment where Oracle was the main database technology in use. As a wrapper for the native OCI API (Oracle Call Interface), the name "Simple Oracle Call Interface" was quite obvious - until the 2.0 release, when the internal architecture was largely redesigned to allow the use of *backends* that support other database servers. We have kept the same name to indicate that Oracle is the main supported technology in the sense that the library includes only those features that were naturally implemented in Oracle. With the 2.1 release of the library, two new backends were added (MySQL and SQLite3) and we decided to drop the original full name so that new users looking for a library supporting any of these simpler libraries are not discouraged by seeing "Oracle" somewhere in the name.

The other possible interpretation was "Syntax Oriented Call Interface", which stresses the fact that SOCI was built to support the most natural and easy interface for the user that is similar to the Embedded SQL concept (see below). But on the other hand, SOCI also provides other features (like object-relational mapping) and as a whole it is not just "emulator" of the Embedded SQL. With all these considerations in mind, SOCI is just "SOCI - The C++ Database Access Library".

Still, Oracle is considered to be the main driving server technology in terms of the set of features that are supported by the library. This also means that backends for other servers might need to work around some of the imposed idioms and protocols, but already available and well-working PostgreSQL, MySQL and SQLite3 backends show
that it's actually not that bad and the abstractions provided by the library are actually very universal. Of course, some of the features that were provided for Oracle might not be supported for all other servers, but we think that it's better to have one leading technology (where at least one group is fully happy) instead of some "common denominator" for all databases (where *nobody* is happy).

## Q: Where the basic SOCI syntax comes from?

The basic SOCI syntax was inspired by the Embedded SQL, which is part of the SQL standard, supported by the major DB technologies and even available as built-in part of the languages used in some DB-oriented integrated development environments. The term "Embedded SQL" is enough for Google to spit millions of references - one of the typical examples is:

```cpp
{
    int a;
    /* ... */
    EXEC SQL SELECT salary INTO :a
                FROM Employee
                WHERE SSN=876543210;
    /* ... */
    printf("The salary is %d\n", a);
    /* ... */
}
```

The above is not a regular C (nor C++) code, of course. It's the mix of C and SQL and there is a separate, pecialized preprocessor needed to convert it to something that the actual C (or C++) compiler will be able to understand. This means that the compilation of the program using embedded SQL is two-phase: preprocess the embedded SQL part and compile the result. This two-phase development is quite troublesome, especially when it comes to debugging. Yet, the advantage of it is that the code expresses the programmer's intents in a very straightforward
way: read something from the database and put it into the local variable. Just like that.

The SOCI library was born as an anwer to the following question: is it possible to have the same expressive power without the disadvantages of two-phase builds?

The following was chosen to be the basic SOCI syntax that can mirror the above Embedded SQL example:

```cpp
int a;
sql << "SELECT salary FROM Employee WHERE SSN=876543210", into(a);
```

(as you see, SOCI changes the order of elements a little bit, so that the SQL query is separate and not mixed with other elements)

Apart from mimicking the Embedded SQL techniques in the regular, fully standard C++ code, the above syntax has the following benefit: it is *minimal* with respect to what has to be said. Every single piece above is needed and expresses something important, like:

* which session should be used (the client can be connected to many databases at the same time) - here, the `sql` object encapsulates the session,
* what SQL query should be executed - here, it's the string literal, but it could be also a `std::string` variable,
* where to put the result - here, the local variable `a` will receive the result.

Everything else is just a couple of operators that allow to treat the whole as a single expression. It's rather difficult to remove anything from this example.

The fact that the basic SOCI syntax is minimal (but without being obscure at the same time, see below) means that the programmer does not need to bother with unnecessary noise that some other database libraries impose. We hope that after having written one line of code like above by themselves, most programmers will react with something
like "how obvious!" instead of "how advanced!".

## Q: Why should I use SQL queries as strings in my program? I prefer the query to be generated or composed piece-by-piece by separate functions.

First, you don't need to use SQL queries as string literals. In bigger projects it is a common practice to store SQL queries externally (in a file, or in a... database) and load them before use. This means that they are not necessarily expected to appear in the program code, as they do in our simple code examples and the advantage of separating them from the source code of the main program is, among others, the possibility to optimize and tune the SQL queries without recompilation and relinking of the whole program.

What is the most important, though, is that SOCI does not try to mess with the text of the query (apart from very few cases), which means that the database server will get exactly the same text of the query as is used in the program. The advantage of this is that there is no new SQL-like (or even SQL-*un*like) syntax that you would need to learn, and also that it's much easier to convince a typical DBA to help with SQL tuning or other specialized activities, if he is given the material in the form that is not polluted with any foreign abstractions.

## Q: Why not some stream-like interface, which is well-known to all C++ programmers?

An example of the stream-like interface might be something like this (this is imaginary syntax, not supported by SOCI):

```cpp
sql.exec("select a, b, c from some_table");

while (!sql.eof())
{
    int a, b, c;
    sql >> a >> b >> c;
    // ...
}
```

We think that the data stored in the relational database should be treated as a set of relations - which is exactly what it is. This means that what is read from the database as a result of some SQL query is a *set of rows*. This set might be ordered, but it is still a set of rows, not a uniformly flat list of values. This distinction might seem to be unnecessarily low-level and that the uniform stream-like presentation of data is more preferable, but it's actually the other way round - the set of rows is something more structured - and that structure was *designed* into the database - than the flat stream and is therefore less prone to programming errors like miscounting the number of values that is expected in each row.

Consider the following programming error:

```cpp
sql.exec("select a, b from some_table"); // only TWO columns

while (!sql.eof())
{
    int a, b, c;
    sql >> a >> b >> c; // this causes "row-tearing"
    // ...
}
```

*"How to detect the end of each line in a file"* is a common beginner's question that relates to the use of IOStreams - and this common question clearly shows that for the record-oriented data the stream is not an optimal abstraction. Of course, we don't claim that IOStreams is bad - but we do insist that the record-oriented data is
better manipulated in a way that is also record-aware.

Having said that, we *have* provided some form of the stream-like interface, but with the important limitation that the stream is always bound to the single row, so that the row-tearing effect is not possible. In other words,
data returned from the database is still structured into rows, but each row can be separately traversed like a stream. We hope that it provides a good balance between convenience and code safety.

## Q: Why use indicators instead of some special value to discover that something is null?

Some programmers are used to indicating the null value by using some special (which means: "unlikely" to be ever used) value - for example, to use the smallest integer value to indicate null integer. Or to use empty string to indicate null string. And so on.

We think that it's *completely wrong*. Null (in the database sense) is an information *about* the data. It describes the *state* of the data and if it's null, then there's *no data at all*. Nothing. Null. It does not make any sense to talk about some special value if in fact there is *no* value at all - especially if we take into account that, for example, the smallest integer value (or whatever else you choose as the "special" value) might not be *that* special in the given application or domain.

Thus, SOCI uses a separate indicators to describe the state of exchanged data. It also has an additional benefit of allowing the library to convey more than two states (null and not null). Indeed, the SOCI library uses indicators also to report that the data was read, but truncated (this applies to strings when reading to fixed-length character arrays). Truncation is also an information about the data and as such it's better to have it in addition to the data, not as part of it.

Having said that, it is important to point at the [Integration with Boost](boost.html) that allows to use `boost::optional<T>` to conveniently pack together the data and the information about its state.

## Q: Overloaded comma operator is just obfuscation, I don't like it.

Well, consider the following:

"Send the query X to the server Y *and* put result into variable Z."

Above, the "and" plays a role of the comma. Even if overloading the comma operator is not a very popular practice in C++, some libraries do this, achieving terse and easy to learn syntax. We are pretty sure that in SOCI the comma operator was overloaded with a good effect.

## Q: The `operator<<` provides a bad abstraction for the "input" statements.

Indeed, the `operator<<` in the basic SOCI syntax shows that something (the query) is *sent* somewhere (to the server). Some people don't like this, especially when the "select" statements are involved. If the high-level idea is to *read*  data from somewhere, then `operator<<` seems unintuitive to the die-hard IOStreams users. The fact is, however, that the code containing SQL statement already indicates that there is a client-server relationship with some other software component (very likely remote). In such code it does not make any sense to pretend that the communication is one-way only, because it's clear that even the "select" statements need to be *sent* somewhere. This approach is also more uniform and allows to cover other statements like "drop table" or alike, where no data is expected to be exchanged at all (and therefore the IOStreams analogies for data exchange have no sense at all). No matter what is the kind of the SQL statement, it is *sent*  to the server and this "sending" justifies the choice of `operator<<`.

Using different operators (`operator>>` and `operator<<`) as a way of distinguishing between different high-level ideas (*reading* and *writing* from the data store, respectively) does make sense on much higher level of abstraction, where the SQL statement itself is already hidden - and we do encourage programmers to use SOCI for implementing such high-level abstractions. For this, the object-relational mapping facilities available in SOCI might prove to be a valuable tool as well, as an effective bridge
between the low-level world of SQL statements and the high-level world of user-defined abstract data types.

## Q: Why the Boost license?

We decided to use the [Boost license](http://www.boost.org/LICENSE_1_0.txt), because
it's well recognized in the C++ community, allows us to keep our minimum copyrights, and at the same time allows SOCI to be safely used in commercial projects, without imposing concerns (or just plainuncertainty) typical to other open source licenses, like GPL. We also hope that by choosing the Boost license we have made the life easier
for both us and our users. It saves us from answering law-related questions that were already answered on the [Boost license info page](http://www.boost.org/more/license_info.html) and it should also give more confidence to our users - especially to those of them, who already accepted the conditions of the Boost license - the just have one license less to analyze.

Still, if for any reason the conditions of this license are not acceptable, we encourage the users to contact us directly (see [links](http://soci.sourceforge.net/people.html) on the relevant SOCI page) to discuss any remaining concerns.
