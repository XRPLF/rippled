# Multithreading

The general rule for multithreading is that SOCI classes are *not* thread-safe, meaning that their instances should not be used concurrently by multiple threads.

The simplest solution for multithreaded code is to set up a separate `session` object for each thread that needs to inteact with the database.
Depending on the design of the client application this might be also the most straightforward approach.

For some applications, however, it might be preferable to decouple the set of threads from the set of sessions, so that they can be optimized separately with different resources in mind.
The `connection_pool` class is provided for this purpose:

```cpp
// phase 1: preparation

const size_t poolSize = 10;
connection_pool pool(poolSize);

for (size_t i = 0; i != poolSize; ++i)
{
    session &amp; sql = pool.at(i);

    sql.open("postgresql://dbname=mydb");
}

// phase 2: usage from working threads

{
    session sql(pool);

    sql << "select something from somewhere...";

} // session is returned to the pool automatically
```

The `connection_pool`'s constructor expects the size of the pool and internally creates an array of `session`s in the disconnected state.
Later, the `at` function provides *non-synchronized* access to each element of the array.
Note that this function is *not* thread-safe and exists only to make it easier to set up the pool in the initialization phase.

Note that it is not obligatory to use the same connection parameters for all sessions in the pool, although this will be most likely the usual case.

The working threads that need to *lease* a single session from the pool use the dedicated constructor of the `session` class - this constructor blocks until some session object becomes available in the pool and attaches to it, so that all further uses will be forwarded to the `session` object managed by the pool.
As long as the local `session` object exists, the associated session in the pool is *locked* and no other thread will gain access to it.
When the local `session` variable goes out of scope, the related entry in the pool's internal array is released, so that it can be used by other threads.
This way, the connection pool guarantees that its session objects are never used by more than one thread at a time.

Note that the above scheme is the simplest way to use the connection pool, but it is also constraining in the fact that the `session`'s constructor can *block* waiting for the availability of some entry in the pool.
For more demanding users there are also low-level functions that allow to lease sessions from the pool with timeout on wait.
Please consult the [reference](reference.html) for details.
