# Copyright 2014 Vinnie Falco (vinnie.falco@gmail.com)
# Portions Copyright The SCons Foundation
# This file is part of beast

"""
A SCons tool to provide a family of scons builders that
generate Visual Studio project files
"""

import itertools
import ntpath
import os
import random

import SCons.Builder

#-------------------------------------------------------------------------------

def _generateGUID(slnfile, name):
    """This generates a dummy GUID for the sln file to use.  It is
    based on the MD5 signatures of the sln filename plus the name of
    the project.  It basically just needs to be unique, and not
    change with each invocation."""
    m = hashlib.md5()
    # Normalize the slnfile path to a Windows path (\ separators) so
    # the generated file has a consistent GUID even if we generate
    # it on a non-Windows platform.
    m.update(ntpath.normpath(str(slnfile)) + str(name))
    solution = m.hexdigest().upper()
    # convert most of the signature to GUID form (discard the rest)
    solution = "{" + solution[:8] + "-" + solution[8:12] + "-" + solution[12:16] + "-" + solution[16:20] + "-" + solution[20:32] + "}"
    return solution

def _unique_id(seed):
    r = random.Random()
    r.seed (seed)
    s = "{%0.8x-%0.4x-%0.4x-%0.4x-%0.12x}" % (
        r.getrandbits(4*8),
        r.getrandbits(2*8),
        r.getrandbits(2*8),
        r.getrandbits(2*8),
        r.getrandbits(6*8))
    return s

# Return a Windows path from a native path
def winpath(path):
    return ntpath.join(*os.path.split(path))

def nodekind(node):
    e = os.path.splitext(node.get_abspath())[1]
    if e in ['.cpp', '.c']:
        return 'compile'
    if e in ['.h', '.hpp']:
        return 'include'
    return ''

def is_unity(node):
    b, _ = os.path.splitext(node.get_abspath())
    return os.path.splitext(b)[1] == '.unity'

class Config(object):
    pass

#-------------------------------------------------------------------------------

_xml_vcxproj_header = (
'''<?xml version="1.0" encoding="utf-8"?>\r
<Project DefaultTargets="Build" ToolsVersion="12.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\r
\t<ItemGroup Label="ProjectConfigurations">\r
\t\t<ProjectConfiguration Include="Debug|Win32">\r
\t\t\t<Configuration>Debug</Configuration>\r
\t\t\t<Platform>Win32</Platform>\r
\t\t</ProjectConfiguration>\r
\t\t<ProjectConfiguration Include="Debug|x64">\r
\t\t\t<Configuration>Debug</Configuration>\r
\t\t\t<Platform>x64</Platform>\r
\t\t</ProjectConfiguration>\r
\t\t<ProjectConfiguration Include="Release|Win32">\r
\t\t\t<Configuration>Release</Configuration>\r
\t\t\t<Platform>Win32</Platform>\r
\t\t</ProjectConfiguration>\r
\t\t<ProjectConfiguration Include="Release|x64">\r
\t\t\t<Configuration>Release</Configuration>\r
\t\t\t<Platform>x64</Platform>\r
\t</ProjectConfiguration>\r
\t</ItemGroup>\r
\t<ItemGroup>\r
''')

