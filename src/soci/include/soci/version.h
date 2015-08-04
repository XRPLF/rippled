//  SOCI version.hpp configuration header file

//
// Copyright (C) 2011 Mateusz Loskot <mateusz@loskot.net>
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_VERSION_HPP
#define SOCI_VERSION_HPP

// When updating the version here, don't forget to update it in CMakeLists.txt!

//
//  Caution, this is the only SOCI header that is guarenteed
//  to change with every SOCI release, including this header
//  will cause a recompile every time a new SOCI version is
//  released.
//
//  SOCI_VERSION % 100 is the patch level
//  SOCI_VERSION / 100 % 1000 is the minor version
//  SOCI_VERSION / 100000 is the major version

#define SOCI_VERSION 400000

//
//  SOCI_LIB_VERSION must be defined to be the same as SOCI_VERSION
//  but as a *string* in the form "x_y[_z]" where x is the major version
//  number, y is the minor version number, and z is the patch level if not 0.

#define SOCI_LIB_VERSION "4_0_0"

#endif // SOCI_VERSION_HPP
