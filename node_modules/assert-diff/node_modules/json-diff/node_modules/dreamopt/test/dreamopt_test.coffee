assert = require 'assert'

dreamopt = require '../lib/dreamopt'


o = (syntax, argv, expected) ->
  expected.argv ||= []

  describe "when given #{JSON.stringify(argv)}", ->
    _actual = null
    before ->
      _actual = dreamopt(syntax, argv)

    for own k, v of expected
      do (k, v) ->
        it "value of #{k} should be #{JSON.stringify(v)}", ->
          assert.deepEqual _actual[k], v, "#{k} is #{JSON.stringify(_actual[k])}, expected #{JSON.stringify(v)}, actual = #{JSON.stringify(_actual)}"

    it "should not return any other option keys", ->
      keys = {}
      for own k, v of _actual
        keys[k] = true
      for own k, v of expected
        delete keys[k]

      assert.deepEqual keys, [], "Extra keys found in expected: #{Object.keys(keys).join(', ')}, actual = #{JSON.stringify(_actual)}, expected = #{JSON.stringify(expected)}"


oo = (syntax, argv, errorRegexp) ->
  describe "when given #{JSON.stringify(argv)}", ->
    _actual = null
    _err = null
    before ->
      try
        _actual = dreamopt(syntax, argv)
      catch e
        _err = e

    it "should throw an error matching #{errorRegexp}", ->
      assert.ok !!_err, "Expected error matching #{errorRegexp}, no error thrown, actual = #{JSON.stringify(_actual)}"
      assert.ok _err.message.match(errorRegexp), "Expected error matching #{errorRegexp}, got error #{_err.message}"


ooo = (syntax, expectedUsage) ->
  it "should display correct usage info", ->
    _usage = null
    captureUsage = (usage) ->
      _usage = usage

    dreamopt syntax, { printUsage: captureUsage }, ['--help']

    assert.equal _usage.trim(), expectedUsage.trim(), "Usage mismatch, actual:\n#{_usage.trim()}\n\nExpected:\n#{expectedUsage.trim()}\n"



describe 'dreamopt', ->

  describe "with a syntax as simple as -a/--AAA, -b/--BBB COUNT, -c/--ccc", ->

    syntax = [
        "  -a, --AAA  Simple option"
        "  -b, --BBB COUNT  Option with value"
        "  -c, --[no-]ccc  Flag option"
    ]

    o syntax, [''], {}

    o syntax, ['-a'],            { AAA: true }
    o syntax, ['-b', '10'],      { BBB: 10 }
    o syntax, ['-b10'],          { BBB: 10 }
    o syntax, ['-ac'],           { AAA: true, ccc: true }
    o syntax, ['-ab', '10'],     { AAA: true, BBB: 10 }
    o syntax, ['-ab10'],         { AAA: true, BBB: 10 }

    oo syntax, ['-z'],           /Unknown short option/
    oo syntax, ['-azc'],         /Unknown short option/
    oo syntax, ['-b'],           /requires an argument/
    oo syntax, ['-ab'],          /requires an argument/
    oo syntax, ['-a', '-b'],     /requires an argument/

    o syntax, ['--AAA'],         { AAA: true }
    o syntax, ['--no-AAA'],      { AAA: false }
    o syntax, ['--ccc'],         { ccc: true }
    o syntax, ['--no-ccc'],      { ccc: false }
    o syntax, ['--BBB', '10'],   { BBB: 10 }
    o syntax, ['--BBB=10'],      { BBB: 10 }

    oo syntax, ['--zzz'],        /Unknown long option/
    oo syntax, ['--BBB'],        /requires an argument/


    ooo syntax, """
      Options:
        -a, --AAA             Simple option
        -b, --BBB COUNT       Option with value
        -c, --[no-]ccc        Flag option
        -h, --help            Display this usage information
    """


  describe "with a syntax that has two positional arguments and one option (-v/--verbose)", ->

    syntax = [
        "  -v, --verbose  Be verbose"
        "  first  First positional arg"
        "  second  Second positional arg"
    ]

    o syntax, [],                      {}
    o syntax, ['-v'],                  { verbose: true }
    o syntax, ['foo'],                 { argv: ['foo'], first: 'foo' }
    o syntax, ['foo', 'bar'],          { argv: ['foo', 'bar'], first: 'foo', second: 'bar' }
    o syntax, ['-v', 'foo'],           { argv: ['foo'], first: 'foo', verbose: true }
    o syntax, ['foo', '-v'],           { argv: ['foo'], first: 'foo', verbose: true }
    o syntax, ['-v', 'foo', 'bar'],    { argv: ['foo', 'bar'], first: 'foo', second: 'bar', verbose: true }


  describe "with a syntax that has two positional arguments, both of which have default values", ->

    syntax = [
        "  first  First positional arg (default: 10)"
        "  second  Second positional arg (default: 20)"
    ]

    o syntax, [],                      { argv: [10, 20], first: 10, second: 20 }
    o syntax, ['foo'],                 { argv: ['foo', 20], first: 'foo', second: 20 }
    o syntax, ['foo', 'bar'],          { argv: ['foo', 'bar'], first: 'foo', second: 'bar' }


  describe "with a syntax that has two positional arguments, one of which is required", ->

    syntax = [
        "  first  First positional arg  #required"
        "  second  Second positional arg (default: 20)"
    ]

    oo syntax, [],                     /required/
    o syntax, ['foo'],                 { argv: ['foo', 20], first: 'foo', second: 20 }
    o syntax, ['foo', 'bar'],          { argv: ['foo', 'bar'], first: 'foo', second: 'bar' }


  describe "with a syntax that has a required option", ->

    syntax = [
        "  --src FILE  Source file  #required"
        "  first  First positional arg"
    ]

    oo syntax, [],                     /required/
    oo syntax, ['foo'],                /required/
    oo syntax, ['--src'],              /requires an argument/
    o syntax, ['--src', 'xxx'],        { src: 'xxx' }
    o syntax, ['--src', 'xxx', 'zzz'], { src: 'xxx', first: 'zzz', argv: ['zzz'] }
    o syntax, ['zzz', '--src', 'xxx'], { src: 'xxx', first: 'zzz', argv: ['zzz'] }


  describe "with a syntax that has a list option", ->

    syntax = [
        "  --src FILE  Source file  #list"
    ]

    o syntax, [],                                 { src: [], argv: [] }
    o syntax, ['--src', 'xxx'],                   { src: ['xxx'], argv: [] }
    o syntax, ['--src', 'xxx', '--src', 'yyy'],   { src: ['xxx', 'yyy'], argv: [] }

setTimeout (->), 2000
