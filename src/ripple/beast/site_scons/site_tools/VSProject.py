# Copyright 2014 Vinnie Falco (vinnie.falco@gmail.com)
# Portions Copyright The SCons Foundation
# Portions Copyright Google, Inc.
# This file is part of beast

"""
A SCons tool to provide a family of scons builders that
generate Visual Studio project files
"""

import collections
import hashlib
import io
import itertools
import ntpath
import os
import pprint
import random
import re

import SCons.Builder
import SCons.Node.FS
import SCons.Node
import SCons.Script.Main
import SCons.Util

#-------------------------------------------------------------------------------

# Adapted from msvs.py

UnicodeByteMarker = '\xEF\xBB\xBF'

V14DSPHeader = """\
<?xml version="1.0" encoding="%(encoding)s"?>\r
<Project DefaultTargets="Build" ToolsVersion="14.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\r
"""

V14DSPProjectConfiguration = """\
    <ProjectConfiguration Include="%(variant)s|%(platform)s">\r
      <Configuration>%(variant)s</Configuration>\r
      <Platform>%(platform)s</Platform>\r
    </ProjectConfiguration>\r
"""

V14DSPGlobals = """\
  <PropertyGroup Label="Globals">\r
    <ProjectGuid>%(project_guid)s</ProjectGuid>\r
    <Keyword>Win32Proj</Keyword>\r
    <RootNamespace>%(name)s</RootNamespace>\r
    <IgnoreWarnCompileDuplicatedFilename>true</IgnoreWarnCompileDuplicatedFilename>\r
  </PropertyGroup>\r
"""

V14DSPPropertyGroup = """\
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='%(variant)s|%(platform)s'" Label="Configuration">\r
    <CharacterSet>MultiByte</CharacterSet>\r
    <ConfigurationType>Application</ConfigurationType>\r
    <PlatformToolset>v140</PlatformToolset>\r
    <LinkIncremental>False</LinkIncremental>\r
    <UseDebugLibraries>%(use_debug_libs)s</UseDebugLibraries>\r
    <UseOfMfc>False</UseOfMfc>\r
    <WholeProgramOptimization>false</WholeProgramOptimization>\r
    <IntDir>%(int_dir)s</IntDir>\r
    <OutDir>%(out_dir)s</OutDir>\r
  </PropertyGroup>\r
"""

V14DSPImportGroup= """\
  <ImportGroup Condition="'$(Configuration)|$(Platform)'=='%(variant)s|%(platform)s'" Label="PropertySheets">\r
    <Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />\r
  </ImportGroup>\r
"""

V14DSPItemDefinitionGroup= """\
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='%(variant)s|%(platform)s'">\r
"""

V14CustomBuildProtoc= """\
      <FileType>Document</FileType>\r
      <Command Condition="'$(Configuration)|$(Platform)'=='%(name)s'">protoc --cpp_out=%(cpp_out)s --proto_path=%%(RelativeDir) %%(Identity)</Command>\r
      <Outputs Condition="'$(Configuration)|$(Platform)'=='%(name)s'">%(base_out)s.pb.h;%(base_out)s.pb.cc</Outputs>\r
      <Message Condition="'$(Configuration)|$(Platform)'=='%(name)s'">protoc --cpp_out=%(cpp_out)s --proto_path=%%(RelativeDir) %%(Identity)</Message>\r
      <LinkObjects Condition="'$(Configuration)|$(Platform)'=='%(name)s'">false</LinkObjects>\r
"""

V14DSPFiltersHeader = (
'''<?xml version="1.0" encoding="utf-8"?>\r
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\r
''')

#-------------------------------------------------------------------------------

def is_subdir(child, parent):
    '''Determine if child is a subdirectory of parent'''
    return os.path.commonprefix([parent, child]) == parent

def _key(item):
    if isinstance(item, (str, unicode)):
        return ('s', item.upper(), item)
    elif isinstance(item, (int, long, float)):
        return ('n', item)
    elif isinstance(item, (list, tuple)):
        return ('l', map(_key, item))
    elif isinstance(item, dict):
        return ('d', xsorted(item.keys()), xsorted(item.values()))
    elif isinstance(item, Configuration):
        return ('c', _key(item.name), _key(item.target), _key(item.variant), _key(item.platform))
    elif isinstance(item, Item):
        return ('i', _key(winpath(item.path())), _key(item.is_compiled()), _key(item.builder()), _key(item.tag()), _key(item.is_excluded()))
    elif isinstance(item, SCons.Node.FS.File):
        return ('f', _key(item.name), _key(item.suffix))
    else:
        return ('x', item)

def xsorted(tosort, **kwargs):
    '''Performs sorted in a deterministic manner.'''
    if 'key' in kwargs:
        map(kwargs['key'], tosort)
    kwargs['key'] = _key
    return sorted(tosort, **kwargs)

def itemList(items, sep):
    if type(items) == str:  # Won't work in Python 3.
        return items
    def gen():
        for item in xsorted(items):
            if isinstance(item, dict):
                for k, v in xsorted(item.items()):
                    yield k + '=' + v
            elif isinstance(item, (tuple, list)):
                assert len(item) == 2, "Item shoud have exactly two elements: " + str(item)
                yield '%s=%s' % tuple(item)
            else:
                yield item
            yield sep
    return ''.join(gen())

