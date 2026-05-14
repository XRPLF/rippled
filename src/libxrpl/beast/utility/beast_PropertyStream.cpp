/** @file
 *  Implements the `beast::PropertyStream` family: hierarchical diagnostic
 *  property emission for XRPL subsystems.
 *
 *  Subsystems such as `LedgerCleaner`, `PeerFinder`, `OverlayImpl`, and
 *  `ResourceManager` inherit from `PropertyStream::Source` and override
 *  `onWrite()` to expose their internal state as a named tree that can be
 *  serialised to JSON or other formats by a concrete `PropertyStream` backend.
 *
 *  The three RAII types (`Map`, `Set`, `Proxy`) enforce correct open/close
 *  bracketing through their constructors and destructors — no explicit close
 *  calls are needed at the call site.
 */

#include <xrpl/beast/core/List.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <algorithm>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>

namespace beast {

// --- Item ---

/** Constructs an intrusive list node that back-references the owning Source.
 *
 *  Each `Source` embeds exactly one `Item item_` to link itself into a
 *  parent's `children_` list without requiring a separate heap allocation.
 *
 *  @param source Pointer to the `Source` that owns this node; must not be
 *      null for the lifetime of the `Item`.
 */
PropertyStream::Item::Item(Source* source) : source_(source)
{
}

/** Returns the `Source` that owns this intrusive list node.
 *
 *  @return A reference to the owning `Source` object.
 */
PropertyStream::Source&
PropertyStream::Item::source() const
{
    return *source_;
}

/** Returns a pointer to the owning `Source`, enabling member access via `->`.
 *
 *  @return Pointer to the owning `Source` object.
 */
PropertyStream::Source*
PropertyStream::Item::operator->() const
{
    return &source();
}

/** Returns a reference to the owning `Source` via dereference operator.
 *
 *  @return Reference to the owning `Source` object.
 */
PropertyStream::Source&
PropertyStream::Item::operator*() const
{
    return source();
}

// --- Proxy ---

/** Constructs a Proxy that defers writing a key-value entry until destruction.
 *
 *  The value is accumulated in an internal `std::ostringstream`; it is flushed
 *  to the parent `Map` only when the `Proxy` is destroyed and the stream is
 *  non-empty.  This means `map["foo"] = 42` and `map["foo"] << "bar"` both
 *  work uniformly and produce no entry if nothing is written.
 *
 *  @param map The parent `Map` that will receive the entry on destruction.
 *  @param key The key string for the emitted entry.
 */
PropertyStream::Proxy::Proxy(Map const& map, std::string key) : map_(&map), key_(std::move(key))
{
}

/** Copy constructor; produces an independent proxy sharing the same parent map
 *  and key but with its own fresh output stream.
 *
 *  @param other The proxy to copy key and map reference from.
 */
PropertyStream::Proxy::Proxy(Proxy const& other) : map_(other.map_), key_(other.key_)
{
}

/** Flushes the accumulated output to the parent map.
 *
 *  If the internal stream is non-empty the entry `(key_, stream_contents)` is
 *  committed to the parent `Map`.  If nothing was written to the proxy, no
 *  entry is emitted — the deferred-commit pattern ensures zero-overhead for
 *  conditionally-populated fields.
 */
PropertyStream::Proxy::~Proxy()
{
    std::string const s(ostream_.str());
    if (!s.empty())
        map_->add(key_, s);
}

/** Forwards a stream manipulator (e.g., `std::endl`) to the internal stream.
 *
 *  @param manip A stream manipulator function.
 *  @return The internal `std::ostream` after applying the manipulator.
 */
std::ostream&
PropertyStream::Proxy::operator<<(std::ostream& manip(std::ostream&)) const
{
    return ostream_ << manip;
}

// --- Map ---

/** Wraps an existing stream as a root map without emitting a `mapBegin`.
 *
 *  Used when the caller has already opened the enclosing scope on the stream
 *  and just needs a `Map` handle to forward key-value writes.  `mapEnd()` is
 *  still called on destruction.
 *
 *  @param stream The underlying `PropertyStream` to write to.
 */
PropertyStream::Map::Map(PropertyStream& stream) : stream_(stream)
{
}

/** Opens an anonymous (unnamed) map inside a `Set`.
 *
 *  Calls `mapBegin()` (no key) so the stream records the start of a new
 *  map element within the enclosing array scope.  `mapEnd()` is called on
 *  destruction.
 *
 *  @param parent The enclosing `Set` whose stream this map writes to.
 */
PropertyStream::Map::Map(Set& parent) : stream_(parent.stream())
{
    stream_.mapBegin();
}

/** Opens a keyed sub-map nested inside a parent `Map`.
 *
 *  Calls `mapBegin(key)` so the stream records the start of a named nested
 *  map.  `mapEnd()` is called on destruction, closing the scope.
 *
 *  @param key  The name of this sub-map within the parent.
 *  @param map  The parent `Map` whose stream is used.
 */
PropertyStream::Map::Map(std::string const& key, Map& map) : stream_(map.stream())
{
    stream_.mapBegin(key);
}

/** Opens a keyed root-level map directly on a stream.
 *
 *  Calls `mapBegin(key)` to begin a named map scope.  `mapEnd()` is called on
 *  destruction.  Prefer this form when constructing the outermost named scope
 *  in `writeOne()` / `write()`.
 *
 *  @param key    The name of this map.
 *  @param stream The `PropertyStream` to write to.
 */
PropertyStream::Map::Map(std::string const& key, PropertyStream& stream) : stream_(stream)
{
    stream_.mapBegin(key);
}

/** Closes the map scope by calling `mapEnd()` on the underlying stream. */
PropertyStream::Map::~Map()
{
    stream_.mapEnd();
}

/** Returns a mutable reference to the underlying stream. */
PropertyStream&
PropertyStream::Map::stream()
{
    return stream_;
}

/** Returns a const reference to the underlying stream. */
PropertyStream const&
PropertyStream::Map::stream() const
{
    return stream_;
}

/** Returns a `Proxy` that will write a key-value entry to this map on
 *  destruction.
 *
 *  The returned `Proxy` accumulates output via `<<` or `=` and flushes it
 *  only if non-empty, so conditional fields produce no output when unused.
 *
 *  @param key The key for the map entry.
 *  @return A `Proxy` bound to this map and `key`.
 */
PropertyStream::Proxy
PropertyStream::Map::operator[](std::string const& key)
{
    return Proxy(*this, key);
}

// --- Set ---

/** Opens a named array scope inside a parent `Map`.
 *
 *  Calls `arrayBegin(key)` to begin the array scope; `arrayEnd()` is called
 *  on destruction.  Nested `Map` objects created with `Map(Set&)` produce
 *  anonymous elements within this array.
 *
 *  @param key  The name of this array within the parent map.
 *  @param map  The parent `Map` whose stream is used.
 */
PropertyStream::Set::Set(std::string const& key, Map& map) : stream_(map.stream())
{
    stream_.arrayBegin(key);
}

/** Opens a named array scope directly on a stream.
 *
 *  Calls `arrayBegin(key)` to begin the array scope; `arrayEnd()` is called
 *  on destruction.
 *
 *  @param key    The name of this array.
 *  @param stream The `PropertyStream` to write to.
 */
PropertyStream::Set::Set(std::string const& key, PropertyStream& stream) : stream_(stream)
{
    stream_.arrayBegin(key);
}

/** Closes the array scope by calling `arrayEnd()` on the underlying stream. */
PropertyStream::Set::~Set()
{
    stream_.arrayEnd();
}

/** Returns a mutable reference to the underlying stream. */
PropertyStream&
PropertyStream::Set::stream()
{
    return stream_;
}

/** Returns a const reference to the underlying stream. */
PropertyStream const&
PropertyStream::Set::stream() const
{
    return stream_;
}

// --- Source ---

/** Constructs a named diagnostic source with no parent or children.
 *
 *  The embedded `item_` node is initialised to point back to `this`, making
 *  the `Source` ready to be inserted into a parent's intrusive `children_`
 *  list via `add()`.
 *
 *  @param name Human-readable identifier for this source in the property tree.
 */
PropertyStream::Source::Source(std::string name) : name_(std::move(name)), item_(this)
{
}

/** Detaches this source from its parent and removes all children.
 *
 *  Cleanup order matters: the source is first removed from its parent (clearing
 *  the parent's reference to this node), then all children are detached (clearing
 *  their `parent_` pointers back to this source).  This order prevents dangling
 *  pointers in either direction.
 *
 *  `lock_` is a `std::recursive_mutex` because `removeAll()` calls `remove()`
 *  while the destructor already holds the lock — re-entrancy is required.
 */
PropertyStream::Source::~Source()
{
    std::scoped_lock const _(lock_);
    if (parent_ != nullptr)
        parent_->remove(*this);
    removeAll();
}

std::string const&
PropertyStream::Source::name() const
{
    return name_;
}

/** Attaches `source` as a direct child of this source.
 *
 *  Both `lock_` and `source.lock_` are acquired simultaneously via
 *  `std::scoped_lock` (which uses `std::lock` internally) to prevent
 *  deadlock when two threads concurrently try to cross-link sources.
 *  Sequential lock acquisition would risk ABBA deadlock.
 *
 *  @param source The child source to attach.  Must not already have a parent
 *      (`source.parent_ == nullptr` is asserted).
 */
void
PropertyStream::Source::add(Source& source)
{
    std::scoped_lock const lock(lock_, source.lock_);

    XRPL_ASSERT(
        source.parent_ == nullptr, "beast::PropertyStream::Source::add : null source parent");
    children_.pushBack(source.item_);
    source.parent_ = this;
}

/** Detaches `child` from this source's children list.
 *
 *  Both locks are acquired simultaneously for the same deadlock-avoidance
 *  reason as `add()`.  Asserts that `child.parent_` is indeed `this` before
 *  removal.
 *
 *  @param child The direct child to remove.
 */
void
PropertyStream::Source::remove(Source& child)
{
    std::scoped_lock const lock(lock_, child.lock_);

    XRPL_ASSERT(
        child.parent_ == this, "beast::PropertyStream::Source::remove : child parent match");
    children_.erase(children_.iteratorTo(child.item_));
    child.parent_ = nullptr;
}

/** Detaches all current children from this source.
 *
 *  Iterates the children list under `lock_`, acquiring each child's lock
 *  individually before calling `remove()`.  Safe to call from the destructor
 *  while `lock_` is already held because `lock_` is a `std::recursive_mutex`.
 */
void
PropertyStream::Source::removeAll()
{
    std::scoped_lock const _(lock_);
    for (auto iter = children_.begin(); iter != children_.end();)
    {
        std::scoped_lock const cl((*iter)->lock_);
        remove(*(*iter));
    }
}

//------------------------------------------------------------------------------

/** Writes only this source (not its children) to `stream`.
 *
 *  Opens a named `Map` scope for `name_`, calls `onWrite()` to let the
 *  subclass populate it, then closes the scope via `Map`'s destructor.
 *  Child sources are not traversed.
 *
 *  @param stream The destination `PropertyStream`.
 */
void
PropertyStream::Source::writeOne(PropertyStream& stream)
{
    Map map(name_, stream);
    onWrite(map);
}

/** Writes this source and all descendants recursively to `stream`.
 *
 *  Calls `onWrite()` first (not under `lock_`), then acquires `lock_` only
 *  while iterating `children_`.  Subclasses must protect their own internal
 *  state inside `onWrite()` — tree topology is protected but diagnostic
 *  emission is not globally serialised.
 *
 *  @param stream The destination `PropertyStream`.
 */
void
PropertyStream::Source::write(PropertyStream& stream)
{
    Map map(name_, stream);
    onWrite(map);

    std::scoped_lock const _(lock_);

    for (auto& child : children_)
        child.source().write(stream);
}

/** Resolves `path` and writes the matched source (and optional subtree).
 *
 *  Delegates to `find(path)` to locate the target source; if none is found
 *  the call is a no-op.  When the path ends with `/*`, `write()` (recursive)
 *  is used; otherwise `writeOne()` (this source only) is used.
 *
 *  @param stream The destination `PropertyStream`.
 *  @param path   Slash-delimited path; see `find()` for syntax.
 */
void
PropertyStream::Source::write(PropertyStream& stream, std::string const& path)
{
    std::pair<Source*, bool> result(find(path));

    if (result.first == nullptr)
        return;

    if (result.second)
    {
        result.first->write(stream);
    }
    else
    {
        result.first->writeOne(stream);
    }
}

/** Parses a slash-delimited path and returns the matching source.
 *
 *  Path syntax (verified by unit tests in `beast_PropertyStream_test.cpp`):
 *  - `""` or `"*"` — return this source (wildcard flag set for `*`).
 *  - `"name"` — depth-first search from this source for the first node
 *    named `name` (`findOneDeep`).
 *  - `"/name/child"` — rooted: start at `this` and walk `name/child` as
 *    direct-child hops (`findPath`).
 *  - Trailing `/*` sets the wildcard flag, which causes `write()` to recurse
 *    into children instead of calling `writeOne()`.
 *
 *  @param path The path string (consumed/mutated internally by helpers).
 *  @return A pair `(source*, deep)` where `source*` is `nullptr` if not found
 *      and `deep` is `true` when the wildcard suffix was present.
 */
std::pair<PropertyStream::Source*, bool>
PropertyStream::Source::find(std::string path)
{
    bool const deep(peelTrailingSlashstar(&path));
    bool const rooted(peelLeadingSlash(&path));
    Source* source(this);
    if (!path.empty())
    {
        if (!rooted)
        {
            std::string const name(peelName(&path));
            source = findOneDeep(name);
            if (source == nullptr)
                return std::make_pair(nullptr, deep);
        }
        source = source->findPath(path);
    }
    return std::make_pair(source, deep);
}

/** Strips a leading `'/'` from `*path` and reports whether one was found.
 *
 *  A leading slash signals a rooted path: traversal begins at the current
 *  source rather than performing a depth-first search for the first component.
 *
 *  @param path In/out pointer to the path string; modified in-place.
 *  @return `true` if a leading `'/'` was present and removed, `false` otherwise.
 */
bool
PropertyStream::Source::peelLeadingSlash(std::string* path)
{
    if (!path->empty() && path->front() == '/')
    {
        *path = std::string(path->begin() + 1, path->end());
        return true;
    }
    return false;
}

/** Strips a trailing `"/*"` (or bare `'*'`) from `*path` and reports
 *  whether the wildcard was found.
 *
 *  A trailing `/*` signals that the matched source's subtree should be
 *  written recursively (i.e., `write()` rather than `writeOne()`).  The
 *  preceding `'/'` is also stripped if present so the remainder is a clean
 *  path without a dangling separator.
 *
 *  @param path In/out pointer to the path string; modified in-place.
 *  @return `true` if a trailing wildcard `'*'` was found and removed.
 */
bool
PropertyStream::Source::peelTrailingSlashstar(std::string* path)
{
    bool found(false);
    if (path->empty())
        return false;
    if (path->back() == '*')
    {
        found = true;
        path->pop_back();
    }
    if (!path->empty() && path->back() == '/')
        path->pop_back();
    return found;
}

/** Extracts and returns the first path component (up to the next `'/'`),
 *  leaving the remainder in `*path`.
 *
 *  For example, `"foo/bar/baz"` becomes `"bar/baz"` in `*path` and returns
 *  `"foo"`.  An empty input returns `""` with no modification.
 *
 *  @param path In/out pointer to the path string; modified in-place.
 *  @return The first path component, or `""` if `*path` was empty.
 */
std::string
PropertyStream::Source::peelName(std::string* path)
{
    if (path->empty())
        return "";

    std::string::const_iterator const first = (*path).begin();
    std::string::const_iterator const last = (*path).end();
    std::string::const_iterator const pos = std::find(first, last, '/');
    std::string s(first, pos);

    if (pos != last)
    {
        *path = std::string(pos + 1, last);
    }
    else
    {
        *path = std::string();
    }

    return s;
}

/** Recursively searches this subtree for the first source named `name`.
 *
 *  Checks immediate children first via `findOne()`, then recurses into each
 *  child's subtree.  Returns the first match found in a depth-first traversal.
 *
 *  @param name The source name to search for.
 *  @return Pointer to the matching `Source`, or `nullptr` if none found.
 */
PropertyStream::Source*
PropertyStream::Source::findOneDeep(std::string const& name)
{
    Source* found = findOne(name);  // NOLINT TODO
    if (found != nullptr)
        return found;

    std::scoped_lock const _(lock_);
    for (auto& s : children_)
    {
        found = s.source().findOneDeep(name);
        if (found != nullptr)
            return found;
    }
    return nullptr;
}

/** Walks a slash-separated path of direct-child names starting from `this`.
 *
 *  Each call to `peelName()` consumes one path component, and `findOne()` is
 *  used at each hop — so every component must be an immediate child of the
 *  previous source.  Returns `nullptr` as soon as any component is not found.
 *
 *  @param path The remaining path to traverse (consumed in-place).
 *  @return The source at the end of the path, or `nullptr` if any hop fails.
 */
PropertyStream::Source*
PropertyStream::Source::findPath(std::string path)
{
    if (path.empty())
        return this;
    Source* source(this);
    do
    {
        std::string const name(peelName(&path));
        if (name.empty())
            break;
        source = source->findOne(name);
    } while (source != nullptr);
    return source;
}

/** Returns the first immediate child whose name matches `name`.
 *
 *  Only direct children are examined — the search does not descend further.
 *  Use `findOneDeep()` for a full subtree search.
 *
 *  @param name The name to match against immediate children.
 *  @return Pointer to the matching child, or `nullptr` if none match.
 */
PropertyStream::Source*
PropertyStream::Source::findOne(std::string const& name)
{
    std::scoped_lock const _(lock_);
    for (auto& s : children_)
    {
        if (s.source().name_ == name)
            return &s.source();
    }
    return nullptr;
}

/** Default no-op implementation of the diagnostic write hook.
 *
 *  Subclasses override this to populate the provided `Map` with their
 *  internal state (counters, status flags, configuration values, etc.).
 *  The default does nothing, so a source with no override emits an empty
 *  named map.
 */
void
PropertyStream::Source::onWrite(Map&)
{
}

// --- PropertyStream type-dispatching add() overloads ---

/** Emits a boolean key-value entry as the strings `"true"` or `"false"`.
 *
 *  Overridden rather than delegating to `lexicalAdd` because `operator<<`
 *  on `bool` produces `"1"` / `"0"`, which is inconsistent with the string
 *  representation expected in XRPL diagnostic output.
 *
 *  @param key   The entry key.
 *  @param value The boolean value to emit.
 */
void
PropertyStream::add(std::string const& key, bool value)
{
    if (value)
    {
        add(key, "true");
    }
    else
    {
        add(key, "false");
    }
}

/** @param key   The entry key.
 *  @param value The value, converted to a string via `lexicalAdd`.
 */
void
PropertyStream::add(std::string const& key, char value)
{
    lexicalAdd(key, value);
}

/** @param key   The entry key.
 *  @param value The value, converted to a string via `lexicalAdd`.
 */
void
PropertyStream::add(std::string const& key, signed char value)
{
    lexicalAdd(key, value);
}

/** @param key   The entry key.
 *  @param value The value, converted to a string via `lexicalAdd`.
 */
void
PropertyStream::add(std::string const& key, unsigned char value)
{
    lexicalAdd(key, value);
}

/** @param key   The entry key.
 *  @param value The value, converted to a string via `lexicalAdd`.
 */
void
PropertyStream::add(std::string const& key, short value)
{
    lexicalAdd(key, value);
}

/** @param key   The entry key.
 *  @param value The value, converted to a string via `lexicalAdd`.
 */
void
PropertyStream::add(std::string const& key, unsigned short value)
{
    lexicalAdd(key, value);
}

/** @param key   The entry key.
 *  @param value The value, converted to a string via `lexicalAdd`.
 */
void
PropertyStream::add(std::string const& key, int value)
{
    lexicalAdd(key, value);
}

/** @param key   The entry key.
 *  @param value The value, converted to a string via `lexicalAdd`.
 */
void
PropertyStream::add(std::string const& key, unsigned int value)
{
    lexicalAdd(key, value);
}

/** @param key   The entry key.
 *  @param value The value, converted to a string via `lexicalAdd`.
 */
void
PropertyStream::add(std::string const& key, long value)
{
    lexicalAdd(key, value);
}

/** @param key   The entry key.
 *  @param value The value, converted to a string via `lexicalAdd`.
 */
void
PropertyStream::add(std::string const& key, unsigned long value)
{
    lexicalAdd(key, value);
}

/** @param key   The entry key.
 *  @param value The value, converted to a string via `lexicalAdd`.
 */
void
PropertyStream::add(std::string const& key, long long value)
{
    lexicalAdd(key, value);
}

/** @param key   The entry key.
 *  @param value The value, converted to a string via `lexicalAdd`.
 */
void
PropertyStream::add(std::string const& key, unsigned long long value)
{
    lexicalAdd(key, value);
}

/** @param key   The entry key.
 *  @param value The value, converted to a string via `lexicalAdd`.
 */
void
PropertyStream::add(std::string const& key, float value)
{
    lexicalAdd(key, value);
}

/** @param key   The entry key.
 *  @param value The value, converted to a string via `lexicalAdd`.
 */
void
PropertyStream::add(std::string const& key, double value)
{
    lexicalAdd(key, value);
}

/** @param key   The entry key.
 *  @param value The value, converted to a string via `lexicalAdd`.
 */
void
PropertyStream::add(std::string const& key, long double value)
{
    lexicalAdd(key, value);
}

/** Emits an anonymous boolean array element as the string `"true"` or
 *  `"false"` (rather than `"1"` / `"0"`).
 *
 *  @param value The boolean value to emit.
 */
void
PropertyStream::add(bool value)
{
    if (value)
    {
        add("true");
    }
    else
    {
        add("false");
    }
}

/** @param value The value, converted to a string via `lexicalAdd`. */
void
PropertyStream::add(char value)
{
    lexicalAdd(value);
}

/** @param value The value, converted to a string via `lexicalAdd`. */
void
PropertyStream::add(signed char value)
{
    lexicalAdd(value);
}

/** @param value The value, converted to a string via `lexicalAdd`. */
void
PropertyStream::add(unsigned char value)
{
    lexicalAdd(value);
}

/** @param value The value, converted to a string via `lexicalAdd`. */
void
PropertyStream::add(short value)
{
    lexicalAdd(value);
}

/** @param value The value, converted to a string via `lexicalAdd`. */
void
PropertyStream::add(unsigned short value)
{
    lexicalAdd(value);
}

/** @param value The value, converted to a string via `lexicalAdd`. */
void
PropertyStream::add(int value)
{
    lexicalAdd(value);
}

/** @param value The value, converted to a string via `lexicalAdd`. */
void
PropertyStream::add(unsigned int value)
{
    lexicalAdd(value);
}

/** @param value The value, converted to a string via `lexicalAdd`. */
void
PropertyStream::add(long value)
{
    lexicalAdd(value);
}

/** @param value The value, converted to a string via `lexicalAdd`. */
void
PropertyStream::add(unsigned long value)
{
    lexicalAdd(value);
}

/** @param value The value, converted to a string via `lexicalAdd`. */
void
PropertyStream::add(long long value)
{
    lexicalAdd(value);
}

/** @param value The value, converted to a string via `lexicalAdd`. */
void
PropertyStream::add(unsigned long long value)
{
    lexicalAdd(value);
}

/** @param value The value, converted to a string via `lexicalAdd`. */
void
PropertyStream::add(float value)
{
    lexicalAdd(value);
}

/** @param value The value, converted to a string via `lexicalAdd`. */
void
PropertyStream::add(double value)
{
    lexicalAdd(value);
}

/** @param value The value, converted to a string via `lexicalAdd`. */
void
PropertyStream::add(long double value)
{
    lexicalAdd(value);
}

}  // namespace beast
