# Coding Standards

Coding standards used here gradually evolve and propagate through 
code reviews. Some aspects are enforced more strictly than others.

## Rules

These rules only apply to our own code. We can't enforce any sort of 
style on the external repositories and libraries we include. The best
guideline is to maintain the standards that are used in those libraries.

* Tab inserts 4 spaces. No tab characters.
* Braces are indented in the [Allman style][1].
* Modern C++ principles. No naked ```new``` or ```delete```.
* Line lengths limited to 80 characters. Exceptions limited to data and tables.

## Guidelines

If you want to do something contrary to these guidelines, understand
why you're doing it. Think, use common sense, and consider that this
your changes will probably need to be maintained long after you've
moved on to other projects.

* Use white space and blank lines to guide the eye and keep your intent clear.
* Put private data members at the top of a class, and the 6 public special
members immediately after, in the following order:
  * Destructor
  * Default constructor
  * Copy constructor
  * Copy assignment
  * Move constructor
  * Move assignment
* Don't over-inline by defining large functions within the class
declaration, not even for template classes.

## Formatting

The goal of source code formatting should always be to make things as easy to
read as possible. White space is used to guide the eye so that details are not
overlooked. Blank lines are used to separate code into "paragraphs."

* Always place a space before and after all binary operators,
  especially assignments (`operator=`).
* The `!` operator should be preceded by a space, but not followed by one.
* The `~` operator should be preceded by a space, but not followed by one.
* The `++` and `--` operators should have no spaces between the operator and
  the operand.
* A space never appears before a comma, and always appears after a comma.
* Don't put spaces after a parenthesis. A typical member function call might
  look like this: `foobar (1, 2, 3);`
* In general, leave a blank line before an `if` statement.
* In general, leave a blank line after a closing brace `}`.
* Do not place code on the same line as any opening or
  closing brace.
* Do not write `if` statements all-on-one-line. The exception to this is when
  you've got a sequence of similar `if` statements, and are aligning them all
  vertically to highlight their similarities.
* In an `if-else` statement, if you surround one half of the statement with
  braces, you also need to put braces around the other half, to match.
* When writing a pointer type, use this spacing: `SomeObject* myObject`.
  Technically, a more correct spacing would be `SomeObject *myObject`, but
  it makes more sense for the asterisk to be grouped with the type name,
  since being a pointer is part of the type, not the variable name. The only
  time that this can lead to any problems is when you're declaring multiple
  pointers of the same type in the same statement - which leads on to the next
  rule:
* When declaring multiple pointers, never do so in a single statement, e.g.
  `SomeObject* p1, *p2;` - instead, always split them out onto separate lines
  and write the type name again, to make it quite clear what's going on, and
  avoid the danger of missing out any vital asterisks.
* The previous point also applies to references, so always put the `&` next to
  the type rather than the variable, e.g. `void foo (Thing const& thing)`. And
  don't put a space on both sides of the `*` or `&` - always put a space after
  it, but never before it.
* The word `const` should be placed to the right of the thing that it modifies,
  for consistency. For example `int const` refers to an int which is const.
  `int const*` is a pointer to an int which is const. `int *const` is a const
  pointer to an int.
* Always place a space in between the template angle brackets and the type
  name. Template code is already hard enough to read!

[1]: http://en.wikipedia.org/wiki/Indent_style#Allman_style