#-------------------------------------------------------------------------------

class SwitchConverter(object):
    '''Converts command line switches to MSBuild XML, using tables'''

    def __init__(self, table, booltable, retable=None):
        self.table = {}
        for key in table:
            self.table[key] = table[key]
        for key in booltable:
            value = booltable[key]
            self.table[key] = [value[0], 'True']
            self.table[key + '-'] = [value[0], 'False']
        if retable != None:
            self.retable = retable
        else:
            self.retable = []

    def getXml(self, switches, prefix = ''):
        switches = list(set(switches))      # Filter dupes because on windows platforms, /nologo is added automatically to the environment.
        xml = []
        for regex, tag in self.retable:
            matches = []
            for switch in switches[:]:
                match = regex.match(switch)
                if None != match:
                    matches.append(match.group(1))
                    switches.remove(switch)
            if len(matches) > 0:
                xml.append (
                    '%s<%s>%s</%s>\r\n' % (
                        prefix, tag, ';'.join(matches), tag))
        unknown = []
        for switch in switches:
            try:
                value = self.table[switch]
                xml.append (
                    '%s<%s>%s</%s>\r\n' % (
                        prefix, value[0], value[1], value[0]))
            except:
                unknown.append(switch)
        if unknown:
            s = itemList(unknown, ' ')
            tag = 'AdditionalOptions'
            xml.append('%(prefix)s<%(tag)s>%(s)s%%(%(tag)s)</%(tag)s>\r\n' % locals())
        if xml:
            return ''.join(xml)
        return ''

