# Ada Bindings

* [Concepts](concepts.md)
* [Idioms](idioms.md)
* [API Reference](reference.md)

## Introduction

SOCI-Ada is a database access library for Ada.

The library itself is a wrapper for the selected functionality of the SOCI library, which is a C++ database access library recognized for its high quality and innovative interface.

The SOCI-Ada library offers the following features to the Ada community:

* Modular design based on dynamic backend loading. Thanks to this feature, new backends implemented within the context of the main SOCI project are immediately available for Ada programmers without any additional work. A large community of C++ users can help ensure that the new backends are well tested in a variety of environments and usage scenarios.
* Native backends for major database servers ensure optimal performance and minimize configuration overhead and complexity that is usually associated with other database access methods.
* Direct support for bulk operations allow to achieve high performance with queries that operate on large data sets.
* Very liberal open-source license ([Boost, accepted by Open Source Initiative](http://www.opensource.org/licenses/bsl1.0.html)) that encourages both commercial and non-commercial use.
* Easy to use and compact interface.

Currently the following database servers are directly supported via their native interfaces:

* Oracle
* PostgreSQL
* MySQL

Other backends exist in the SOCI Git repository and can be provided with future version of the library.

## Compilation

In order to use SOCI-Ada, compile the C++ parts first (core and required backends).

*Note:* SOCI header files are not needed to use SOCI-Ada, only compiled SOCI libraries (core and relevant backend) need to exist to build and use SOCI-Ada programs.

The SOCI-Ada library itself is a single package named `SOCI`. This package can be just imported in the target project as is or pre-built to the binary form if required.

In order to link the user programs the `-lsoci_core -lstdc++` linker options need to be provided on the Unix/Linux platforms.
