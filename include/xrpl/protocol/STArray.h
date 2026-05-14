#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/STObject.h>

namespace xrpl {

/** An ordered, variable-length sequence of `STObject` instances.
 *
 *  `STArray` is the protocol's container for repeated structured sub-fields
 *  within transactions and ledger entries — for example `sfMemos` (per-tx
 *  memo objects), `sfSigners` (multi-sign signer list), and `sfNFTokens` (NFT
 *  page entries). It participates fully in the XRPL binary wire format and
 *  the JSON/RPC layer.
 *
 *  The binary format is sentinel-terminated: elements are encoded sequentially
 *  and the stream ends with an `(STI_ARRAY, 1)` marker rather than a length
 *  prefix. Each element must be an `STObject`; non-object field types and
 *  misplaced terminators throw `std::runtime_error` during deserialization.
 *
 *  Instances are tracked by `CountedObject<STArray>` for diagnostic purposes
 *  (see `GetCounts`). The tracking cost is a single atomic
 *  increment/decrement per object lifetime.
 *
 *  @note An empty `STArray` is considered the default value (`isDefault()`
 *      returns `true`), so enclosing `STObject` serializers will omit it from
 *      the wire encoding entirely — consistent with how absent optional array
 *      fields are represented in the ledger.
 */
class STArray final : public STBase, public CountedObject<STArray>
{
private:
    using list_type = std::vector<STObject>;

    list_type v_;

public:
    using value_type = STObject;
    using size_type = list_type::size_type;
    using iterator = list_type::iterator;
    using const_iterator = list_type::const_iterator;

    STArray() = default;
    STArray(STArray const&) = default;

    /** Construct an anonymous STArray from an iterator range of `STObject`s.
     *
     *  The resulting array has no `SField` association. Use the two-argument
     *  overload when the array must be bound to a named field.
     *
     *  @tparam Iter Forward iterator whose reference type is convertible to
     *      `STObject`.
     *  @param first Beginning of the source range.
     *  @param last  One-past-the-end of the source range.
     */
    template <
        class Iter,
        class = std::enable_if_t<
            std::is_convertible_v<typename std::iterator_traits<Iter>::reference, STObject>>>
    explicit STArray(Iter first, Iter last);

    /** Construct an STArray bound to a field, initialized from an iterator range.
     *
     *  @tparam Iter Forward iterator whose reference type is convertible to
     *      `STObject`.
     *  @param f     The `SField` that names this array in its parent object.
     *  @param first Beginning of the source range.
     *  @param last  One-past-the-end of the source range.
     */
    template <
        class Iter,
        class = std::enable_if_t<
            std::is_convertible_v<typename std::iterator_traits<Iter>::reference, STObject>>>
    STArray(SField const& f, Iter first, Iter last);

    STArray&
    operator=(STArray const&) = default;

    /** Move constructor.
     *
     *  Explicitly copies the `SField` name from @p other before moving the
     *  element vector. `STBase` stores the field-name pointer separately from
     *  the data, so without this explicit transfer the moved-into object would
     *  carry a stale field association, causing field-ID mismatches during
     *  serialization.
     *
     *  @param other The array to move from; left in a valid but unspecified state.
     */
    STArray(STArray&&);

    /** Move assignment operator.
     *
     *  Same field-name transfer requirement as the move constructor.
     *
     *  @param other The array to move from; left in a valid but unspecified state.
     *  @return `*this`
     */
    STArray&
    operator=(STArray&&);

    /** Construct an STArray bound to a field with pre-allocated capacity.
     *
     *  @param f The `SField` that names this array in its parent object.
     *  @param n Number of elements to reserve storage for.
     */
    STArray(SField const& f, std::size_t n);