class ClSwitchConverter(SwitchConverter):
    def __init__(self):
        booltable = {
            '/C'            : ['KeepComments'],
            '/doc'          : ['GenerateXMLDocumentationFiles'],
            '/FAu'          : ['UseUnicodeForAssemblerListing'],
            '/FC'           : ['UseFullPaths'],
            '/FR'           : ['BrowseInformation'],
            '/Fr'           : ['BrowseInformation'],
            '/Fx'           : ['ExpandAttributedSource'],
            '/GF'           : ['StringPooling'],
            '/GL'           : ['WholeProgramOptimization'],
            '/Gm'           : ['MinimalRebuild'],
            '/GR'           : ['RuntimeTypeInfo'],
            '/GS'           : ['BufferSecurityCheck'],
            '/GT'           : ['EnableFiberSafeOptimizations'],
            '/Gy'           : ['FunctionLevelLinking'],
            '/MP'           : ['MultiProcessorCompilation'],
            '/Oi'           : ['IntrinsicFunctions'],
            '/Oy'           : ['OmitFramePointers'],
            '/RTCc'         : ['SmallerTypeCheck'],
            '/u'            : ['UndefineAllPreprocessorDefinitions'],
            '/X'            : ['IgnoreStandardIncludePath'],
            '/WX'           : ['TreatWarningAsError'],
            '/Za'           : ['DisableLanguageExtensions'],
            '/Zl'           : ['OmitDefaultLibName'],
            '/fp:except'    : ['FloatingPointExceptions'],
            '/hotpatch'     : ['CreateHotpatchableImage'],
            '/nologo'       : ['SuppressStartupBanner'],
            '/openmp'       : ['OpenMPSupport'],
            '/showIncludes' : ['ShowIncludes'],
            '/Zc:forScope'  : ['ForceConformanceInForLoopScope'],
            '/Zc:wchar_t'   : ['TreatWChar_tAsBuiltInType'],
        }
        table = {
            '/EHsc' : ['ExceptionHandling', 'Sync'],
            '/EHa'  : ['ExceptionHandling', 'Async'],
            '/EHs'  : ['ExceptionHandling', 'SyncCThrow'],
            '/FA'   : ['AssemblerOutput', 'AssemblyCode'],
            '/FAcs' : ['AssemblerOutput', 'All'],
            '/FAc'  : ['AssemblerOutput', 'AssemblyAndMachineCode'],
            '/FAs'  : ['AssemblerOutput', 'AssemblyAndSourceCode'],
            '/Gd'   : ['CallingConvention', 'Cdecl'],
            '/Gr'   : ['CallingConvention', 'FastCall'],
            '/Gz'   : ['CallingConvention', 'StdCall'],
            '/MT'   : ['RuntimeLibrary', 'MultiThreaded'],
            '/MTd'  : ['RuntimeLibrary', 'MultiThreadedDebug'],
            '/MD'   : ['RuntimeLibrary', 'MultiThreadedDLL'],
            '/MDd'  : ['RuntimeLibrary', 'MultiThreadedDebugDLL'],
            '/Od'   : ['Optimization', 'Disabled'],
            '/O1'   : ['Optimization', 'MinSpace'],
            '/O2'   : ['Optimization', 'MaxSpeed'],
            '/Ox'   : ['Optimization', 'Full'],
            '/Ob1'  : ['InlineFunctionExpansion', 'OnlyExplicitInline'],
            '/Ob2'  : ['InlineFunctionExpansion', 'AnySuitable'],
            '/Ot'   : ['FavorSizeOrSpeed', 'Speed'],
            '/Os'   : ['FavorSizeOrSpeed', 'Size'],
            '/RTCs' : ['BasicRuntimeChecks', 'StackFrameRuntimeCheck'],
            '/RTCu' : ['BasicRuntimeChecks', 'UninitializedLocalUsageCheck'],
            '/RTC1' : ['BasicRuntimeChecks', 'EnableFastChecks'],
            '/TC'   : ['CompileAs', 'CompileAsC'],
            '/TP'   : ['CompileAs', 'CompileAsCpp'],
            '/W0'   : [ 'WarningLevel', 'TurnOffAllWarnings'],
            '/W1'   : [ 'WarningLevel', 'Level1'],
            '/W2'   : [ 'WarningLevel', 'Level2'],
            '/W3'   : [ 'WarningLevel', 'Level3'],
            '/W4'   : [ 'WarningLevel', 'Level4'],
            '/Wall' : [ 'WarningLevel', 'EnableAllWarnings'],
            '/Yc'   : ['PrecompiledHeader', 'Create'],
            '/Yu'   : ['PrecompiledHeader', 'Use'],
            '/Z7'   : ['DebugInformationFormat', 'OldStyle'],
            '/Zi'   : ['DebugInformationFormat', 'ProgramDatabase'],
            '/ZI'   : ['DebugInformationFormat', 'EditAndContinue'],
            '/Zp1'  : ['StructMemberAlignment', '1Byte'],
            '/Zp2'  : ['StructMemberAlignment', '2Bytes'],
            '/Zp4'  : ['StructMemberAlignment', '4Bytes'],
            '/Zp8'  : ['StructMemberAlignment', '8Bytes'],
            '/Zp16'         : ['StructMemberAlignment', '16Bytes'],
            '/arch:IA32'     : ['EnableEnhancedInstructionSet', 'NoExtensions'],
            '/arch:SSE'      : ['EnableEnhancedInstructionSet', 'StreamingSIMDExtensions'],
            '/arch:SSE2'     : ['EnableEnhancedInstructionSet', 'StreamingSIMDExtensions2'],
            '/arch:AVX'      : ['EnableEnhancedInstructionSet', 'AdvancedVectorExtensions'],
            '/clr'           : ['CompileAsManaged', 'True'],
            '/clr:pure'      : ['CompileAsManaged', 'Pure'],
            '/clr:safe'      : ['CompileAsManaged', 'Safe'],
            '/clr:oldSyntax' : ['CompileAsManaged', 'OldSyntax'],
            '/fp:fast'       : ['FloatingPointModel', 'Fast'],
            '/fp:precise'    : ['FloatingPointModel', 'Precise'],
            '/fp:strict'     : ['FloatingPointModel', 'Strict'],
            '/errorReport:none'   : ['ErrorReporting', 'None'],
            '/errorReport:prompt' : ['ErrorReporting', 'Prompt'],
            '/errorReport:queue'  : ['ErrorReporting', 'Queue'],
            '/errorReport:send'   : ['ErrorReporting', 'Send'],
        }
        retable = [
            (re.compile(r'/wd\"(\d+)\"'), 'DisableSpecificWarnings'),
        ]
        # Ideas from Google's Generate Your Project
        '''
        _Same(_compile, 'AdditionalIncludeDirectories', _folder_list)  # /I

        _Same(_compile, 'PreprocessorDefinitions', _string_list)  # /D
        _Same(_compile, 'ProgramDataBaseFileName', _file_name)  # /Fd

        _Same(_compile, 'AdditionalOptions', _string_list)
        _Same(_compile, 'AdditionalUsingDirectories', _folder_list)  # /AI
        _Same(_compile, 'AssemblerListingLocation', _file_name)  # /Fa
        _Same(_compile, 'BrowseInformationFile', _file_name)
        _Same(_compile, 'ForcedIncludeFiles', _file_list)  # /FI
        _Same(_compile, 'ForcedUsingFiles', _file_list)  # /FU
        _Same(_compile, 'UndefinePreprocessorDefinitions', _string_list)  # /U
        _Same(_compile, 'XMLDocumentationFileName', _file_name)
           ''    : ['EnablePREfast', _boolean)  # /analyze Visible='false'
        _Renamed(_compile, 'ObjectFile', 'ObjectFileName', _file_name)  # /Fo
        _Renamed(_compile, 'PrecompiledHeaderThrough', 'PrecompiledHeaderFile',
                 _file_name)  # Used with /Yc and /Yu
        _Renamed(_compile, 'PrecompiledHeaderFile', 'PrecompiledHeaderOutputFile',
                 _file_name)  # /Fp
        _ConvertedToAdditionalOption(_compile, 'DefaultCharIsUnsigned', '/J')
        _MSBuildOnly(_compile, 'ProcessorNumber', _integer)  # the number of processors
        _MSBuildOnly(_compile, 'TrackerLogDirectory', _folder_name)
        _MSBuildOnly(_compile, 'TreatSpecificWarningsAsErrors', _string_list)  # /we
        _MSBuildOnly(_compile, 'PreprocessOutputPath', _string)  # /Fi
        '''
        SwitchConverter.__init__(self, table, booltable, retable)

