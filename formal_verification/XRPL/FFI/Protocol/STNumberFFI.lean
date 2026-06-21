import XRPL.Model.Protocol.STNumber


namespace XRPL.FFI

open XRPL.Model.Protocol (STNumber Number)

@[export lean_st_number_build]
def lean_st_number_build (value : Number) : STNumber := STNumber.ofNumber value
@[export lean_st_number_value]
def lean_st_number_value (s : STNumber) : Number := s.value

end XRPL.FFI
