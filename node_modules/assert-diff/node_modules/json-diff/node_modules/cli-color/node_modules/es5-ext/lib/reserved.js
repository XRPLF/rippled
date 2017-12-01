'use strict';

var freeze  = Object.freeze

  , keywords, future, futureStrict, all;

// 7.6.1.1 Keywords
keywords = freeze(['break', 'case', 'catch', 'continue', 'debugger', 'default', 'delete', 'do',
	'else', 'finally', 'for', 'function', 'if', 'in', 'instanceof', 'new',
	'return', 'switch', 'this', 'throw', 'try', 'typeof', 'var', 'void', 'while',
	'with']);

// 7.6.1.2 Future Reserved Words
future = freeze(['class', 'const', 'enum', 'exports', 'extends', 'import', 'super'])

// Future Reserved Words (only in strict mode)
futureStrict = freeze(['implements', 'interface', 'let', 'package', 'private', 'protected', 'public',
	'static', 'yield']);

all = module.exports = keywords.concat(future, futureStrict);
all.keywords = keywords;
all.future = future;
all.futureStrict = futureStrict;
freeze(all);
