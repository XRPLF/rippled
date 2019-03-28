In this directory are two scripts, `build.sh` and `test.sh` used for building
and testing rippled.

(For now, they assume Bash and Linux. Once I get Windows containers for
testing, I'll try them there, but if Bash is not available, then they will
soon be joined by PowerShell scripts `build.ps` and `test.ps`.)

We don't want these scripts to require arcane invocations that can only be
pieced together from within a CI configuration. We want something that humans
can easily invoke, read, and understand, for when we eventually have to test
and debug them interactively. That means:

(1) They should work with no arguments.
(2) They should document their arguments.
(3) They should expand short arguments into long arguments.

While we want to provide options for common use cases, we don't need to offer
the kitchen sink. We can rightfully expect users with esoteric, complicated
needs to write their own scripts.

To make argument-handling easy for us, the implementers, we can just take all
arguments from environment variables. They have the nice advantage that every
command-line uses named arguments. For the benefit of us and our users, we
document those variables at the top of each script.