class LinkSwitchConverter(SwitchConverter):
    def __init__(self):
        # Based on code in Generate Your Project
        booltable = {
            '/DEBUG'                : ['GenerateDebugInformation'],
            '/DYNAMICBASE'          : ['RandomizedBaseAddress'],
            '/NOLOGO'               : ['SuppressStartupBanner'],
            '/nologo'               : ['SuppressStartupBanner'],
        }
        table = {
            '/ERRORREPORT:NONE'     : ['ErrorReporting', 'NoErrorReport'],
            '/ERRORREPORT:PROMPT'   : ['ErrorReporting', 'PromptImmediately'],
            '/ERRORREPORT:QUEUE'    : ['ErrorReporting', 'QueueForNextLogin'],
            '/ERRORREPORT:SEND'     : ['ErrorReporting', 'SendErrorReport'],
            '/MACHINE:X86'          : ['TargetMachine', 'MachineX86'],
            '/MACHINE:ARM'          : ['TargetMachine', 'MachineARM'],
            '/MACHINE:EBC'          : ['TargetMachine', 'MachineEBC'],
            '/MACHINE:IA64'         : ['TargetMachine', 'MachineIA64'],
            '/MACHINE:MIPS'         : ['TargetMachine', 'MachineMIPS'],
            '/MACHINE:MIPS16'       : ['TargetMachine', 'MachineMIPS16'],
            '/MACHINE:MIPSFPU'      : ['TargetMachine', 'MachineMIPSFPU'],
            '/MACHINE:MIPSFPU16'    : ['TargetMachine', 'MachineMIPSFPU16'],
            '/MACHINE:SH4'          : ['TargetMachine', 'MachineSH4'],
            '/MACHINE:THUMB'        : ['TargetMachine', 'MachineTHUMB'],
            '/MACHINE:X64'          : ['TargetMachine', 'MachineX64'],
            '/NXCOMPAT'             : ['DataExecutionPrevention', 'true'],
            '/NXCOMPAT:NO'          : ['DataExecutionPrevention', 'false'],
            '/SUBSYSTEM:CONSOLE'                    : ['SubSystem', 'Console'],
            '/SUBSYSTEM:WINDOWS'                    : ['SubSystem', 'Windows'],
            '/SUBSYSTEM:NATIVE'                     : ['SubSystem', 'Native'],
            '/SUBSYSTEM:EFI_APPLICATION'            : ['SubSystem', 'EFI Application'],
            '/SUBSYSTEM:EFI_BOOT_SERVICE_DRIVER'    : ['SubSystem', 'EFI Boot Service Driver'],
            '/SUBSYSTEM:EFI_ROM'                    : ['SubSystem', 'EFI ROM'],
            '/SUBSYSTEM:EFI_RUNTIME_DRIVER'         : ['SubSystem', 'EFI Runtime'],
            '/SUBSYSTEM:WINDOWSCE'                  : ['SubSystem', 'WindowsCE'],
            '/SUBSYSTEM:POSIX'                      : ['SubSystem', 'POSIX'],
        }
        '''
        /TLBID:1 /MANIFEST /MANIFESTUAC:level='asInvoker' uiAccess='false'

        _Same(_link, 'AllowIsolation', _boolean)  # /ALLOWISOLATION
        _Same(_link, 'CLRUnmanagedCodeCheck', _boolean)  # /CLRUNMANAGEDCODECHECK
        _Same(_link, 'DelaySign', _boolean)  # /DELAYSIGN
        _Same(_link, 'EnableUAC', _boolean)  # /MANIFESTUAC
        _Same(_link, 'GenerateMapFile', _boolean)  # /MAP
        _Same(_link, 'IgnoreAllDefaultLibraries', _boolean)  # /NODEFAULTLIB
        _Same(_link, 'IgnoreEmbeddedIDL', _boolean)  # /IGNOREIDL
        _Same(_link, 'MapExports', _boolean)  # /MAPINFO:EXPORTS
        _Same(_link, 'StripPrivateSymbols', _file_name)  # /PDBSTRIPPED
        _Same(_link, 'PerUserRedirection', _boolean)
        _Same(_link, 'Profile', _boolean)  # /PROFILE
        _Same(_link, 'RegisterOutput', _boolean)
        _Same(_link, 'SetChecksum', _boolean)  # /RELEASE
        _Same(_link, 'SupportUnloadOfDelayLoadedDLL', _boolean)  # /DELAY:UNLOAD
        
        _Same(_link, 'SwapRunFromCD', _boolean)  # /SWAPRUN:CD
        _Same(_link, 'TurnOffAssemblyGeneration', _boolean)  # /NOASSEMBLY
        _Same(_link, 'UACUIAccess', _boolean)  # /uiAccess='true'
        _Same(_link, 'EnableCOMDATFolding', _newly_boolean)  # /OPT:ICF
        _Same(_link, 'FixedBaseAddress', _newly_boolean)  # /FIXED
        _Same(_link, 'LargeAddressAware', _newly_boolean)  # /LARGEADDRESSAWARE
        _Same(_link, 'OptimizeReferences', _newly_boolean)  # /OPT:REF
        _Same(_link, 'TerminalServerAware', _newly_boolean)  # /TSAWARE

        _Same(_link, 'AdditionalDependencies', _file_list)
        _Same(_link, 'AdditionalLibraryDirectories', _folder_list)  # /LIBPATH 
        _Same(_link, 'AdditionalManifestDependencies', _file_list)  # /MANIFESTDEPENDENCY:
        _Same(_link, 'AdditionalOptions', _string_list)
        _Same(_link, 'AddModuleNamesToAssembly', _file_list)  # /ASSEMBLYMODULE
        _Same(_link, 'AssemblyLinkResource', _file_list)  # /ASSEMBLYLINKRESOURCE
        _Same(_link, 'BaseAddress', _string)  # /BASE
        _Same(_link, 'DelayLoadDLLs', _file_list)  # /DELAYLOAD
        _Same(_link, 'EmbedManagedResourceFile', _file_list)  # /ASSEMBLYRESOURCE
        _Same(_link, 'EntryPointSymbol', _string)  # /ENTRY
        _Same(_link, 'ForceSymbolReferences', _file_list)  # /INCLUDE
        _Same(_link, 'FunctionOrder', _file_name)  # /ORDER
        _Same(_link, 'HeapCommitSize', _string)
        _Same(_link, 'HeapReserveSize', _string)  # /HEAP
        _Same(_link, 'ImportLibrary', _file_name)  # /IMPLIB
        _Same(_link, 'KeyContainer', _file_name)  # /KEYCONTAINER
        _Same(_link, 'KeyFile', _file_name)  # /KEYFILE
        _Same(_link, 'ManifestFile', _file_name)  # /ManifestFile
        _Same(_link, 'MapFileName', _file_name)
        _Same(_link, 'MergedIDLBaseFileName', _file_name)  # /IDLOUT
        _Same(_link, 'MergeSections', _string)  # /MERGE
        _Same(_link, 'MidlCommandFile', _file_name)  # /MIDL
        _Same(_link, 'ModuleDefinitionFile', _file_name)  # /DEF
        _Same(_link, 'OutputFile', _file_name)  # /OUT
        _Same(_link, 'ProfileGuidedDatabase', _file_name)  # /PGD
        _Same(_link, 'ProgramDatabaseFile', _file_name)  # /PDB
        _Same(_link, 'StackCommitSize', _string)
        _Same(_link, 'StackReserveSize', _string)  # /STACK
        _Same(_link, 'TypeLibraryFile', _file_name)  # /TLBOUT
        _Same(_link, 'TypeLibraryResourceID', _integer)  # /TLBID
        _Same(_link, 'Version', _string)  # /VERSION


        _Same(_link, 'AssemblyDebug',
              _Enumeration(['',
                            'true',  # /ASSEMBLYDEBUG
                            'false']))  # /ASSEMBLYDEBUG:DISABLE
        _Same(_link, 'CLRImageType',
              _Enumeration(['Default',
                            'ForceIJWImage',  # /CLRIMAGETYPE:IJW
                            'ForcePureILImage',  # /Switch="CLRIMAGETYPE:PURE
                            'ForceSafeILImage']))  # /Switch="CLRIMAGETYPE:SAFE
        _Same(_link, 'CLRThreadAttribute',
              _Enumeration(['DefaultThreadingAttribute',  # /CLRTHREADATTRIBUTE:NONE
                            'MTAThreadingAttribute',  # /CLRTHREADATTRIBUTE:MTA
                            'STAThreadingAttribute']))  # /CLRTHREADATTRIBUTE:STA
        _Same(_link, 'Driver',
              _Enumeration(['NotSet',
                            'Driver',  # /Driver
                            'UpOnly',  # /DRIVER:UPONLY
                            'WDM']))  # /DRIVER:WDM
        _Same(_link, 'LinkTimeCodeGeneration',
              _Enumeration(['Default',
                            'UseLinkTimeCodeGeneration',  # /LTCG
                            'PGInstrument',  # /LTCG:PGInstrument
                            'PGOptimization',  # /LTCG:PGOptimize
                            'PGUpdate']))  # /LTCG:PGUpdate
        _Same(_link, 'ShowProgress',
              _Enumeration(['NotSet',
                            'LinkVerbose',  # /VERBOSE
                            'LinkVerboseLib'],  # /VERBOSE:Lib
                           new=['LinkVerboseICF',  # /VERBOSE:ICF
                                'LinkVerboseREF',  # /VERBOSE:REF
                                'LinkVerboseSAFESEH',  # /VERBOSE:SAFESEH
                                'LinkVerboseCLR']))  # /VERBOSE:CLR
        _Same(_link, 'UACExecutionLevel',
              _Enumeration(['AsInvoker',  # /level='asInvoker'
                            'HighestAvailable',  # /level='highestAvailable'
                            'RequireAdministrator']))  # /level='requireAdministrator'
        _Same(_link, 'MinimumRequiredVersion', _string)
        _Same(_link, 'TreatLinkerWarningAsErrors', _boolean)  # /WX


        # Options found in MSVS that have been renamed in MSBuild.
        _Renamed(_link, 'IgnoreDefaultLibraryNames', 'IgnoreSpecificDefaultLibraries',
                 _file_list)  # /NODEFAULTLIB
        _Renamed(_link, 'ResourceOnlyDLL', 'NoEntryPoint', _boolean)  # /NOENTRY
        _Renamed(_link, 'SwapRunFromNet', 'SwapRunFromNET', _boolean)  # /SWAPRUN:NET

        _Moved(_link, 'GenerateManifest', '', _boolean)
        _Moved(_link, 'IgnoreImportLibrary', '', _boolean)
        _Moved(_link, 'LinkIncremental', '', _newly_boolean)
        _Moved(_link, 'LinkLibraryDependencies', 'ProjectReference', _boolean)
        _Moved(_link, 'UseLibraryDependencyInputs', 'ProjectReference', _boolean)

        # MSVS options not found in MSBuild.
        _MSVSOnly(_link, 'OptimizeForWindows98', _newly_boolean)
        _MSVSOnly(_link, 'UseUnicodeResponseFiles', _boolean)

        # MSBuild options not found in MSVS.
        _MSBuildOnly(_link, 'BuildingInIDE', _boolean)
        _MSBuildOnly(_link, 'ImageHasSafeExceptionHandlers', _boolean)  # /SAFESEH
        _MSBuildOnly(_link, 'LinkDLL', _boolean)  # /DLL Visible='false'
        _MSBuildOnly(_link, 'LinkStatus', _boolean)  # /LTCG:STATUS
        _MSBuildOnly(_link, 'PreventDllBinding', _boolean)  # /ALLOWBIND
        _MSBuildOnly(_link, 'SupportNobindOfDelayLoadedDLL', _boolean)  # /DELAY:NOBIND
        _MSBuildOnly(_link, 'TrackerLogDirectory', _folder_name)
        _MSBuildOnly(_link, 'MSDOSStubFileName', _file_name)  # /STUB Visible='false'
        _MSBuildOnly(_link, 'SectionAlignment', _integer)  # /ALIGN
        _MSBuildOnly(_link, 'SpecifySectionAttributes', _string)  # /SECTION
        _MSBuildOnly(_link, 'ForceFileOutput',
                     _Enumeration([], new=['Enabled',  # /FORCE
                                           # /FORCE:MULTIPLE
                                           'MultiplyDefinedSymbolOnly',
                                           'UndefinedSymbolOnly']))  # /FORCE:UNRESOLVED
        _MSBuildOnly(_link, 'CreateHotPatchableImage',
                     _Enumeration([], new=['Enabled',  # /FUNCTIONPADMIN
                                           'X86Image',  # /FUNCTIONPADMIN:5
                                           'X64Image',  # /FUNCTIONPADMIN:6
                                           'ItaniumImage']))  # /FUNCTIONPADMIN:16
        _MSBuildOnly(_link, 'CLRSupportLastError',
                     _Enumeration([], new=['Enabled',  # /CLRSupportLastError
                                           'Disabled',  # /CLRSupportLastError:NO
                                           # /CLRSupportLastError:SYSTEMDLL
                                           'SystemDlls']))

        '''
        SwitchConverter.__init__(self, table, booltable)

