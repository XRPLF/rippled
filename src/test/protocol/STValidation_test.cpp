//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <test/jtx.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/protocol/st.h>
#include <memory>
#include <type_traits>

namespace ripple {

class STValidation_test : public beast::unit_test::suite
{
    // No public key:
    static constexpr std::uint8_t payload1[] = {
        0x22, 0x80, 0x00, 0x00, 0x01, 0x26, 0x03, 0x4B, 0xEA, 0x97, 0x29, 0x26,
        0x47, 0x31, 0x1A, 0x3A, 0x4E, 0x69, 0x6B, 0x2B, 0x54, 0x69, 0x66, 0x66,
        0x51, 0x53, 0x1F, 0x1A, 0x4E, 0xBB, 0x43, 0x19, 0x69, 0x16, 0xF8, 0x3E,
        0xEA, 0x5C, 0x77, 0x94, 0x08, 0x19, 0x0B, 0x4B, 0x40, 0x8C, 0xDE, 0xB8,
        0x79, 0x39, 0xF3, 0x9D, 0x66, 0x7B, 0x12, 0xCA, 0x97, 0x50, 0x17, 0x21,
        0x0B, 0xAB, 0xBC, 0x8C, 0xB7, 0xFB, 0x45, 0x49, 0xED, 0x1E, 0x07, 0xB4,
        0xFB, 0xC5, 0xF2, 0xFB, 0x67, 0x2D, 0x18, 0xA6, 0x43, 0x35, 0x28, 0xEB,
        0xD9, 0x06, 0x3E, 0xB3, 0x8B, 0xC2, 0xE0, 0x76, 0x47, 0x30, 0x45, 0x02,
        0x21, 0x00, 0xAF, 0x1D, 0x17, 0xA2, 0x12, 0x7B, 0xA4, 0x6B, 0x40, 0xBD,
        0x58, 0x76, 0x39, 0x3F, 0xF4, 0x49, 0x6B, 0x25, 0xA1, 0xAD, 0xB7, 0x36,
        0xFB, 0x64, 0x4C, 0x05, 0x21, 0x0C, 0x43, 0x02, 0xE5, 0xEE, 0x02, 0x20,
        0x26, 0x01, 0x7C, 0x5F, 0x69, 0xDA, 0xD1, 0xC3, 0x28, 0xED, 0x80, 0x05,
        0x36, 0x86, 0x8B, 0x1B, 0x22, 0xE4, 0x8E, 0x09, 0x11, 0x52, 0x28, 0x5A,
        0x48, 0x8F, 0x98, 0x7A, 0x5A, 0x10, 0x74, 0xCC};

    // Short public key:
    static constexpr std::uint8_t payload2[] = {
        0x22, 0x80, 0x00, 0x00, 0x01, 0x26, 0x03, 0x4B, 0xEA, 0x97, 0x29, 0x26,
        0x47, 0x31, 0x1A, 0x51, 0x53, 0x1F, 0x1A, 0x4E, 0xBB, 0x43, 0x19, 0x69,
        0x16, 0xF8, 0x3E, 0xEA, 0x5C, 0x77, 0x94, 0x08, 0x19, 0x0B, 0x4B, 0x40,
        0x8C, 0xDE, 0xB8, 0x79, 0x39, 0xF3, 0x9D, 0x66, 0x7B, 0x12, 0xCA, 0x97,
        0x50, 0x17, 0x21, 0x0B, 0xAB, 0xBC, 0x8C, 0xB7, 0xFB, 0x45, 0x49, 0xED,
        0x1E, 0x07, 0xB4, 0xFB, 0xC5, 0xF2, 0xFB, 0x67, 0x2D, 0x18, 0xA6, 0x43,
        0x35, 0x28, 0xEB, 0xD9, 0x06, 0x3E, 0xB3, 0x8B, 0xC2, 0xE0, 0x73, 0x20,
        0x02, 0x9D, 0x19, 0xFB, 0x09, 0x40, 0xE5, 0xC0, 0xD8, 0x58, 0x73, 0xFA,
        0x71, 0x19, 0x99, 0x94, 0x4A, 0x68, 0x7D, 0x12, 0x9D, 0xA5, 0xC3, 0x3E,
        0x92, 0x8C, 0x27, 0x51, 0xFC, 0x1B, 0x31, 0xEB, 0x76, 0x46, 0x30, 0x44,
        0x02, 0x20, 0x34, 0x89, 0xA3, 0xBF, 0xA9, 0x97, 0x13, 0xBC, 0x87, 0x61,
        0xC5, 0x2B, 0x7F, 0xAA, 0xE9, 0x31, 0x4C, 0xCD, 0x6F, 0x57, 0x68, 0x70,
        0xC8, 0xDC, 0x58, 0x76, 0x91, 0x2F, 0x70, 0x2F, 0xD0, 0x78, 0x02, 0x20,
        0x7E, 0x57, 0x9D, 0xCA, 0x11, 0xF1, 0x3B, 0xA0, 0x39, 0x38, 0x37, 0x40,
        0xC5, 0xC8, 0xFE, 0xC1, 0xFC, 0xE9, 0xE7, 0x84, 0x6C, 0x2D, 0x47, 0x6E,
        0xD7, 0xFF, 0x83, 0x9D, 0xEF, 0x7D, 0xF7, 0x6A};

