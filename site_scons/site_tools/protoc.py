#!python
""" 
protoc.py: Protoc Builder for SCons

This Builder invokes protoc to generate C++ and Python 
from a .proto file.  

NOTE: Java is not currently supported.

From: http://www.scons.org/wiki/ProtocBuilder
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
    dirOfCallingSConscript = Dir('.').srcnode()
    env.Prepend(PROTOCPROTOPATH = dirOfCallingSConscript.path)
    
    source_with_corrected_path = []
    for src in source:
        commonprefix = os.path.commonprefix([dirOfCallingSConscript.path, src.srcnode().path])
        if len(commonprefix)>0:
            source_with_corrected_path.append( src.srcnode().path[len(commonprefix + os.sep):] )
        else:
            source_with_corrected_path.append( src.srcnode().path )
        
    source = source_with_corrected_path
    
    for src in source:
        modulename = os.path.splitext(os.path.basename(src))[0]

        if env['PROTOCOUTDIR']:            
            base = os.path.join(env['PROTOCOUTDIR'] , modulename)
            target.extend( [ base + '.pb.cc', base + '.pb.h' ] )
        
        if env['PROTOCPYTHONOUTDIR']:
            base = os.path.join(env['PROTOCPYTHONOUTDIR'] , modulename)
            target.append( base + '_pb2.py' )

    try:
        target.append(env['PROTOCFDSOUT'])
    except KeyError:
        pass

    # XXX KLUDGE: Force things to be right.
    env['PROTOCOUTDIR']	    = 'build/proto'
    env['PROTOCPROTOPATH']  = ['src/ripple_data/protocol']

    #~ print "PROTOC SOURCE:", [str(s) for s in source]
    #~ print "PROTOC TARGET:", [str(s) for s in target]

    return target, source

ProtocBuilder = SCons.Builder.Builder(action = ProtocAction,
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