    /** Deserializing constructor — decodes a sentinel-terminated sequence of
     *  inner objects from a binary stream.
     *
     *  Loops over `(type, field)` pairs from @p sit until the canonical
     *  end-of-array marker (`STI_ARRAY, field == 1`) is encountered. Each
     *  iteration validates the next token and constructs an `STObject` element
     *  in place. After construction, `applyTemplateFromSField` validates the
     *  element against the registered schema for its field type (e.g. `sfMemo`,
     *  `sfSigner`).
     *
     *  @param sit   Forward cursor over the binary payload. Advanced in place.
     *  @param f     The `SField` naming this array in its parent object.
     *  @param depth Current nesting depth threaded from the parent `STObject`.
     *      Incremented before each child `STObject` is constructed; `STObject`
     *      enforces a maximum depth of 10 to prevent stack exhaustion from
     *      crafted payloads.
     *  @throws std::runtime_error with message `"Illegal terminator in array"`
     *      if a misplaced end-of-object marker `(STI_OBJECT, 1)` is found.
     *  @throws std::runtime_error with message `"Unknown field"` if an
     *      unrecognized `(type, field)` pair is encountered.
     *  @throws std::runtime_error with message `"Non-object in array"` if a
     *      non-`STI_OBJECT` element type appears in the stream.
     *  @throws std::runtime_error if `applyTemplateFromSField` rejects an
     *      element; the partially constructed array is abandoned entirely.
     */
    STArray(SerialIter& sit, SField const& f, int depth = 0);

    /** Construct an anonymous STArray with pre-allocated capacity.
     *
     *  Creates an array with no `SField` association but with storage reserved
     *  for @p n elements, avoiding early reallocations when the size is known
     *  up front.
     *
     *  @param n Number of elements to reserve space for.
     */
    explicit STArray(int n);

    /** Construct an empty STArray bound to the given field.
     *
     *  @param f The `SField` that names this array in its parent object.
     */
    explicit STArray(SField const& f);

    /** Access element at index @p j without bounds checking.
     *
     *  @param j Zero-based index; behaviour is undefined if `j >= size()`.
     *  @return Reference to the element at position @p j.
     */
    STObject&
    operator[](std::size_t j);

    /** Access element at index @p j without bounds checking (const overload).
     *
     *  @param j Zero-based index; behaviour is undefined if `j >= size()`.
     *  @return Const reference to the element at position @p j.
     */
    STObject const&
    operator[](std::size_t j) const;

    /** Access the last element without bounds checking.
     *
     *  @return Reference to the last element; behaviour is undefined if the
     *      array is empty.
     */
    STObject&
    back();

    /** Access the last element without bounds checking (const overload).
     *
     *  @return Const reference to the last element; behaviour is undefined if
     *      the array is empty.
     */
    [[nodiscard]] STObject const&
    back() const;

    /** Construct an `STObject` in place at the end of the array.
     *
     *  @tparam Args Constructor argument types forwarded to `STObject`.
     *  @param args Arguments forwarded to the `STObject` constructor.
     */
    template <class... Args>
    void
    emplaceBack(Args&&... args);

    /** Append a copy of @p object to the end of the array.
     *
     *  @param object The element to copy-append.
     */
    void
    pushBack(STObject const& object);

    /** Append @p object to the end of the array by move.
     *
     *  @param object The element to move-append.
     */
    void
    pushBack(STObject&& object);

    // STL-compatible alias required by std::back_insert_iterator
    void
    // NOLINTNEXTLINE(readability-identifier-naming)
    push_back(STObject const& object)
    {
        pushBack(object);
    }

    void
    // NOLINTNEXTLINE(readability-identifier-naming)
    push_back(STObject&& object)
    {
        pushBack(std::move(object));
    }

    /** Return an iterator to the first element. */
    iterator
    begin();

    /** Return an iterator to one past the last element. */
    iterator
    end();

    /** Return a const iterator to the first element. */
    [[nodiscard]] const_iterator
    begin() const;

    /** Return a const iterator to one past the last element. */
    [[nodiscard]] const_iterator
    end() const;

