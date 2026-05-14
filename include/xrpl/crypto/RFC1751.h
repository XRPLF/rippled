/** @file
 *  RFC 1751 mnemonic encoding for 128-bit XRPL wallet seeds.
 *
 *  Declares the `RFC1751` utility class, which encodes and decodes 128-bit
 *  binary keys as sequences of short English words drawn from a 2048-word
 *  dictionary.  Each word represents exactly 11 bits; a 64-bit block maps
 *  to 6 words (64 data bits + 2 parity bits = 66 bits).  A full 128-bit key
 *  therefore encodes as 12 words in two back-to-back 6-word groups.
 *
 *  The primary consumer is `Seed.cpp`, which uses the codec to produce
 *  human-readable wallet seed mnemonics.  `NetworkOPs.cpp` also uses
 *  `getWordFromBlob` to derive a stable short label for the local node's
 *  public key in log output.
 */

#pragma once

#include <string>
#include <vector>

namespace xrpl {

/** XRPL adaptation of the RFC 1751 128-bit mnemonic key codec.
 *
 *  Converts 128-bit binary keys to and from sequences of 12 English words
 *  using a fixed 2048-word dictionary.  The dictionary is split at index 571:
 *  words 0–570 have 1–3 characters; words 571–2047 are all exactly 4
 *  characters.  This property is exploited internally to halve binary-search
 *  range during decoding.
 *
 *  All methods are static; this class is a pure stateless namespace and is
 *  never instantiated.
 *
 *  @note `Seed.cpp` reverses the 16 seed bytes before passing them to
 *      `getEnglishFromKey` and after receiving them from `getKeyFromEnglish`
 *      to satisfy the RFC's big-endian byte-order convention.
 */
class RFC1751
{
public:
    /** Decode a 12-word mnemonic string into a 128-bit binary key.
     *
     *  Splits @p strHuman on whitespace (multiple spaces are collapsed),
     *  validates and normalises each word via `standard()`, looks each one
     *  up in the dictionary, and packs the resulting 11-bit indices into two
     *  8-byte binary halves.  Each half carries a 2-bit parity check computed
     *  from the 64 data bits; the decode fails with `-2` if the recomputed
     *  parity does not match.
     *
     *  @param strKey   Output parameter; set to the 16-byte binary key on
     *      success.  Unchanged on any failure return.
     *  @param strHuman 12 space-separated words to decode.  Leading and
     *      trailing whitespace is trimmed before splitting.
     *  @return  1  success — @p strKey holds the decoded 16-byte key.
     *  @return  0  a word was not found in the dictionary.
     *  @return -1  malformed input: word count ≠ 12, or a word exceeds 4
     *      characters.
     *  @return -2  all words are valid but the 2-bit parity check failed,
     *      indicating a transcription error.
     *
     *  @note The four distinct return codes must not be collapsed; `-2`
     *      (parity failure) implies the words themselves were individually
     *      valid and is a different diagnostic than `0` (unknown word).
     */
    static int
    getKeyFromEnglish(std::string& strKey, std::string const& strHuman);

    /** Encode a 128-bit binary key as 12 space-separated English words.
     *
     *  Encodes the first 8 bytes of @p strKey as 6 words and the next 8
     *  bytes as a further 6 words, then joins the two groups with a single
     *  space.  A 2-bit parity value is appended to each 64-bit block before
     *  encoding to support transcription-error detection on decode.
     *
     *  Encoding is lossless and cannot fail for valid 16-byte input; no
     *  return code is needed.
     *
     *  @param strHuman Output parameter; receives the 12-word mnemonic string.
     *  @param strKey   The 16-byte (128-bit) binary key to encode.  Behaviour
     *      is undefined if fewer than 16 bytes are provided.
     */
    static void
    getEnglishFromKey(std::string& strHuman, std::string const& strKey);

