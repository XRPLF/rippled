
#ifndef RIPPLE_HASH_VALUE_H
#define RIPPLE_HASH_VALUE_H

// VFALCO: TODO, clean this up
//
// These are needed for boost::hash stuff. The implemnetations access
// the Application object for the nonce, introducing a nasty dependency
// so I have split them away from the relevant classes and put them here.

extern std::size_t hash_value(const uint160&);

extern std::size_t hash_value(const uint256&);

extern std::size_t hash_value(const CBase58Data& b58);

#endif
