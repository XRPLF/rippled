#!/bin/sh

# Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

mkdir -p temp
doxygen source.dox
cd temp
xsltproc combine.xslt index.xml > all.xml
cp ../docca/include/docca/doxygen.xsl doxygen.xsl
sed -i -e '/<!-- CLASS_DETAIL_TEMPLATE -->/{r ../xsl/class_detail.xsl' -e 'd}' doxygen.xsl
sed -i -e '/<!-- INCLUDES_TEMPLATE -->/{r ../xsl/includes.xsl' -e 'd}' doxygen.xsl
sed -i -e '/<!-- INCLUDES_FOOT_TEMPLATE -->/{r ../xsl/includes_foot.xsl' -e 'd}' doxygen.xsl
xsltproc ../xsl/reference.xsl all.xml > ../reference.qbk