CLSWITCHES = ClSwitchConverter()
LINKSWITCHES = LinkSwitchConverter()

#-------------------------------------------------------------------------------

# Return a Windows path from a native path
def winpath(path):
    drive, rest = ntpath.splitdrive(path)
    result = []
    while rest and rest != ntpath.sep:
        rest, part = ntpath.split(rest)
        result.insert(0, part)
    if rest:
        result.insert(0, rest)
    return ntpath.join(drive.upper(), *result)

def makeList(x):
    if not x:
        return []
    if type(x) is not list:
        return [x]
    return x

#-------------------------------------------------------------------------------

class Configuration(object):
    def __init__(self, variant, platform, target, env):
        self.name = '%s|%s' % (variant, platform)
        self.variant = variant
        self.platform = platform
        self.target = target
        self.env = env

#-------------------------------------------------------------------------------

class Item(object):
    '''Represents a file item in the Solution Explorer'''
    def __init__(self, path, builder):
        self._path = path
        self._builder = builder
        self.node = dict()

        if builder == 'Object':
            self._tag = 'ClCompile'
            self._excluded = False
        elif builder == 'Protoc':
            self._tag = 'CustomBuild'
            self._excluded = False
        else:
            ext = os.path.splitext(self._path)[1]
            if ext in ['.c', '.cc', '.cpp']:
                self._tag = 'ClCompile'
                self._excluded = True
            else:
                if ext in ['.h', '.hpp', '.hxx', '.inl', '.inc']:
                    self._tag = 'ClInclude'
                else:
                    self._tag = 'None'
                self._excluded = False;

    def __repr__(self):
        return '<VSProject.Item "%s" %s>' % (
            self.path, self.tag, str(self.node))

    def path(self):
        return self._path

    def tag(self):
        return self._tag

    def builder(self):
        return self._builder

    def is_compiled(self):
        return self._builder == 'Object'

    def is_excluded(self):
        return self._excluded

