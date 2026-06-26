namespace XRPL.Model.Protocol

inductive TER where
  | tesSUCCESS
  | tecINTERNAL
  | tecAMM_INVALID_TOKENS
  | tecAMM_FAILED
  | tecUNFUNDED_AMM
  | tecNO_ENTRY
  | tecWRONG_ASSET
  | tecOBJECT_NOT_FOUND
  | tecNO_AUTH
  | tecINSUFFICIENT_RESERVE
  | tecDIR_FULL
  | tecNO_TARGET
  | tecEXPIRED
  | tecNO_PERMISSION
  | tecDUPLICATE
  | tecNO_LINE_INSUF_RESERVE
  | tecHAS_OBLIGATIONS
  | tecNO_DST
  | tecDST_TAG_NEEDED
  | tecNO_LINE
  | tecFAILED_PROCESSING
  | tecPATH_DRY
  | tecINSUFFICIENT_FUNDS
  | tefINTERNAL
  | tefBAD_LEDGER
  | terNO_ACCOUNT
  | terNO_RIPPLE
  | tecFROZEN
  | tecLOCKED
  | temMALFORMED
  | temBAD_AMOUNT
  | telFAILED_PROCESSING
  | temINVALID
  | temINVALID_FLAG
  | temBAD_FEE
  | temBAD_SRC_ACCOUNT
  | temDISABLED
  | tecLIMIT_EXCEEDED
  deriving DecidableEq, Repr, BEq

def TER.operator_bool : TER → Bool
  | .tesSUCCESS => false
  | _ => true

def TER.isTesSuccess : TER → Bool
  | .tesSUCCESS => true
  | _ => false

-- A `tec` result claims a fee (the tx is recorded, its effects discarded).
def TER.isTec : TER → Bool
  | .tecINTERNAL | .tecAMM_INVALID_TOKENS | .tecAMM_FAILED | .tecUNFUNDED_AMM
  | .tecNO_ENTRY | .tecWRONG_ASSET | .tecOBJECT_NOT_FOUND | .tecNO_AUTH
  | .tecINSUFFICIENT_RESERVE | .tecDIR_FULL | .tecNO_TARGET | .tecEXPIRED
  | .tecNO_PERMISSION | .tecDUPLICATE | .tecNO_LINE_INSUF_RESERVE
  | .tecHAS_OBLIGATIONS | .tecNO_DST | .tecDST_TAG_NEEDED | .tecNO_LINE
  | .tecFAILED_PROCESSING | .tecPATH_DRY | .tecINSUFFICIENT_FUNDS
  | .tecFROZEN | .tecLOCKED | .tecLIMIT_EXCEEDED => true
  | _ => false

end XRPL.Model.Protocol
