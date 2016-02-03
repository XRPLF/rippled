# Constants from ripple/protocol/SField.h

# special types
STI_UNKNOWN     = -2
STI_DONE        = -1
STI_NOTPRESENT  = 0

# # types (common)
STI_UINT16 = 1
STI_UINT32 = 2
STI_UINT64 = 3
STI_HASH128 = 4
STI_HASH256 = 5
STI_AMOUNT = 6
STI_VL = 7
STI_ACCOUNT = 8
# 9-13 are reserved
STI_OBJECT = 14
STI_ARRAY = 15

# types (uncommon)
STI_UINT8 = 16
STI_HASH160 = 17
STI_PATHSET = 18
STI_VECTOR256 = 19

# high level types
# cannot be serialized inside other types
STI_TRANSACTION = 10001
STI_LEDGERENTRY = 10002
STI_VALIDATION  = 10003
STI_METADATA    = 10004

def field_code(sti, name):
    if sti < 16:
        if name < 16:
            bytes = [(sti << 4) + name]
        else:
            bytes = [sti << 4, name]
    elif name < 16:
        bytes = [name, sti]
    else:
        bytes = [0, sti, name]
    return ''.join(chr(i) for i in bytes)

# Selected constants from SField.cpp

sfSequence          = field_code(STI_UINT32, 4)
sfPublicKey         = field_code(STI_VL, 1)
sfSigningPubKey     = field_code(STI_VL, 3)
sfSignature         = field_code(STI_VL, 6)
sfMasterSignature   = field_code(STI_VL, 18)
