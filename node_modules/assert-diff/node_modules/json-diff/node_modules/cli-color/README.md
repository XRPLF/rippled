# cli-color - Yet another colors and formatting for the console solution

Colors, formatting and other goodies for the console. This package won't mess with built-ins and provides neat way to predefine formatting patterns, see below.

## Installation

	$ npm install cli-color

## Usage

Usage:

	var clc = require('cli-color');

Output colored text:

	console.log(clc.red('Text in red'));

Styles can be mixed:

	console.log(clc.red.bgWhite.underline('Underlined red text on white background.'));

Styled text can be mixed with unstyled:

	console.log(clc.red('red') + ' plain ' + clc.blue('blue'));

__Best way is to predefine needed stylings and then use it__:

	var error = clc.red.bold;
	var warn = clc.yellow;
	var notice = clc.blue;

	console.log(error('Error!'));
	console.log(warn('Warning'));
	console.log(notice('Notice'));


Supported are all ANSI colors and styles:

#### Styles

Styles will display correctly if font used in your console supports them.

* bold
* italic
* underline
* inverse
* strike

#### Foreground colors

* black
* red
* green
* yellow
* blue
* magenta
* cyan
* white
* gray (technically bright.black)

#### Background colors

* bgBlack
* bgRed
* bgGreen
* bgYellow
* bgBlue
* bgMagenta
* bgCyan
* bgWhite

#### Colors intensity

For _bright_ color variants:

* bright (foreground)
* bgBright (background)

### Additional functions:

#### trim(formatedText)

Trims ANSI formatted string to plain text

	var ansiTrim = require('cli-color/lib/trim');

	var plain = ansiTrim(formatted);

#### throbber(interval[, format])

Displays throbber on given interval.
Interval should be [clock.interval](https://github.com/medikoo/clock) object
Optionally throbber output can be formatted with given format

	var interval = require('clock/lib/interval')
	  , ansiThrobber = require('cli-color/lib/throbber');

	var i = interval(200, true);

	// Display throbber while interval is ticking
	ansiThrobber(i);

	// at any time you can stop/start interval
	// When interval is stopped throbber doesn't show
	i.stop();

## Tests [![Build Status](https://secure.travis-ci.org/medikoo/cli-color.png?branch=master)](https://secure.travis-ci.org/medikoo/cli-color)

	$ npm test