    /** Return the number of elements in the array. */
    [[nodiscard]] size_type
    size() const;

    /** Return `true` when the array contains no elements. */
    [[nodiscard]] bool
    empty() const;

    /** Remove all elements, leaving an empty array. */
    void
    clear();

    /** Reserve storage for at least @p n elements without changing the size.
     *
     *  @param n Minimum capacity to reserve.
     */
    void
    reserve(std::size_t n);

    /** Swap contents with @p a in constant time.
     *
     *  @param a The other array to swap with.
     */
    void
    swap(STArray& a) noexcept;

    /** Return a bracket-delimited, comma-separated string including field names.
     *
     *  Each element is rendered via `STObject::getFullText()`. Intended for
     *  debugging and logging.
     *
     *  @return Human-readable representation such as `[fieldA = ..., fieldB = ...]`.
     */
    [[nodiscard]] std::string
    getFullText() const override;

    /** Return a bracket-delimited, comma-separated string of element values only.
     *
     *  Each element is rendered via `STObject::getText()`, which omits field-name
     *  prefixes. Intended for debugging.
     *
     *  @return Human-readable value-only representation of the array.
     */
    [[nodiscard]] std::string
    getText() const override;

    /** Serialize this array to a JSON array value.
     *
     *  Each element that is not `STI_NOTPRESENT` is appended as a JSON object
     *  with a single key — the element's field name — mapping to the element's
     *  own JSON representation:
     *  @code
     *  [ { "Memo": { "MemoData": "..." } }, ... ]
     *  @endcode
     *  Elements with type `STI_NOTPRESENT` (absent optional fields in a
     *  template-bound context) are silently skipped.
     *
     *  @param index JSON rendering options forwarded to each element.
     *  @return A `json::Value` of array type.
     */
    [[nodiscard]] json::Value
    getJson(JsonOptions index) const override;

    /** Append the binary encoding of every element to @p s.
     *
     *  For each element, writes: field ID, element content, per-element object
     *  terminator (`STI_OBJECT, 1`). The outer array's own field ID and the
     *  end-of-array terminator (`STI_ARRAY, 1`) are written by the enclosing
     *  `STObject`, not here — each level is responsible only for its own body.
     *
     *  @param s The serializer to append to.
     */
    void
    add(Serializer& s) const override;

    /** Sort elements in place using a caller-supplied strict-weak-order comparator.
     *
     *  Used to impose canonical ordering before serialization. Key callers:
     *  - `TxMeta::addRaw()` — sorts `AffectedNodes` by `sfLedgerIndex`; a
     *    deviation is a consensus-fork risk.
     *  - NFToken helpers — sorts `sfNFTokens` entries by `sfNFTokenID` when
     *    managing NFT pages.
     *
     *  @note Multi-sign transactions require `sfSigners` to be sorted in
     *      ascending `AccountID` order before submission. That sort is expected
     *      to be performed by the signing client, not by the protocol layer.
     *
     *  @param compare Function pointer returning `true` when the first argument
     *      should precede the second. Must satisfy strict-weak-ordering.
     */
    void
    sort(bool (*compare)(STObject const& o1, STObject const& o2));

    /** Test element-wise equality with another `STArray`.
     *
     *  @param s The array to compare against.
     *  @return `true` if both arrays have the same number of elements and each
     *      pair of corresponding elements compares equal via `STObject::operator==`.
     */
    bool
    operator==(STArray const& s) const;

    /** Test element-wise inequality with another `STArray`.
     *
     *  @param s The array to compare against.
     *  @return `true` if the arrays differ in size or any element pair is unequal.
     */
    bool
    operator!=(STArray const& s) const;

    /** Erase the element at @p pos.
     *
     *  @param pos Iterator to the element to remove.
     *  @return Iterator to the element following the erased one.
     */
    iterator
    erase(iterator pos);