_xml_vcxproj_props = (
'''\t</ItemGroup>\r
\t<PropertyGroup Label="Globals">\r
\t\t<ProjectGuid>{73C5A0F0-7629-4DE7-9194-BE7AC6C19535}</ProjectGuid>\r
\t\t<Keyword>Win32Proj</Keyword>\r
\t\t<RootNamespace>beast</RootNamespace>\r
\t\t<IgnoreWarnCompileDuplicatedFilename>true</IgnoreWarnCompileDuplicatedFilename>\r
\t</PropertyGroup>\r
\t<Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />\r
\t<PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Debug|Win32'" Label="Configuration">\r
\t\t<ConfigurationType>StaticLibrary</ConfigurationType>\r
\t\t<UseDebugLibraries>true</UseDebugLibraries>\r
\t\t<PlatformToolset>v120</PlatformToolset>\r
\t\t<CharacterSet>Unicode</CharacterSet>\r
\t</PropertyGroup>\r
\t<PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'" Label="Configuration">\r
\t\t<ConfigurationType>StaticLibrary</ConfigurationType>\r
\t\t<UseDebugLibraries>true</UseDebugLibraries>\r
\t\t<PlatformToolset>v120</PlatformToolset>\r
\t\t<CharacterSet>Unicode</CharacterSet>\r
\t</PropertyGroup>\r
\t<PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|Win32'" Label="Configuration">\r
\t\t<ConfigurationType>StaticLibrary</ConfigurationType>\r
\t\t<UseDebugLibraries>false</UseDebugLibraries>\r
\t\t<PlatformToolset>v120</PlatformToolset>\r
\t\t<WholeProgramOptimization>true</WholeProgramOptimization>\r
\t\t<CharacterSet>Unicode</CharacterSet>\r
\t</PropertyGroup>\r
\t<PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'" Label="Configuration">\r
\t\t<ConfigurationType>StaticLibrary</ConfigurationType>\r
\t\t<UseDebugLibraries>false</UseDebugLibraries>\r
\t\t<PlatformToolset>v120</PlatformToolset>\r
\t\t<WholeProgramOptimization>true</WholeProgramOptimization>\r
\t\t<CharacterSet>Unicode</CharacterSet>\r
\t</PropertyGroup>\r
\t<Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />\r
\t<ImportGroup Label="ExtensionSettings">\r
\t</ImportGroup>\r
\t<ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='Debug|Win32'">\r
\t\t<Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />\r
\t\t<Import Project="Beast.props" />\r
\t</ImportGroup>\r
\t<ImportGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'" Label="PropertySheets">\r
\t\t<Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />\r
\t\t<Import Project="Beast.props" />\r
\t</ImportGroup>\r
\t<ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='Release|Win32'">\r
\t\t<Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />\r
\t\t<Import Project="Beast.props" />\r
\t</ImportGroup>\r
\t<ImportGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'" Label="PropertySheets">\r
\t\t<Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />\r
\t\t<Import Project="Beast.props" />\r
\t</ImportGroup>\r
\t<PropertyGroup Label="UserMacros" />\r
\t<PropertyGroup />\r
\t<ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Debug|Win32'">\r
\t\t<ClCompile>\r
\t\t\t<PrecompiledHeader>\r
\t\t\t</PrecompiledHeader>\r
\t\t\t<Optimization>Disabled</Optimization>\r
\t\t\t<PreprocessorDefinitions>WIN32;_DEBUG;_LIB;%(PreprocessorDefinitions)</PreprocessorDefinitions>\r
\t\t\t<DisableLanguageExtensions>false</DisableLanguageExtensions>\r
\t\t\t<RuntimeTypeInfo>true</RuntimeTypeInfo>\r
\t\t</ClCompile>\r
\t\t<Link>\r
\t\t\t<SubSystem>Windows</SubSystem>\r
\t\t\t<GenerateDebugInformation>true</GenerateDebugInformation>\r
\t\t</Link>\r
\t</ItemDefinitionGroup>\r
\t<ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">\r
\t\t<ClCompile>\r
\t\t\t<PrecompiledHeader>\r
\t\t\t</PrecompiledHeader>\r
\t\t\t<Optimization>Disabled</Optimization>\r
\t\t\t<PreprocessorDefinitions>WIN32;_DEBUG;_LIB;%(PreprocessorDefinitions)</PreprocessorDefinitions>\r
\t\t\t<RuntimeLibrary>MultiThreadedDebug</RuntimeLibrary>\r
\t\t</ClCompile>\r
\t\t<Link>\r
\t\t\t<SubSystem>Windows</SubSystem>\r
\t\t\t<GenerateDebugInformation>true</GenerateDebugInformation>\r
\t\t</Link>\r
\t</ItemDefinitionGroup>\r
\t<ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|Win32'">\r
\t\t<ClCompile>\r
\t\t\t<PrecompiledHeader>\r
\t\t\t</PrecompiledHeader>\r
\t\t\t<Optimization>MaxSpeed</Optimization>\r
\t\t\t<FunctionLevelLinking>true</FunctionLevelLinking>\r
\t\t\t<IntrinsicFunctions>true</IntrinsicFunctions>\r
\t\t\t<PreprocessorDefinitions>WIN32;NDEBUG;_LIB;%(PreprocessorDefinitions)</PreprocessorDefinitions>\r
\t\t\t<AdditionalIncludeDirectories>$(ProjectDir);%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>\r
\t\t\t<DisableLanguageExtensions>false</DisableLanguageExtensions>\r
\t\t\t<RuntimeTypeInfo>true</RuntimeTypeInfo>\r
\t\t</ClCompile>\r
\t\t<Link>\r
\t\t\t<SubSystem>Windows</SubSystem>\r
\t\t\t<GenerateDebugInformation>true</GenerateDebugInformation>\r
\t\t\t<EnableCOMDATFolding>true</EnableCOMDATFolding>\r
\t\t\t<OptimizeReferences>true</OptimizeReferences>\r
\t\t</Link>\r
\t</ItemDefinitionGroup>\r
\t<ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">\r
\t\t<ClCompile>\r
\t\t\t<PrecompiledHeader>\r
\t\t\t</PrecompiledHeader>\r
\t\t\t<Optimization>MaxSpeed</Optimization>\r
\t\t\t<FunctionLevelLinking>true</FunctionLevelLinking>\r
\t\t\t<IntrinsicFunctions>true</IntrinsicFunctions>\r
\t\t\t<PreprocessorDefinitions>WIN32;NDEBUG;_LIB;%(PreprocessorDefinitions)</PreprocessorDefinitions>\r
\t\t\t<AdditionalIncludeDirectories>$(ProjectDir);%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>\r
\t\t\t<RuntimeLibrary>MultiThreaded</RuntimeLibrary>\r
\t\t</ClCompile>\r
\t\t<Link>\r
\t\t\t<SubSystem>Windows</SubSystem>\r
\t\t\t<GenerateDebugInformation>true</GenerateDebugInformation>\r
\t\t\t<EnableCOMDATFolding>true</EnableCOMDATFolding>\r
\t\t\t<OptimizeReferences>true</OptimizeReferences>\r
\t\t</Link>\r
\t</ItemDefinitionGroup>\r
\t<Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />\r
\t<ImportGroup Label="ExtensionTargets">\r
\t</ImportGroup>\r
</Project>\r
''')

