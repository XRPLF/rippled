# `Hook.cpp` — HookImpl Pure Virtual Destructor

`Hook.cpp` is a minimal translation unit within the `beast::insight` metrics subsystem, containing exactly one definition: the out-of-line destructor body for `HookImpl`.

`HookImpl` is the abstract base class for polled-collection hooks — callbacks invoked at each metrics collection interval. It is declared in `HookImpl.h` with a pure virtual destructor (`virtual ~HookImpl() = 0`) and a `HandlerType` alias (`std::function<void(void)>`) that concrete subclasses use to store the user-supplied callback.

The reason this file exists at all is a C++ subtlety: even though the destructor is declared pure virtual (forcing subclasses to override it), the base destructor is always invoked as part of the destruction chain, so it must have a definition. Placing `= default` here in the `.cpp` rather than inline in the header keeps the vtable and destructor body anchored to a single translation unit, which is the idiomatic pattern for abstract base classes.

Concrete implementations — `StatsDHookImpl` in `StatsDCollector.cpp` and `NullHookImpl` in `NullCollector.cpp` — inherit from `HookImpl` and register themselves with the collector's polling loop. The user-facing `Hook` class in `Hook.h` is simply a `shared_ptr<HookImpl>` wrapper; lifetime of the hook registration is tied directly to the lifetime of that shared pointer.