#-------------------------------------------------------------------------------

def _guid(seed, name = None):
    m = hashlib.md5()
    m.update(seed)
    if name:
        m.update(name)
    d = m.hexdigest().upper()
    guid = "{%s-%s-%s-%s-%s}" % (d[:8], d[8:12], d[12:16], d[16:20], d[20:32])
    return guid

class _ProjectGenerator(object):
    '''Generates a project file for Visual Studio 2013'''

    def __init__(self, project_node, filters_node, env):
        try:
            self.configs = xsorted(env['VSPROJECT_CONFIGS'])
        except KeyError:
            raise ValueError ('Missing VSPROJECT_CONFIGS')
        self.root_dir = os.getcwd()
        self.root_dirs = [os.path.abspath(x) for x in makeList(env['VSPROJECT_ROOT_DIRS'])]
        self.project_dir = os.path.dirname(os.path.abspath(str(project_node)))
        self.project_node = project_node
        self.project_file = None
        self.filters_node = filters_node
        self.filters_file = None
        self.guid = _guid(os.path.basename(str(self.project_node)))
        self.buildItemList(env)

    def buildItemList(self, env):
        '''Build the Item set associated with the configurations'''
        items = {}
        def _walk(target, items, prefix=''):
            if os.path.isabs(str(target)):
                return
            if target.has_builder():
                builder = target.get_builder().get_name(env)
                bsources = target.get_binfo().bsources
                if builder == 'Program':
                    for child in bsources:
                        _walk(child, items, prefix+'  ')
                else:
                    for child in bsources:
                        item = items.setdefault(str(child), Item(str(child), builder=builder))
                        item.node[config] = target
                        _walk(child, items, prefix+'  ')
                    for child in target.children(scan=1):
                        if not os.path.isabs(str(child)):
                            item = items.setdefault(str(child), Item(str(child), builder=None))
                            _walk(child, items, prefix+'  ')
        for config in self.configs:
            targets = config.target
            for target in targets:
                _walk(target, items)
        self.items = xsorted(items.values())

    def makeListTag(self, items, prefix, tag, attrs, inherit=True):
        '''Builds an XML tag string from a list of items. If items is
        empty, then the returned string is empty.'''
        if not items:
            return ''
        s = '%(prefix)s<%(tag)s%(attrs)s>' % locals()
        s += ';'.join(items)
        if inherit:
            s += ';%%(%(tag)s)' % locals()
        s += '</%(tag)s>\r\n' % locals()
        return s

    def relPaths(self, paths):
        items = []
        for path in paths:
            if not os.path.isabs(path):
                items.append(winpath(os.path.relpath(path, self.project_dir)))
        return items

    def extraRelPaths(self, paths, base):
        extras = []
        for path in paths:
            if not path in base:
                extras.append(path)
        return self.relPaths(extras)

    def writeHeader(self):
        global clSwitches

        encoding = 'utf-8'
        project_guid = self.guid
        name = os.path.splitext(os.path.basename(str(self.project_node)))[0]

        f = self.project_file
        f.write(UnicodeByteMarker)
        f.write(V14DSPHeader % locals())
        f.write(V14DSPGlobals % locals())
        f.write('  <ItemGroup Label="ProjectConfigurations">\r\n')
        for config in self.configs:
            variant = config.variant
            platform = config.platform           
            f.write(V14DSPProjectConfiguration % locals())
        f.write('  </ItemGroup>\r\n')

        f.write('  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />\r\n')
        for config in self.configs:
            variant = config.variant
            platform = config.platform
            use_debug_libs = variant == 'Debug'
            variant_dir = os.path.relpath(os.path.dirname(
                config.target[0].get_abspath()), self.project_dir)
            out_dir = winpath(variant_dir) + ntpath.sep
            int_dir = winpath(ntpath.join(variant_dir, 'src')) + ntpath.sep
            f.write(V14DSPPropertyGroup % locals())

        f.write('  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />\r\n')
        f.write('  <ImportGroup Label="ExtensionSettings" />\r\n')
        for config in self.configs:
            variant = config.variant
            platform = config.platform
            f.write(V14DSPImportGroup % locals())

        f.write('  <PropertyGroup Label="UserMacros" />\r\n')
        for config in self.configs:
            variant = config.variant
            platform = config.platform
            f.write(V14DSPItemDefinitionGroup % locals())
            # Cl options
            f.write('    <ClCompile>\r\n')
            f.write(
                '      <PreprocessorDefinitions>%s%%(PreprocessorDefinitions)</PreprocessorDefinitions>\r\n' % (
                    itemList(config.env['CPPDEFINES'], ';')))
            props = ''
            props += self.makeListTag(self.relPaths(xsorted(config.env['CPPPATH'])),
                '      ', 'AdditionalIncludeDirectories', '', True)
            f.write(props)
            f.write(CLSWITCHES.getXml(xsorted(config.env['CCFLAGS']), '      '))
            f.write('    </ClCompile>\r\n')

            f.write('    <Link>\r\n')
            props = ''
            props += self.makeListTag(xsorted(config.env['LIBS']),
                '      ', 'AdditionalDependencies', '', True)
            try:
                props += self.makeListTag(self.relPaths(xsorted(config.env['LIBPATH'])),
                    '      ', 'AdditionalLibraryDirectories', '', True)
            except:
                pass
            f.write(props)
            f.write(LINKSWITCHES.getXml(xsorted(config.env['LINKFLAGS']), '      '))
            f.write('    </Link>\r\n')

            f.write('  </ItemDefinitionGroup>\r\n')

    def writeProject(self):
        self.writeHeader()

        f = self.project_file
        self.project_file.write('  <ItemGroup>\r\n')
        for item in self.items:
            path = winpath(os.path.relpath(item.path(), self.project_dir))
            tag = item.tag()
            props = ''
            if item.builder() == 'Object':
                props = ''
                for config in self.configs:
                    name = config.name
                    variant = config.variant
                    platform = config.platform
                    if not config in item.node:
                        props += \
                            '''      <ExcludedFromBuild Condition="'$(Configuration)|$(Platform)'=='%(variant)s|%(platform)s'">True</ExcludedFromBuild>\r\n''' % locals()
                for config, output in xsorted(item.node.items()):
                    name = config.name
                    env = output.get_build_env()
                    variant = config.variant
                    platform = config.platform
                    props += self.makeListTag(self.extraRelPaths(xsorted(env['CPPPATH']), config.env['CPPPATH']),
                        '      ', 'AdditionalIncludeDirectories',
                        ''' Condition="'$(Configuration)|$(Platform)'=='%(variant)s|%(platform)s'"''' % locals(),
                        True)
            elif item.is_excluded():
                props = '      <ExcludedFromBuild>True</ExcludedFromBuild>\r\n'
            elif item.builder() == 'Protoc':
                for config, output in xsorted(item.node.items()):
                    name = config.name
                    out_dir = os.path.relpath(os.path.dirname(str(output)), self.project_dir)
                    cpp_out = winpath(out_dir)
                    out_parts = out_dir.split(os.sep)
                    out_parts.append(os.path.splitext(os.path.basename(item.path()))[0])
                    base_out = ntpath.join(*out_parts)
                    props += V14CustomBuildProtoc % locals()
 
            f.write('    <%(tag)s Include="%(path)s">\r\n' % locals())
            f.write(props)
            f.write('    </%(tag)s>\r\n' % locals())
        f.write('  </ItemGroup>\r\n')

        f.write(
            '  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />\r\n'
            '  <ImportGroup Label="ExtensionTargets">\r\n'
            '  </ImportGroup>\r\n'
            '</Project>\r\n')

    def writeFilters(self):
        def getGroup(abspath):
            abspath = os.path.dirname(abspath)
            for d in self.root_dirs:
                common = os.path.commonprefix([abspath, d])
                if common == d:
                    return winpath(os.path.relpath(abspath, common))
            return winpath(os.path.split(abspath)[1])

        f = self.filters_file
        f.write(UnicodeByteMarker)
        f.write(V14DSPFiltersHeader)

        f.write('  <ItemGroup>\r\n')
        groups = set()
        for item in self.items:
            group = getGroup(os.path.abspath(item.path()))
            while group != '':
                groups.add(group)
                group = ntpath.split(group)[0]
        for group in xsorted(groups):
            guid = _guid(self.guid, group)
            f.write(
                '    <Filter Include="%(group)s">\r\n'
                '      <UniqueIdentifier>%(guid)s</UniqueIdentifier>\r\n'
                '    </Filter>\r\n' % locals())
        f.write('  </ItemGroup>\r\n')

        f.write('  <ItemGroup>\r\n')
        for item in self.items:
            path = os.path.abspath(item.path())
            group = getGroup(path)
            path = winpath(os.path.relpath(path, self.project_dir))
            tag = item.tag()
            f.write (
                '    <%(tag)s Include="%(path)s">\r\n'
                '      <Filter>%(group)s</Filter>\r\n'
                '    </%(tag)s>\r\n' % locals())
        f.write('  </ItemGroup>\r\n')
        f.write('</Project>\r\n')

    def build(self):
        try:
            self.project_file = open(str(self.project_node), 'wb')
        except (IOError, detail):
            raise SCons.Errors.InternalError('Unable to open "' +
                str(self.project_node) + '" for writing:' + str(detail))
        try:
            self.filters_file = open(str(self.filters_node), 'wb')
        except (IOError, detail):
            raise SCons.Errors.InternalError('Unable to open "' +
                str(self.filters_node) + '" for writing:' + str(detail))
        self.writeProject()
        self.writeFilters()
        self.project_file.close()
        self.filters_file.close()

