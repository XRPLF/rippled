# Algorithm TODO

- Rethink the CycledSet (and cousin AgedCache). Make each element part
  of an intrusive linked list, add a DiscreteClock data member, and
  perform aging on each insertion or sweep rather than once in a while.