    /** Map arbitrary binary data to a single dictionary word.
     *
     *  Applies the Jenkins one-at-a-time hash to the input bytes, then
     *  indexes into the 2048-word dictionary using the hash modulo 2048.
     *  The result is a stable, reproducible label for the input data.
     *
     *  @param blob  Pointer to the input data.
     *  @param bytes Number of bytes to hash.
     *  @return A single uppercase dictionary word of 1–4 characters.
     *
     *  @note This function is **not** cryptographically secure.  It is
     *      intended only for producing human-readable identifiers, such as
     *      the `shroudedHostId` label derived from a node's public key in
     *      `NetworkOPs.cpp`.
     */
    static std::string
    getWordFromBlob(void const* blob, size_t bytes);

private:
    /** Read up to 11 bits from a byte array at an arbitrary bit offset.
     *
     *  Assembles up to 3 adjacent bytes into a 24-bit window, shifts right
     *  to align the target field, and masks to @p length bits.  Works across
     *  byte boundaries.  The output buffer for the 66-bit block (64 data +
     *  2 parity) must be at least 9 bytes.
     *
     *  @param s      Source byte array (at least ⌈(start + length) / 8⌉ + 1
     *      bytes long; 9 bytes for the full 66-bit block).
     *  @param start  First bit to read (0-based).  Must be ≥ 0.
     *  @param length Number of bits to read.  Must satisfy 0 ≤ length ≤ 11
     *      and start + length ≤ 66.
     *  @return The extracted value, right-justified and zero-extended.
     */
    static unsigned long
    extract(char const* s, int start, int length);

    /** Encode an 8-byte binary block as 6 space-separated dictionary words.
     *
     *  Appends a 9th byte carrying a 2-bit parity value (sum of all 32
     *  two-bit pairs in the 64-bit payload, placed at bits 64–65), then
     *  calls `extract()` at six 11-bit offsets to obtain dictionary indices.
     *
     *  @param strHuman Output; receives the 6-word space-separated string.
     *  @param strData  Exactly 8 bytes of binary data to encode.
     */
    static void
    btoe(std::string& strHuman, std::string const& strData);

    /** Write up to 11 bits into a byte array at an arbitrary bit offset.
     *
     *  ORs the bit field into the target bytes; the output buffer must be
     *  zero-initialised before the first call because this function
     *  accumulates bits with bitwise OR rather than assignment.
     *
     *  @param s      Target byte array (must be zero-initialised).
     *  @param x      Value to insert (only the low @p length bits are used).
     *  @param start  First destination bit (0-based).  Must be ≥ 0.
     *  @param length Number of bits to write.  Must satisfy 0 ≤ length ≤ 11
     *      and start + length ≤ 66.
     */
    static void
    insert(char* s, int x, int start, int length);

    /** Normalise a mnemonic word for dictionary lookup.
     *
     *  Applies three in-place transformations to tolerate common
     *  handwriting and OCR ambiguities: lowercased letters are uppercased,
     *  `'1'` is replaced by `'L'`, `'0'` by `'O'`, and `'5'` by `'S'`.
     *
     *  @param strWord Word to normalise in place.
     */
    static void
    standard(std::string& strWord);

    /** Binary-search the dictionary within a given index range.
     *
     *  The dictionary is sorted, and its first 571 entries (indices 0–570)
     *  are words of 1–3 characters while the remaining 1477 (indices
     *  571–2047) are all exactly 4 characters.  Callers restrict the range
     *  based on word length to halve the search space.
     *
     *  @param strWord Word to search for (must already be normalised via
     *      `standard()`).
     *  @param iMin    Inclusive lower bound of the search range.
     *  @param iMax    Exclusive upper bound of the search range.
     *  @return The dictionary index of @p strWord, or -1 if not found.
     */
    static int
    wsrch(std::string const& strWord, int iMin, int iMax);

    /** Decode 6 mnemonic words into an 8-byte binary block.
     *
     *  Normalises each word, looks it up via `wsrch()`, packs the resulting
     *  11-bit indices into a 9-byte buffer using `insert()`, then validates
     *  the 2-bit parity stored at bit offset 64.
     *
     *  @param strData   Output; receives the 8 decoded data bytes on success.
     *      Unchanged on any failure return.
     *  @param vsHuman   Exactly 6 words to decode.  Returns -1 immediately
     *      if the vector does not contain exactly 6 elements, or if any
     *      word is longer than 4 characters.
     *  @return  1  success.
     *  @return  0  a word was not found in the dictionary.
     *  @return -1  wrong word count or word exceeds 4 characters.
     *  @return -2  parity mismatch.
     */
    static int
    etob(std::string& strData, std::vector<std::string> vsHuman);

    /** The 2048-word mnemonic dictionary, sorted ascending.
     *
     *  Indices 0–570 contain words of 1–3 characters; indices 571–2047
     *  contain words of exactly 4 characters.  This structural split is
     *  relied upon by `wsrch()` to restrict binary-search ranges.
     */
    static char const* dictionary[];
};

}  // namespace xrpl
