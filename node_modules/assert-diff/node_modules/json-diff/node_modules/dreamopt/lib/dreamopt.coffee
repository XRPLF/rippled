wordwrap = require 'wordwrap'

USAGE    = /^Usage:/
HEADER   = /^[^-].*:$/
OPTION   = /^\s+-/
COMMAND  = ///^  \s+  (\w+)  (?: \s{2,} (\S.*) )  $///
ARGUMENT = /// ^ \s+ .* \s\s | ^ \s+ \S+ $ ///
TEXT     = /^\S/

# if only JavaScript had a sane split(), we'd skip some of these
OPTION_DESC         = ///^ (.*?)  \s{2,}  (.*) $///
OPTION_METAVARS     = ///^ ([^\s,]+ (?:,\s* \S+)? )  \s+  ([^,].*) $///
OPTION_SHORT        = ///^ (-\S)  (?: , \s* (.*) )? $///
OPTION_LONG         = ///^ (--\S+) $///
OPTION_BOOL         = ///^ --\[no-\](.*) $///
OPTION_DESC_TAG     = ///^ (.*) \#(\w+) (?: \(  ([^()]*)  \) )? \s* $///
DUMMY               = /// \# ///   # make Sublime Text syntax highlighting happy
OPTION_DESC_DEFAULT = ///  \(  (?: default: | default\s+is | defaults\s+to )  \s+  ([^()]+)  \)  ///i


DefaultHandlers =
  auto: (value) ->
    return value unless typeof value is 'string'
    return Number(value) if not isNaN(Number(value))
    return value

  string: (value) -> value

  int: (value) ->
    return value unless typeof value is 'string'
    if isNaN(parseInt(value, 10))
      throw new Error("Integer value required: #{value}")
    return parseInt(value, 10)

  flag: (value, options, optionName, tagValue) ->
    return yes   if !value?
    return value if typeof value isnt 'string'
    return no    if value.toLowerCase() in ['0', 'false', 'no', 'off']
    return yes   if value.toLowerCase() in ['', '1', 'true', 'yes', 'on']
    throw new Error("Invalid flag value #{JSON.stringify(value)} for option #{optionName}")


alignment   = 24
indent      = "  "
separator   = "  "
width       = 100

wrapText        = require('wordwrap')(width)


formatUsageString = (left, right) ->
  overhead = indent.length + separator.length

  if left.length < alignment - overhead
    padding = new Array(alignment - overhead - left.length + 1).join(' ')
  else
    padding = ''

  actualAlignment   = overhead + left.length + padding.length
  descriptionWidth  = width - actualAlignment
  wrappedLineIndent = new Array(actualAlignment + 1).join(' ')

  [firstLine, otherLines...] = wordwrap(descriptionWidth)(right).trim().split('\n')
  right = [firstLine].concat(otherLines.map (line) -> wrappedLineIndent + line).join("\n")
  right += "\n" if otherLines.length

  return "  #{left}#{padding}  #{right}"


class Option
  constructor: (@shortOpt, @longOpt, @desc, tagPairs, @metavars, @defaultValue) ->
    if @longOpt || @shortOpt
      @name = @longOpt && @longOpt.slice(2) || @shortOpt.slice(1)
    else if @metavars.length
      @name = @metavars[0]
      if $ = @name.match ///^ \[ (.*) \] $///
        @name = $[1]

    @var = @name

    @tags = {}
    @tagsOrder = []
    for [tag, value] in tagPairs
      @tags[tag] = value
      @tagsOrder.push tag

      switch tag
        when 'default' then @defaultValue = value
        when 'var'     then @var = value

    @func = null


  leftUsageComponent: ->
    longOpt = @longOpt
    if longOpt && @tags.acceptsno
      longOpt = "--[no-]" + longOpt.slice(2)

    string = switch
      when @shortOpt and longOpt then "#{@shortOpt}, #{longOpt}"
      when @shortOpt             then @shortOpt
      when @longOpt              then "    #{longOpt}"
      else ''

    if @metavars
      string = string + (string && ' ' || '') + @metavars.join(' ')

    return string


  toUsageString: -> formatUsageString(@leftUsageComponent(), @desc)


  coerce: (value, options, syntax) ->
    any = no
    for tag in @tagsOrder
      if handler = (syntax.handlers[tag] || DefaultHandlers[tag])
        newValue = handler(value, options, @leftUsageComponent(), @tags[tag])
        unless typeof newValue is undefined
          value = newValue
        any = yes
    unless any
      value = DefaultHandlers.auto(value, options, syntax, @leftUsageComponent())
    return value


