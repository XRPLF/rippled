# Quickstart

The following (complete!) example is purposedly provided without any explanation.

```cpp
#include "soci.h"
#include "soci-oracle.h"
#include <iostream>
#include <istream>
#include <ostream>
#include <string>
#include <exception>

using namespace soci;
using namespace std;

bool get_name(string &name) {
    cout << "Enter name: ";
    return cin >> name;
}

int main()
{
    try
    {
        session sql(oracle, "service=mydb user=john password=secret");

        int count;
        sql << "select count(*) from phonebook", into(count);

        cout << "We have " << count << " entries in the phonebook.\n";

        string name;
        while (get_name(name))
        {
            string phone;
            indicator ind;
            sql << "select phone from phonebook where name = :name",
                into(phone, ind), use(name);

            if (ind == i_ok)
            {
                cout << "The phone number is " << phone << '\n';
            }
            else
            {
                cout << "There is no phone for " << name << '\n';
            }
        }
    }
    catch (exception const &e)
    {
        cerr << "Error: " << e.what() << '\n';
    }
}
```