    // Long public key:
    static constexpr std::uint8_t payload3[] = {
        0x22, 0x80, 0x00, 0x00, 0x01, 0x26, 0x03, 0x4B, 0xEA, 0x97, 0x29, 0x26,
        0x47, 0x31, 0x1A, 0x51, 0x53, 0x1F, 0x1A, 0x4E, 0xBB, 0x43, 0x19, 0x69,
        0x16, 0xF8, 0x3E, 0xEA, 0x5C, 0x77, 0x94, 0x08, 0x19, 0x0B, 0x4B, 0x40,
        0x8C, 0xDE, 0xB8, 0x79, 0x39, 0xF3, 0x9D, 0x66, 0x7B, 0x12, 0xCA, 0x97,
        0x50, 0x17, 0x21, 0x0B, 0xAB, 0xBC, 0x8C, 0xB7, 0xFB, 0x45, 0x49, 0xED,
        0x1E, 0x07, 0xB4, 0xFB, 0xC5, 0xF2, 0xFB, 0x67, 0x2D, 0x18, 0xA6, 0x43,
        0x35, 0x28, 0xEB, 0xD9, 0x06, 0x3E, 0xB3, 0x8B, 0xC2, 0xE0, 0x73, 0x22,
        0x02, 0x9D, 0x19, 0xFB, 0x09, 0x40, 0xE5, 0xC0, 0xD8, 0x58, 0x73, 0xFA,
        0x71, 0x19, 0x99, 0x94, 0x4A, 0x68, 0x7D, 0x12, 0x9D, 0xA5, 0xC3, 0x3E,
        0x92, 0x8C, 0x27, 0x51, 0xFC, 0x1B, 0x31, 0xEB, 0x32, 0x78, 0x76, 0x46,
        0x30, 0x44, 0x02, 0x20, 0x3C, 0xAB, 0xEE, 0x36, 0xD8, 0xF3, 0x74, 0x5F,
        0x50, 0x28, 0x66, 0x17, 0x57, 0x26, 0x6A, 0xBD, 0x9A, 0x19, 0x08, 0xAA,
        0x65, 0x94, 0x0B, 0xDF, 0x24, 0x20, 0x44, 0x99, 0x05, 0x8C, 0xB7, 0x3D,
        0x02, 0x20, 0x79, 0x66, 0xE6, 0xCC, 0xA2, 0x5E, 0x15, 0xFE, 0x18, 0x4B,
        0xB2, 0xA8, 0x01, 0x3A, 0xD6, 0x63, 0x54, 0x08, 0x1B, 0xDA, 0xD0, 0x04,
        0xEF, 0x4C, 0x73, 0xB3, 0xFF, 0xFE, 0xA9, 0x8E, 0x92, 0xE8};