class Command
  constructor: (@name, @desc, @syntax) ->
    @func = null

  leftUsageComponent: -> @name

  toUsageString: -> formatUsageString(@leftUsageComponent(), @desc)


class Syntax
  constructor: (@handlers, specs=[]) ->
    @usage     = []

    @options   = []
    @arguments = []
    @commands  = {}
    @commandsOrder = []

    @shortOptions = {}
    @longOptions  = {}

    @usageFound = no
    @headerAdded = no

    @implicitHeaders =
      options:   "Options:"
      arguments: "Arguments:"
      commands:  "Commands:"

    @lastSectionType = 'none'
    @customHeaderAdded = no

    if specs
      @add(specs)


  addHeader: (header) ->
    @usage.push "\n#{header}"
    @lastSectionType = 'any'

  ensureHeaderExists: (sectionType) ->
    if @lastSectionType is 'any'
      @lastSectionType = sectionType
    else if @lastSectionType != sectionType
      @addHeader @implicitHeaders[sectionType]
      @lastSectionType = sectionType


  add: (specs) ->
    unless typeof specs is 'object'
      specs = [specs]
    specs = specs.slice(0)

    gotArray    = -> (typeof specs[0] is 'object') and (specs[0] instanceof Array)
    gotFunction = -> typeof specs[0] is 'function'

    while spec = specs.shift()
      if typeof spec != 'string'
        throw new Error("Expected string spec, found #{typeof spec}")

      if spec.match(HEADER)
        @addHeader spec

      else if spec.match(USAGE)
        @usage.unshift "#{spec}"
        @usageFound = yes

      else if spec.match(OPTION)
        @options.push (option = Option.parse(spec.trim()))

        @shortOptions[option.shortOpt.slice(1)] = option  if option.shortOpt
        @longOptions[option.longOpt.slice(2)]   = option  if option.longOpt

        if gotFunction()
          option.func = specs.shift()

        @ensureHeaderExists 'options'
        @usage.push option.toUsageString()

      else if !gotArray() and spec.match(ARGUMENT)
        @arguments.push (option = Option.parse(spec.trim()))

        if gotFunction()
          option.func = specs.shift()

        @ensureHeaderExists 'arguments'
        @usage.push option.toUsageString()

      else if $ = spec.match COMMAND
        [name, desc] = $

        unless gotArray()
          throw new Error("Array must follow a command spec: #{JSON.stringify(spec)}")
        subsyntax = new Syntax(@handlers, specs.shift())

        @commands[name] = command = new Command(name, desc, subsyntax)
        @commandsOrder.push name

        @ensureHeaderExists 'commands'
        @usage.push command.toUsageString()

      else if spec.match TEXT
        @usage.push "\n" + wrapText(spec.trim())

      else
        throw new Error("String spec invalid: #{JSON.stringify(spec)}")

    return this


  toUsageString: -> (line + "\n" for line in @usage).join('')


  parse: (argv) ->
    argv = argv.slice(0)

    result     = {}
    positional = []
    funcs      = []

    executeHook = (option, value) =>
      if option.func
        if option.tags.delayfunc
          funcs.push [option.func, option, value]
        else
          newValue = option.func(value, result, this, option)
          if newValue?
            value = newValue
      return value

    processOption = (result, arg, option, value) =>
      switch option.metavars.length
        when 0
          value = true
        when 1
          value ?= argv.shift()
          if typeof value is 'undefined'
            throw new Error("Option #{arg} requires an argument: #{option.leftUsageComponent()}")
        else
          value = []
          for metavar, index in option.metavars
            value.push (subvalue = argv.shift())
            if typeof subvalue is 'undefined'
              throw new Error("Option #{arg} requires #{option.metavars.length} arguments: #{option.leftUsageComponent()}")
      return option.coerce(value, result, this)

    assignValue = (result, option, value) =>
      if option.tags.list
        if not result.hasOwnProperty(option.var)
          result[option.var] = []
        if value?
          result[option.var].push(value)
      else
        result[option.var] = value

    while arg = argv.shift()
      if arg is '--'
        while arg = argv.shift()
          positional.push arg
      else if arg is '-'
        positional.push arg
      else if arg.match(/^--no-/) && (option = @longOptions[arg.slice(5)]) && option.tags.flag
        assignValue result, option, false
      else if $ = arg.match(///^  --  ([^=]+)  (?: = (.*) )?  $///)
        [_, name, value] = $
        if option = @longOptions[name]
          value = processOption(result, arg, option, value)
          value = executeHook(option, value)
          assignValue result, option, value
        else
          throw new Error("Unknown long option: #{arg}")
      else if arg.match /^-/
        remainder = arg.slice(1)
        while remainder
          subarg    = remainder[0]
          remainder = remainder.slice(1)

          if option = @shortOptions[subarg]
            if remainder && option.metavars.length > 0
              value = remainder
              remainder = ''
            else
              value = undefined
            value = processOption(result, arg, option, value)
            value = executeHook(option, value)
            assignValue result, option, value
          else
            if arg == "-#{subarg}"
              throw new Error("Unknown short option #{arg}")
            else
              throw new Error("Unknown short option -#{subarg} in #{arg}")
      else
        positional.push arg

    for option in @options
      if !result.hasOwnProperty(option.var)
        if option.tags.required
          throw new Error("Missing required option: #{option.leftUsageComponent()}")
        if option.defaultValue? or option.tags.fancydefault or option.tags.list
          if option.defaultValue?
            value = option.coerce(option.defaultValue, result, this)
          else
            value = null
          value = executeHook(option, value)
          assignValue result, option, value

    for arg, index in positional
      if option = @arguments[index]
        value = option.coerce(arg, result, this)
        value = executeHook(option, value)
        positional[index] = value
        if option.var
          assignValue result, option, value

    for option, index in @arguments
      if index >= positional.length
        if option.tags.required
          throw new Error("Missing required argument \##{index + 1}: #{option.leftUsageComponent()}")
        if option.defaultValue? or option.tags.fancydefault
          if option.defaultValue?
            value = option.coerce(option.defaultValue, result, this)
          else
            value = null
          value = executeHook(option, value)

          if option.var
            assignValue result, option, value

          if index == positional.length
            positional.push value
          else if !option.var && !option.func
            throw new Error("Cannot apply default value to argument \##{index + 1} (#{option.leftUsageComponent()}) because no #var is specified, no func is provided and previous arguments don't have default values")

    result.argv = positional

    for [func, option, value] in funcs
      func(value, result, this, option)

    return result


Option.parse = (spec) ->
  isOption = (' ' + spec).match(OPTION)

  [_, options, desc]     = spec.match(OPTION_DESC)     || [undefined, spec, ""]
  if isOption
    [_, options, metavars] = options.match(OPTION_METAVARS) || [undefined, options, ""]
    [_, shortOpt, options] = options.match(OPTION_SHORT) || [undefined, "", options]
    [_, longOpt, options]  = (options || '').match(OPTION_LONG)  || [undefined, "", options]
  else
    [metavars, options] = [options, ""]
  metavars = metavars && metavars.split(/\s+/) || []

  tags = (([_, desc, tag, value] = $; [tag, value ? true]) while $ = desc.match(OPTION_DESC_TAG))
  tags.reverse()

  if longOpt && longOpt.match(OPTION_BOOL)
    tags.push ['acceptsno', true]
    longOpt = longOpt.replace('--[no-]', '--')

  if isOption && metavars.length == 0
    tags.push ['flag', true]

  if $ = desc.match(OPTION_DESC_DEFAULT)
    defaultValue = $[1]
    if defaultValue.match(/\s/)
      # the default is too fancy, don't use it verbatim, but call the user callback if any to obtain the default
      defaultValue = undefined
      tags.push ['fancydefault', true]


  if options
    throw new Error("Invalid option spec format (cannot parse #{JSON.stringify(options)}): #{JSON.stringify(spec)}")
  if isOption && !(shortOpt || longOpt)
    throw new Error("Invalid option spec format !(shortOpt || longOpt): #{JSON.stringify(spec)}")

  new Option(shortOpt || null, longOpt || null, desc.trim(), tags, metavars, defaultValue)


printUsage = (usage) ->
  console.error(usage)
  process.exit 1


handleUsage = (printUsage, value, options, syntax) ->
  printUsage syntax.toUsageString()


parse = (specs, handlers, argv) ->
  if !argv? and (handlers instanceof Array)
    argv = handlers
    handlers = {}
  handlers ?= {}
  argv ?= process.argv.slice(2)

  syntax = new Syntax(handlers, specs)
  unless syntax.longOptions.help
    syntax.add ["  -h, --help  Display this usage information", (v, o, s) -> handleUsage(handlers.printUsage ? printUsage, v, o, s)]

  syntax.parse(argv)


module.exports = parse

# for testing
module.exports.parseOptionSpec = Option.parse
module.exports.Syntax = Syntax