#-------------------------------------------------------------------------------

class _SolutionGenerator(object):
    def __init__(self, slnfile, projfile, env):
        pass

    def build(self):
        pass

#-------------------------------------------------------------------------------

# Generate the VS2013 project
def buildProject(target, source, env):
    if env.get('auto_build_solution', 1):
        if len(target) != 3:
            raise ValueError ("Unexpected len(target) != 3")
    if not env.get('auto_build_solution', 1):
        if len(target) != 2:
            raise ValueError ("Unexpected len(target) != 2")

    g = _ProjectGenerator (target[0], target[1], env)
    g.build()

    if env.get('auto_build_solution', 1):
        g = _SolutionGenerator (target[2], target[0], env)
        g.build()

def projectEmitter(target, source, env):
    if len(target) != 1:
        raise ValueError ("Exactly one target must be specified")

    # If source is unspecified this condition will be true
    if not source or source[0] == target[0]:
        source = []

    outputs = []
    for node in list(target):
        path = env.GetBuildPath(node)
        outputs.extend([
            path + '.vcxproj',
            path + '.vcxproj.filters'])
        if env.get('auto_build_solution', 1):
            outputs.append(path + '.sln')
    return outputs, source

projectBuilder = SCons.Builder.Builder(
    action = SCons.Action.Action(buildProject, "Building ${TARGET}"),
    emitter = projectEmitter)

def createConfig(self, variant, platform, target, env):
    return Configuration(variant, platform, target, env)

def generate(env):
    '''Add Builders and construction variables for Microsoft Visual
    Studio project files to an Environment.'''
    try:
      env['BUILDERS']['VSProject']
    except KeyError:
      env['BUILDERS']['VSProject'] = projectBuilder
    env.AddMethod(createConfig, 'VSProjectConfig')

def exists(env):
    return True