    // Ed25519 public key:
    static constexpr std::uint8_t payload4[] = {
        0x22, 0x80, 0x00, 0x00, 0x01, 0x26, 0x03, 0x4B, 0xEA, 0x97, 0x29, 0x26,
        0x47, 0x31, 0x1A, 0x51, 0x53, 0x1F, 0x1A, 0x4E, 0xBB, 0x43, 0x19, 0x69,
        0x16, 0xF8, 0x3E, 0xEA, 0x5C, 0x77, 0x94, 0x08, 0x19, 0x0B, 0x4B, 0x40,
        0x8C, 0xDE, 0xB8, 0x79, 0x39, 0xF3, 0x9D, 0x66, 0x7B, 0x12, 0xCA, 0x97,
        0x50, 0x17, 0x21, 0x0B, 0xAB, 0xBC, 0x8C, 0xB7, 0xFB, 0x45, 0x49, 0xED,
        0x1E, 0x07, 0xB4, 0xFB, 0xC5, 0xF2, 0xFB, 0x67, 0x2D, 0x18, 0xA6, 0x43,
        0x35, 0x28, 0xEB, 0xD9, 0x06, 0x3E, 0xB3, 0x8B, 0xC2, 0xE0, 0x73, 0x21,
        0xED, 0x04, 0x8B, 0x9A, 0x31, 0x5E, 0xC7, 0x33, 0xC0, 0x15, 0x3B, 0x67,
        0x04, 0x73, 0x7A, 0x91, 0x3D, 0xEF, 0x57, 0x1D, 0xAD, 0xEC, 0x57, 0xE5,
        0x91, 0x5D, 0x55, 0xD9, 0x32, 0x9D, 0x45, 0x12, 0x85, 0x76, 0x40, 0x52,
        0x07, 0xF9, 0x0D, 0x18, 0x2B, 0xB7, 0xAF, 0x5D, 0x43, 0xF8, 0xF9, 0xC5,
        0xAD, 0xF9, 0xBA, 0x33, 0x23, 0xC0, 0x2F, 0x95, 0xFF, 0x36, 0x94, 0xD8,
        0x99, 0x99, 0xE0, 0x66, 0xF8, 0xB6, 0x27, 0x22, 0xFD, 0x29, 0x39, 0x30,
        0x39, 0xAB, 0x93, 0xDB, 0x9D, 0x2C, 0xE5, 0xF0, 0x4C, 0xB7, 0x30, 0xFD,
        0xC7, 0xD3, 0x21, 0xC9, 0x4E, 0x0D, 0x8A, 0x1B, 0xB2, 0x89, 0x97, 0x10,
        0x7E, 0x84, 0x09};

    // No ledger sequence:
    static constexpr std::uint8_t payload5[] = {
        0x22, 0x80, 0x00, 0x00, 0x01, 0x29, 0x26, 0x47, 0x31, 0x1A, 0x51, 0x53,
        0x1F, 0x1A, 0x4E, 0xBB, 0x43, 0x19, 0x69, 0x16, 0xF8, 0x3E, 0xEA, 0x5C,
        0x77, 0x94, 0x08, 0x19, 0x0B, 0x4B, 0x40, 0x8C, 0xDE, 0xB8, 0x79, 0x39,
        0xF3, 0x9D, 0x66, 0x7B, 0x12, 0xCA, 0x97, 0x50, 0x17, 0x21, 0x0B, 0xAB,
        0xBC, 0x8C, 0xB7, 0xFB, 0x45, 0x49, 0xED, 0x1E, 0x07, 0xB4, 0xFB, 0xC5,
        0xF2, 0xFB, 0x67, 0x2D, 0x18, 0xA6, 0x43, 0x35, 0x28, 0xEB, 0xD9, 0x06,
        0x3E, 0xB3, 0x8B, 0xC2, 0xE0, 0x73, 0x21, 0x02, 0x9D, 0x19, 0xFB, 0x09,
        0x40, 0xE5, 0xC0, 0xD8, 0x58, 0x73, 0xFA, 0x71, 0x19, 0x99, 0x94, 0x4A,
        0x68, 0x7D, 0x12, 0x9D, 0xA5, 0xC3, 0x3E, 0x92, 0x8C, 0x27, 0x51, 0xFC,
        0x1B, 0x31, 0xEB, 0x32, 0x76, 0x47, 0x30, 0x45, 0x02, 0x21, 0x00, 0x83,
        0xB3, 0x1B, 0xE9, 0x03, 0x8F, 0x4A, 0x92, 0x8B, 0x9B, 0x51, 0xEF, 0x79,
        0xED, 0xA1, 0x4A, 0x58, 0x9B, 0x20, 0xCF, 0x89, 0xC4, 0x75, 0x99, 0x5F,
        0x6D, 0x79, 0x51, 0x79, 0x07, 0xF9, 0x93, 0x02, 0x20, 0x39, 0xA6, 0x0C,
        0x77, 0x68, 0x84, 0x50, 0xDB, 0xDA, 0x64, 0x32, 0x74, 0xEC, 0x63, 0x48,
        0x48, 0x96, 0xB5, 0x94, 0x57, 0x55, 0x8D, 0x7D, 0xD8, 0x25, 0x78, 0xD1,
        0xEA, 0x5F, 0xD9, 0xC7, 0xAA};

