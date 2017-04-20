#!/bin/sh

# Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

mkdir -p ../bin/doc/xml
doxygen source.dox
cd ../bin/doc/xml
xsltproc combine.xslt index.xml > all.xml
cd ../../../doc
xsltproc reference.xsl ../bin/doc/xml/all.xml > reference.qbk
