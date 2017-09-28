Command-Line Parser With Readable Syntax From Your Sweetest Dreams
==================================================================

Ever wrote crap code to deal with parsing command-line options? No more. Here's how it should look like (in CoffeeScript):

    options = require('dreamopt') [
      "Usage: myscript [options] source [destination]"

      "  source           Source file to compile into css #required"
      "  destination      Destination file (defaults to source file with .css extension)", (value, options) ->
        if !value
          return options.source.replace(/\.mess/, '') + '.css'

      "Processing options:"
      "  -n, --dry-run    Don't write anything to disk"
      "  -m, --mode MODE  Set execution mode: easy, medium, hard (defaults to medium)"

      "Connection options:"
      "  -p, --port PORT  Port to connect to (default: 80)"
      "  -h, --host HOST  Host to connect to (default is localhost)"

      "Getting useful information:"
      "  --reporters      Print a list of reporters and exit", ->
        console.log "Reporters: foo, bar, boz"
        process.exit 0
    ]

    console.log JSON.stringify(options, null, 2)

Try to run it as `node examples/foo.js`:

    Error: Missing required argument #1: source

Now run it as `node examples/foo.js myfile.mess`:

    {
      "mode": "medium",
      "port": 80,
      "host": "localhost",
      "source": "myfile.mess",
      "destination": "myfile.css",
      "argv": [
        "myfile.mess",
        "myfile.css"
      ]
    }


Installation
------------

    npm install dreamopt


Features
--------

Overview:

* readable option specification with no extra punctuation
* comprehensive test suite (221 tests so far)
* returns a simple hash with predicable contents and coerced data types
* mandatory and optional arguments/options
* default values
* optional callback functions
* custom tags and coercion rules
* automatic `--help`

Option syntax details:

* long options always start with double dashes (`--long`), short options with a single dash (`-s`)
* a single dash by itself (`-`) is considered a positional argument rather than an option (and usually signifies reading from stdin)
* `--` ends option processing
* option value can be specified as `-sVAL`, `-s VAL`, `--long=VAL` or `--long VAL`
* short options can be combined (`-xc`)
* short option that requires a value consumes the remainder of the combined option if any (`-xcfMYFILE`, `-pi.tmp`)


Usage
-----

This module can called with up to three arguments:

    options = require('dreamopt')(spec, [customTags], [argv])

where:

* `spec` is a required array of strings
* `customTags` is an optional hash with custom tag handlers
* `argv` is an optional array of arguments to use instead of `process.argv.slice(2)`

If you leave out `customTags` but specify `argv`, you don't need to include an empty argument: `require('dreamopt')(spec, argv)`.


Specification format
--------------------

Each line of `spec` can be:

* `Usage: blah blag` — a banner, it is displayed at the very top of the usage info
* `Something:` — a header, it is displayed verbatim with appropriate spacing; if you don't define any headers, dreamopt will add the default ones as needed (“Arguments:” and “Options:”)
* `-s, --long VALUE  Description #tag1 #tag2(val2)` — option definition; must start with at least one space; if description or tags are specified, they must be separated from the option itself by at least two spaces; tags must be in the end and may have optional values
* `arg  Description #tag1 #tag2` — positional argument definition, same format as options
* after an option or an argument, you can include a function to be invoked when the option/argument is encountered
* `command  Description` followed by an array — subcommand definition; these are not functional yet, but should be parsed properly

Any other lines that don't start with whitespace are output verbatim, as a paragraph of text. (Lines that start with whitespace must conform to option, argument or subcommand syntax.)


Coercion, validation and custom tags
------------------------------------

Argument values are automatically coerced to numbers if possible, otherwise they are provided as strings. You can specify one of the following tags to change coercion rules:

* `#string` disables coercion and always returns a string
* `#int` always coerces to int, giving an error if that's impossible

You can define custom tags to handle coercion, validation or any other processing. For example, to parse a simple YYYY-MM-DD date format, you can do:

    options = require('../lib/dreamopt') [
      "-f, --from DATE  Only process records from the given date #date"
    ], {
      date: (value, options, optionName) ->
        if isNaN(new Date(value))
          throw new Error("Invalid date for option #{optionName}")
        new Date(value)
    }

    console.log "Year: " + options.from?.getFullYear()

Tag functions are invoked with four arguments `(value, options, optionName, tagValue)`:

* `value` is the value of the current option
* `options` is the options hash built so far
* `optionName` is useful when referring to the current option in an error message
* `tagValue` is the value of the tag if any; for example, for `#date(today)` the tagValue would be `'today'`


Magic tags
----------

* `#required` marks a required option or argument
* `#var(fieldName)` overrides the options field for this option (i.e. the value is stored into `options.fieldName`)
* `#default(value)` specifies a default value
* `#list` marks an option that may be used multiple times; the final value is a JavaScript array
* `#fancydefault` forces the callback function associated with the current option to be called even when an argument is not provided and no default is set; in this case, the original value will be `null` and your function is expected to return a better one
* `#delayfunc` delays invocation of the callback function until all other options and arguments are processed; this is useful for options like `--help` or `--print-reporters`, when you want all normal options to be handled and validated before the callback is invoked; the return value of such callback functions is ignored

Additionally, you may encounter the following internal tags in the source code:

* `#flag` denotes a no-values option (which is always treated as boolean)
* `#acceptsno` is set for options which use `--[no-]something` in their definition; all boolean option accept --no-option variant to turn them off, but only options explicitly specified as such are documented as accepting --no variants in usage info


Automatic usage info
--------------------

If you don't define a `--help` option, it is provided for you automatically and prints a usage info like this:

    Usage: myscript [options] source.mess [destination.css]

    Arguments:
      source                Source file to compile into css
      destination           Destination file (defaults to source file with .css extension)

    Processing options:
      -n, --dry-run         Don't write anything to disk
      -m, --mode MODE       Set execution mode: easy, medium, hard (defaults to medium)

    Connection options:
      -p, --port PORT       Port to connect to (default: 80)
      -h, --host HOST       Host to connect to (default is localhost)

    Getting useful information:
          --reporters       Print a list of reporters and exit
      -h, --help            Display this usage information

You can provide `customTags.printUsage(usageText)` function to customize the way this usage info is printed; the default implementation outputs the argument via `console.error` and executes `process.exit(1)`.