    // No sign time:
    static constexpr std::uint8_t payload6[] = {
        0x22, 0x80, 0x00, 0x00, 0x01, 0x26, 0x03, 0x4B, 0xEA, 0x97, 0x51, 0x53,
        0x1F, 0x1A, 0x4E, 0xBB, 0x43, 0x19, 0x69, 0x16, 0xF8, 0x3E, 0xEA, 0x5C,
        0x77, 0x94, 0x08, 0x19, 0x0B, 0x4B, 0x40, 0x8C, 0xDE, 0xB8, 0x79, 0x39,
        0xF3, 0x9D, 0x66, 0x7B, 0x12, 0xCA, 0x97, 0x50, 0x17, 0x21, 0x0B, 0xAB,
        0xBC, 0x8C, 0xB7, 0xFB, 0x45, 0x49, 0xED, 0x1E, 0x07, 0xB4, 0xFB, 0xC5,
        0xF2, 0xFB, 0x67, 0x2D, 0x18, 0xA6, 0x43, 0x35, 0x28, 0xEB, 0xD9, 0x06,
        0x3E, 0xB3, 0x8B, 0xC2, 0xE0, 0x73, 0x21, 0x02, 0x9D, 0x19, 0xFB, 0x09,
        0x40, 0xE5, 0xC0, 0xD8, 0x58, 0x73, 0xFA, 0x71, 0x19, 0x99, 0x94, 0x4A,
        0x68, 0x7D, 0x12, 0x9D, 0xA5, 0xC3, 0x3E, 0x92, 0x8C, 0x27, 0x51, 0xFC,
        0x1B, 0x31, 0xEB, 0x32, 0x76, 0x47, 0x30, 0x45, 0x02, 0x21, 0x00, 0xDD,
        0xB0, 0x59, 0x9A, 0x02, 0x3E, 0xF2, 0x44, 0xCE, 0x1D, 0xA8, 0x99, 0x06,
        0xF3, 0x8A, 0x4B, 0xEB, 0x95, 0x42, 0x63, 0x6A, 0x6C, 0x04, 0x30, 0x7F,
        0x62, 0x78, 0x3A, 0x89, 0xB0, 0x3F, 0x22, 0x02, 0x20, 0x4E, 0x6A, 0x55,
        0x63, 0x8A, 0x19, 0xED, 0xFE, 0x70, 0x34, 0xD1, 0x30, 0xED, 0x7C, 0xAF,
        0xB2, 0x78, 0xBB, 0x15, 0x6C, 0x42, 0x3E, 0x19, 0x5D, 0xEA, 0xC5, 0x5E,
        0x23, 0xE2, 0x14, 0x80, 0x54};

