# Code Style Cheat Sheet

## Form

- One class per header file.
- Place each data member on its own line.
- Place each ctor-initializer on its own line.
- Create typedefs for primitive types to describe them.
- Return descriptive local variables instead of constants.
- Use long descriptive names instead of abbreviations.
- Use "explicit" for single-argument ctors
- Avoid globals especially objects with static storage duration
- Order class declarations as types, public, protected, private, then data.
- Prefer 'private' over 'protected'

## Function

- Minimize external dependencies
  * Pass options in the ctor instead of using theConfig
  * Use as few other classes as possible