_xml_filters_header = (
'''<?xml version="1.0" encoding="utf-8"?>\r
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\r
\t<ItemGroup>\r
''')

#-------------------------------------------------------------------------------

class _ProjectGenerator(object):
    """Generates a project file for MSVS 2013"""

    def __init__(self, project_node, filters_node, source, env):
        self.project_node = project_node
        self.project_file = None
        self.filters_node = filters_node
        self.filters_file = None
        self.base_dir = os.path.dirname(str(project_node))
        self.source = sorted(source)

    def PrintHeader(self):
        self.project_file.write(_xml_vcxproj_header)
        self.filters_file.write(_xml_filters_header)

    def PrintFolders(self):
        folders = set()
        for source_dir, _ in itertools.groupby(
                self.source, key=lambda f: os.path.dirname(str(f))):
            group = winpath(os.path.relpath(source_dir,
                os.path.commonprefix([source_dir, self.base_dir])))
            while group != '':
                folders.add(group)
                group = ntpath.split(group)[0]

        for folder in sorted(folders):
            self.filters_file.writelines([
                '\t\t<Filter Include="' + folder + '">\r\n',
                '\t\t\t<UniqueIdentifier>' + _unique_id(folder) + '</UniqueIdentifier>\r\n',
                '\t\t</Filter>\r\n'])
        self.filters_file.writelines(['\t</ItemGroup>\r\n', '\t<ItemGroup>\r\n'])

    def PrintProject(self):
        self.PrintFolders()
        for source_node in self.source:
            rel = os.path.relpath(str(source_node), self.base_dir)
            include = ' Include="' + rel + '"'
            k = nodekind(source_node)
            if k == 'compile':
                if is_unity(source_node):
                    self.project_file.write ('\t\t<ClCompile' + include + ' />\r\n')
                else:
                    self.project_file.writelines ([
                        '\t\t<ClCompile' + include + '>\r\n',
                        '\t\t\t<ExcludedFromBuild>true</ExcludedFromBuild>\r\n',
                        '\t\t</ClCompile>\r\n'])
            elif k == 'include':
                self.project_file.write ('\t\t<ClInclude' + include + ' />\r\n')
            else:
                self.project_file.write ('\t\t<None' + include + ' />\r\n')

            dirname = os.path.dirname(source_node.get_abspath())
            common = os.path.commonprefix([dirname, self.base_dir])
            group = winpath(os.path.relpath(dirname, common))
            source_path = winpath(os.path.relpath(str(source_node), common))
            include = ' Include="' + source_path + ''
            filt = '\r\n      <Filter>' + group + '</Filter>\r\n'
            kind = nodekind(source_node)
            if kind == 'compile':
                self.filters_file.write ('\t\t<ClCompile' + include + '">' + filt + '\t\t</ClCompile>\r\n')
            elif kind == 'include':
                self.filters_file.write ('\t\t<ClInclude' + include + '">' + filt + '\t\t</ClInclude>\r\n')
            else:
                self.filters_file.write ('\t\t<None' + include + '">' + filt + '\t\t</None>\r\n')
        pass

    def PrintFooter(self):
        self.project_file.write(_xml_vcxproj_props)
        self.filters_file.write('\t</ItemGroup>\r\n</Project>\r\n')

    def Build(self):
        try:
            self.project_file = open(str(self.project_node), 'wb')
        except IOError, detail:
            raise SCons.Errors.InternalError('Unable to open "' +
                str(self.project_node) + '" for writing:' + str(detail))
        try:
            self.filters_file = open(str(self.filters_node), 'wb')
        except IOError, detail:
            raise SCons.Errors.InternalError('Unable to open "' +
                str(self.filters_node) + '" for writing:' + str(detail))
        self.PrintHeader()
        self.PrintProject()
        self.PrintFooter()
        self.project_file.close()
        self.filters_file.close()

#-------------------------------------------------------------------------------

class _SolutionGenerator(object):
    def __init__(self, slnfile, projfile, env):
        pass

    def Build(self):
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
    
    g = _ProjectGenerator (target[0], target[1], source, env)
    g.Build()

    if env.get('auto_build_solution', 1):
        g = _SolutionGenerator (target[2], target[0], env)
        g.Build()

def projectEmitter(target, source, env):
    if len(target) != 1:
        raise ValueError ("Exactly one target must be specified")

    # According to msvs.py sometimes this happens
    if source[0] == target[0]:
        source = []
        raise ValueError ("Unexpected source[0]==target[0]")

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
    action = buildProject,
    emitter = projectEmitter)

def generate(env):
    """Add Builders and construction variables for Microsoft Visual
    Studio project files to an Environment."""

    try:
      env['BUILDERS']['VSProject']
    except KeyError:
      env['BUILDERS']['VSProject'] = projectBuilder

def exists(env):
    return True