    // No signature field:
    static constexpr std::uint8_t payload7[] = {
        0x22, 0x80, 0x00, 0x00, 0x01, 0x26, 0x03, 0x4B, 0xEA, 0x97, 0x29, 0x26,
        0x47, 0x31, 0x1A, 0x51, 0x53, 0x1F, 0x1A, 0x4E, 0xBB, 0x43, 0x19, 0x69,
        0x16, 0xF8, 0x3E, 0xEA, 0x5C, 0x77, 0x94, 0x08, 0x19, 0x0B, 0x4B, 0x40,
        0x8C, 0xDE, 0xB8, 0x79, 0x39, 0xF3, 0x9D, 0x66, 0x7B, 0x12, 0xCA, 0x97,
        0x50, 0x17, 0x21, 0x0B, 0xAB, 0xBC, 0x8C, 0xB7, 0xFB, 0x45, 0x49, 0xED,
        0x1E, 0x07, 0xB4, 0xFB, 0xC5, 0xF2, 0xFB, 0x67, 0x2D, 0x18, 0xA6, 0x43,
        0x35, 0x28, 0xEB, 0xD9, 0x06, 0x3E, 0xB3, 0x8B, 0xC2, 0xE0, 0x73, 0x21,
        0x02, 0x9D, 0x19, 0xFB, 0x09, 0x40, 0xE5, 0xC0, 0xD8, 0x58, 0x73, 0xFA,
        0x71, 0x19, 0x99, 0x94, 0x4A, 0x68, 0x7D, 0x12, 0x9D, 0xA5, 0xC3, 0x3E,
        0x92, 0x8C, 0x27, 0x51, 0xFC, 0x1B, 0x31, 0xEB, 0x32};

