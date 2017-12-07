# Boost Integration

The SOCI user code can be easily integrated with the [Boost library](http://www.boost.org/) thanks to the very flexible type conversion facility.

The integration with Boost types is optional and is *not* enabled by default, which means that SOCI can also be compiled and used without any dependency on Boost.

In order to enable the support for any of the above types, the user needs to either include one of these headers:

```cpp
#include <boost-optional.h>
#include <boost-tuple.h>
#include <boost-fusion.h>
#include <boost-gregorian-date.h>
```

or to define the `SOCI_USE_BOOST` macro before including the `soci.h` main header file.

## Boost.Optional

`boost::optional<T>` provides an alternative way to support the null data condition and as such relieves the user from necessity to handle separate indicator values.

The `boost::optional<T>` objects can be used everywhere where the regular user provided values are expected.

Example:

```cpp
boost::optional<string> name;
sql << "select name from person where id = 7", into(name);

if (name.is_initialized())
{
    // OK, the name was retrieved and is not-null
    cout << "The name is " << name.get();
}
else
{
    // the name is null
}
```

The `boost::optional<T>` objects are fully supported for both `into` and `use` elements, in both single and vector forms. They can be also used for user-defined data types.

## Boost.Tuple

`boost::tuple<T1, ...>` allows to work with whole rows of information and in some cases can be more convenient to use than the more dynamically-oriented `row` type.

```cpp
boost::tuple<string, string, int> person;

sql << "select name, phone, salary from persons where ...",
        into(person);
```

Tuples are supported for both `into` and `use` elements.
They can be used with `rowset` as well.

Tuples can be also composed with `boost::optional<T>`

```cpp
boost::tuple<string, boost::optional<string>, int> person;

sql << "select name, phone, salary from persons where ...",
        into(person);

if (person.get<1>().is_initialized())
{
    // the given person has a phone number
}
else
{
    // this person does not have a phone number
}
```

## Boost.Fusion

The `boost::fusion::vector` types are supported in the same way as tuples.

**Note:** Support for `boost::fusion::vector` is enabled only if the detected Boost version is at least 1.35.

## Boost.DateTime

The `boost::gregorian::date` is provided as a conversion for base type `std::tm` and can be used as a replacement for it.
