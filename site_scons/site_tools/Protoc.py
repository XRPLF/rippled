#
# Copyright (c) 2009  Scott Stafford
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
# KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
# WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
# LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
# OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
# WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#

#  Author : Scott Stafford
#  Date : 2009-12-09 20:36:14
#
#  Changes : Vinnie Falco <vinnie.falco@gmail.com>
#  Date : 2014--4-25

""" 
Protoc.py: Protoc Builder for SCons

This Builder invokes protoc to generate C++ and Python  from a .proto file.  

NOTE: Java is not currently supported.
"""

__author__ = "Scott Stafford"

import SCons.Action
import SCons.Builder
import SCons.Defaults
import SCons.Node.FS
import SCons.Util

from SCons.Script import File, Dir

import os.path

protocs = 'protoc'

ProtocAction = SCons.Action.Action('$PROTOCCOM', '$PROTOCCOMSTR')

def ProtocEmitter(target, source, env):
    PROTOCOUTDIR = env['PROTOCOUTDIR']
    PROTOCPYTHONOUTDIR = env['PROTOCPYTHONOUTDIR']
    for source_path in [str(x) for x in source]:
        base = os.path.splitext(os.path.basename(source_path))[0]
        if PROTOCOUTDIR:
            target.extend([os.path.join(PROTOCOUTDIR, base + '.pb.cc'),
                           os.path.join(PROTOCOUTDIR, base + '.pb.h')])
        if PROTOCPYTHONOUTDIR:
            target.append(os.path.join(PROTOCPYTHONOUTDIR, base + '_pb2.py'))

    try:
        target.append(env['PROTOCFDSOUT'])
    except KeyError:
        pass

    #print "PROTOC SOURCE:", [str(s) for s in source]
    #print "PROTOC TARGET:", [str(s) for s in target]

    return target, source

ProtocBuilder = SCons.Builder.Builder(
    action = ProtocAction,
    emitter = ProtocEmitter,
    srcsuffix = '$PROTOCSRCSUFFIX')

def generate(env):
    """Add Builders and construction variables for protoc to an Environment."""
    try:
        bld = env['BUILDERS']['Protoc']
    except KeyError:
        bld = ProtocBuilder
        env['BUILDERS']['Protoc'] = bld

    env['PROTOC']        = env.Detect(protocs) or 'protoc'
    env['PROTOCFLAGS']   = SCons.Util.CLVar('')
    env['PROTOCPROTOPATH'] = SCons.Util.CLVar('')
    env['PROTOCCOM']     = '$PROTOC ${["-I%s"%x for x in PROTOCPROTOPATH]} $PROTOCFLAGS --cpp_out=$PROTOCCPPOUTFLAGS$PROTOCOUTDIR ${PROTOCPYTHONOUTDIR and ("--python_out="+PROTOCPYTHONOUTDIR) or ""} ${PROTOCFDSOUT and ("-o"+PROTOCFDSOUT) or ""} ${SOURCES}'
    env['PROTOCOUTDIR'] = '${SOURCE.dir}'
    env['PROTOCPYTHONOUTDIR'] = "python"
    env['PROTOCSRCSUFFIX']  = '.proto'

def exists(env):
    return env.Detect(protocs)