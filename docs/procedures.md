# Stored Procedures

The `procedure` class provides a convenient mechanism for calling stored procedures:

```cpp
sql << "create or replace procedure echo(output out varchar2,"
        "input in varchar2) as "
        "begin output := input; end;";

std::string in("my message");
std::string out;
procedure proc = (sql.prepare << "echo(:output, :input)", use(out, "output"), use(in, "input"));
proc.execute(true);
assert(out == "my message");
```

## Portability note

The above way of calling stored procedures is provided for portability of the code that might need it.
It is of course still possible to call procedures or functions using the syntax supported by the given database server.