    /** Erase the element at @p pos (const_iterator overload).
     *
     *  @param pos Const iterator to the element to remove.
     *  @return Iterator to the element following the erased one.
     */
    iterator
    erase(const_iterator pos);

    /** Erase the elements in the range `[first, last)`.
     *
     *  @param first Iterator to the first element to remove.
     *  @param last  Iterator to one past the last element to remove.
     *  @return Iterator to the element following the last erased element.
     */
    iterator
    erase(iterator first, iterator last);

    /** Erase the elements in the range `[first, last)` (const_iterator overload).
     *
     *  @param first Const iterator to the first element to remove.
     *  @param last  Const iterator to one past the last element to remove.
     *  @return Iterator to the element following the last erased element.
     */
    iterator
    erase(const_iterator first, const_iterator last);

    /** Return `STI_ARRAY` — the serialized type ID for this class. */
    [[nodiscard]] SerializedTypeID
    getSType() const override;

    /** Test deep equality with another `STBase`.
     *
     *  Performs a `dynamic_cast` to confirm @p t is also an `STArray`, then
     *  delegates to vector equality, which cascades through `STObject::operator==`.
     *
     *  @param t The object to compare against.
     *  @return `true` if @p t is an `STArray` whose elements are pairwise equal
     *      to this array's elements.
     */
    [[nodiscard]] bool
    isEquivalent(STBase const& t) const override;

    /** Return `true` when the array is empty.
     *
     *  An empty `STArray` is the default value; the enclosing `STObject`
     *  serializer will omit a default-valued field from the wire encoding.
     */
    [[nodiscard]] bool
    isDefault() const override;

private:
    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

template <class Iter, class>
STArray::STArray(Iter first, Iter last) : v_(first, last)
{
}

template <class Iter, class>
STArray::STArray(SField const& f, Iter first, Iter last) : STBase(f), v_(first, last)
{
}

inline STObject&
STArray::operator[](std::size_t j)
{
    return v_[j];
}

inline STObject const&
STArray::operator[](std::size_t j) const
{
    return v_[j];
}

inline STObject&
STArray::back()
{
    return v_.back();
}

inline STObject const&
STArray::back() const
{
    return v_.back();
}

template <class... Args>
inline void
STArray::emplaceBack(Args&&... args)
{
    v_.emplace_back(std::forward<Args>(args)...);
}

inline void
STArray::pushBack(STObject const& object)
{
    v_.push_back(object);
}

inline void
STArray::pushBack(STObject&& object)
{
    v_.push_back(std::move(object));
}

inline STArray::iterator
STArray::begin()
{
    return v_.begin();
}

inline STArray::iterator
STArray::end()
{
    return v_.end();
}

inline STArray::const_iterator
STArray::begin() const
{
    return v_.begin();
}

inline STArray::const_iterator
STArray::end() const
{
    return v_.end();
}

inline STArray::size_type
STArray::size() const
{
    return v_.size();
}

inline bool
STArray::empty() const
{
    return v_.empty();
}

inline void
STArray::clear()
{
    v_.clear();
}

inline void
STArray::reserve(std::size_t n)
{
    v_.reserve(n);
}

inline void
STArray::swap(STArray& a) noexcept
{
    v_.swap(a.v_);
}

inline bool
STArray::operator==(STArray const& s) const
{
    return v_ == s.v_;
}

inline bool
STArray::operator!=(STArray const& s) const
{
    return v_ != s.v_;
}

inline STArray::iterator
STArray::erase(iterator pos)
{
    return v_.erase(pos);
}

inline STArray::iterator
STArray::erase(const_iterator pos)
{
    return v_.erase(pos);
}

inline STArray::iterator
STArray::erase(iterator first, iterator last)
{
    return v_.erase(first, last);
}

inline STArray::iterator
STArray::erase(const_iterator first, const_iterator last)
{
    return v_.erase(first, last);
}

}  // namespace xrpl