    // Good:
    static constexpr std::uint8_t payload8[] = {
        0x22, 0x80, 0x00, 0x00, 0x01, 0x26, 0x03, 0x4B, 0xEA, 0x97, 0x29, 0x26,
        0x47, 0x31, 0x1A, 0x51, 0x53, 0x1F, 0x1A, 0x4E, 0xBB, 0x43, 0x19, 0x69,
        0x16, 0xF8, 0x3E, 0xEA, 0x5C, 0x77, 0x94, 0x08, 0x19, 0x0B, 0x4B, 0x40,
        0x8C, 0xDE, 0xB8, 0x79, 0x39, 0xF3, 0x9D, 0x66, 0x7B, 0x12, 0xCA, 0x97,
        0x50, 0x17, 0x21, 0x0B, 0xAB, 0xBC, 0x8C, 0xB7, 0xFB, 0x45, 0x49, 0xED,
        0x1E, 0x07, 0xB4, 0xFB, 0xC5, 0xF2, 0xFB, 0x67, 0x2D, 0x18, 0xA6, 0x43,
        0x35, 0x28, 0xEB, 0xD9, 0x06, 0x3E, 0xB3, 0x8B, 0xC2, 0xE0, 0x73, 0x21,
        0x02, 0x9D, 0x19, 0xFB, 0x09, 0x40, 0xE5, 0xC0, 0xD8, 0x58, 0x73, 0xFA,
        0x71, 0x19, 0x99, 0x94, 0x4A, 0x68, 0x7D, 0x12, 0x9D, 0xA5, 0xC3, 0x3E,
        0x92, 0x8C, 0x27, 0x51, 0xFC, 0x1B, 0x31, 0xEB, 0x32, 0x76, 0x47, 0x30,
        0x45, 0x02, 0x21, 0x00, 0xDD, 0x29, 0xDC, 0xAC, 0x82, 0x5E, 0xF9, 0xE2,
        0x2D, 0x26, 0x03, 0x95, 0xC2, 0x11, 0x3A, 0x2A, 0x83, 0xEE, 0xA0, 0x2B,
        0x9F, 0x2A, 0x51, 0xBD, 0x6B, 0xF7, 0x83, 0xCE, 0x4A, 0x7C, 0x52, 0x29,
        0x02, 0x20, 0x52, 0x45, 0xB9, 0x07, 0x57, 0xEF, 0xB2, 0x6C, 0x69, 0xC5,
        0x47, 0xCA, 0xE2, 0x76, 0x00, 0xFC, 0x35, 0x46, 0x5D, 0x19, 0x64, 0xCE,
        0xCA, 0x88, 0xA1, 0x2A, 0x20, 0xCF, 0x3C, 0xF9, 0xCE, 0xCF};

public:
    void
    testDeserialization()
    {
        testcase("Deserialization");

        try
        {
            SerialIter sit{payload8};

            auto val = std::make_shared<STValidation>(
                sit, [](PublicKey const& pk) { return calcNodeID(pk); }, true);

            BEAST_EXPECT(val);
            BEAST_EXPECT(val->isFieldPresent(sfLedgerSequence));
            BEAST_EXPECT(val->isFieldPresent(sfSigningPubKey));
            BEAST_EXPECT(val->isFieldPresent(sfSigningTime));
            BEAST_EXPECT(val->isFieldPresent(sfFlags));
            BEAST_EXPECT(val->isFieldPresent(sfLedgerHash));
            BEAST_EXPECT(val->isFieldPresent(sfSignature));
        }
        catch (std::exception const& ex)
        {
            fail(std::string("Unexpected exception thrown: ") + ex.what());
        }

        testcase("Deserialization: Public Key Tests");

        try
        {
            SerialIter sit{payload1};
            auto val = std::make_shared<ripple::STValidation>(
                sit, [](PublicKey const& pk) { return calcNodeID(pk); }, false);
            fail("An exception should have been thrown");
        }
        catch (std::exception const& ex)
        {
            BEAST_EXPECT(
                strcmp(
                    ex.what(),
                    "Field 'SigningPubKey' is required but missing.") == 0);
        }

        try
        {
            SerialIter sit{payload2};
            auto val = std::make_shared<ripple::STValidation>(
                sit, [](PublicKey const& pk) { return calcNodeID(pk); }, false);
            fail("An exception should have been thrown");
        }
        catch (std::exception const& ex)
        {
            BEAST_EXPECT(
                strcmp(ex.what(), "Invalid public key in validation") == 0);
        }

        try
        {
            SerialIter sit{payload3};
            auto val = std::make_shared<ripple::STValidation>(
                sit, [](PublicKey const& pk) { return calcNodeID(pk); }, false);
            fail("An exception should have been thrown");
        }
        catch (std::exception const& ex)
        {
            BEAST_EXPECT(
                strcmp(ex.what(), "Invalid public key in validation") == 0);
        }

        try
        {
            SerialIter sit{payload4};
            auto val = std::make_shared<ripple::STValidation>(
                sit, [](PublicKey const& pk) { return calcNodeID(pk); }, false);
            fail("An exception should have been thrown");
        }
        catch (std::exception const& ex)
        {
            BEAST_EXPECT(
                strcmp(ex.what(), "Invalid public key in validation") == 0);
        }

        testcase("Deserialization: Missing Fields");

        try
        {
            SerialIter sit{payload5};
            auto val = std::make_shared<STValidation>(
                sit, [](PublicKey const& pk) { return calcNodeID(pk); }, false);
            fail("Expected exception not thrown from validation");
        }
        catch (std::exception const& ex)
        {
            BEAST_EXPECT(
                strcmp(
                    ex.what(),
                    "Field 'LedgerSequence' is required but missing.") == 0);
        }

        try
        {
            SerialIter sit{payload6};
            auto val = std::make_shared<STValidation>(
                sit, [](PublicKey const& pk) { return calcNodeID(pk); }, false);
            fail("Expected exception not thrown from validation");
        }
        catch (std::exception const& ex)
        {
            BEAST_EXPECT(
                strcmp(
                    ex.what(),
                    "Field 'SigningTime' is required but missing.") == 0);
        }

        try
        {
            SerialIter sit{payload7};

            auto val = std::make_shared<STValidation>(
                sit, [](PublicKey const& pk) { return calcNodeID(pk); }, false);

            fail("Expected exception not thrown from validation");
        }
        catch (std::exception const& ex)
        {
            BEAST_EXPECT(
                strcmp(
                    ex.what(), "Field 'Signature' is required but missing.") ==
                0);
        }

        testcase("Deserialization: Corrupted Data / Fuzzing");

        // Mutate a known-good validation and expect it to fail:
        std::vector<std::uint8_t> v;
        for (auto c : payload8)
            v.push_back(c);

        beast::xor_shift_engine g(148979842);

        for (std::size_t i = 0; i != v.size(); ++i)
        {
            auto v2 = v;

            while (v2[i] == v[i])
                v2[i] = rand_byte<std::uint8_t>(g);

            try
            {
                SerialIter sit{makeSlice(v2)};

                auto val = std::make_shared<STValidation>(
                    sit,
                    [](PublicKey const& pk) { return calcNodeID(pk); },
                    true);

                fail(
                    "Mutated validation signature checked out: offset=" +
                    std::to_string(i));
            }
            catch (std::exception const&)
            {
                pass();
            }
        }
    }

    void
    run() override
    {
        testDeserialization();
    }
};

BEAST_DEFINE_TESTSUITE(STValidation, protocol, ripple);

}  // namespace ripple
