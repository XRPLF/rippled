use bls12_381::multi_miller_loop;
use bls12_381::Scalar;
use bls12_381::{G1Affine, G2Affine, G2Prepared};
use group::prime::PrimeCurveAffine;
use group::Curve;

use std::mem;
use std::os::raw::c_void;
// use std::time::{Instant};

// Groth16 proof struct
pub struct Proof {
    pub a: G1Affine,
    pub b: G2Affine,
    pub c: G1Affine,
}

// Groth16 verification key struct
pub struct VerifyingKey {
    pub alpha_g1: G1Affine,
    pub beta_g1: G1Affine,
    pub beta_g2: G2Affine,
    pub gamma_g2: G2Affine,
    pub delta_g1: G1Affine,
    pub delta_g2: G2Affine,
    pub ic: Vec<G1Affine>,
}

#[no_mangle]
pub extern "C" fn allocate(size: usize) -> *mut c_void {
    let mut buffer = Vec::with_capacity(size);
    let pointer = buffer.as_mut_ptr();
    mem::forget(buffer);
    pointer as *mut c_void
    // }
}

#[no_mangle]
fn deserialize_g1_wasm(buffer: &mut Vec<u8>) -> G1Affine {
    let d_g1 = G1Affine::from_compressed(&buffer[0..48].try_into().unwrap())
        .expect("Failed to deserialize vk");

    d_g1
}

fn deserialize_g2_wasm(buffer: &mut Vec<u8>) -> G2Affine {
    let d_g2 = G2Affine::from_compressed(&buffer[0..96].try_into().unwrap())
        .expect("Failed to deserialize vk");

    d_g2
}

#[no_mangle]
// pub extern fn bellman_groth16_test(pointer: *mut u8, capacity: usize) -> bool {
pub extern "C" fn bellman_groth16_test() -> bool {
    // let mut bytes = Vec::new();
    // unsafe {
    //     // println!("Test in vm {:?}", pointer);
    //     let v = Vec::from_raw_parts(pointer, capacity, capacity); //TODO no need to deallocate??
    //     bytes.extend_from_slice(&v);
    // }

    // Hardcode the input bytes for testing in different WASM VMs
    // let bytes = [172, 197, 81, 189, 121, 193, 159, 27, 92, 95, 151, 164, 40, 59, 214, 96, 132, 58, 87, 37, 169, 1, 63, 230, 35, 74, 245, 6, 185, 56, 120, 108, 214, 179, 187, 21, 36, 206, 43, 160, 10, 250, 249, 73, 210, 35, 137, 87, 177, 66, 65, 154, 11, 232, 137, 246, 125, 72, 227, 222, 116, 168, 87, 24, 165, 160, 132, 109, 108, 101, 222, 143, 78, 97, 48, 95, 59, 177, 29, 247, 219, 166, 73, 249, 69, 206, 15, 151, 30, 248, 235, 63, 148, 240, 17, 22, 150, 67, 252, 141, 95, 179, 94, 111, 207, 201, 192, 144, 154, 94, 21, 2, 22, 58, 96, 144, 227, 107, 107, 182, 142, 0, 57, 27, 168, 39, 226, 40, 163, 159, 112, 83, 196, 182, 215, 74, 92, 20, 158, 60, 23, 184, 198, 143, 17, 6, 242, 7, 75, 220, 87, 47, 224, 145, 99, 169, 203, 218, 112, 185, 51, 102, 59, 56, 171, 46, 49, 255, 116, 108, 241, 50, 180, 247, 62, 218, 181, 197, 155, 80, 61, 252, 8, 41, 232, 73, 51, 250, 223, 82, 94, 8, 185, 83, 223, 187, 6, 41, 20, 62, 189, 254, 11, 11, 58, 187, 200, 88, 53, 234, 98, 172, 213, 62, 22, 34, 90, 166, 182, 133, 8, 230, 103, 219, 233, 141, 10, 137, 210, 151, 4, 129, 29, 92, 103, 251, 72, 182, 162, 59, 20, 222, 188, 232, 13, 74, 214, 182, 172, 120, 33, 198, 57, 204, 134, 93, 26, 79, 213, 45, 146, 6, 128, 103, 63, 202, 226, 120, 141, 193, 248, 65, 196, 235, 21, 184, 104, 228, 206, 117, 190, 28, 153, 183, 68, 36, 63, 60, 131, 87, 137, 213, 105, 27, 110, 37, 238, 200, 250, 145, 76, 25, 57, 81, 69, 164, 208, 255, 49, 80, 14, 64, 181, 143, 12, 58, 35, 63, 199, 35, 70, 25, 86, 158, 210, 150, 59, 159, 253, 238, 174, 211, 142, 166, 223, 51, 134, 118, 171, 27, 218, 219, 117, 163, 71, 134, 95, 142, 83, 251, 240, 241, 162, 232, 93, 248, 167, 112, 197, 212, 169, 209, 159, 101, 140, 248, 222, 234, 201, 169, 76, 242, 7, 10, 192, 30, 151, 167, 74, 186, 97, 121, 144, 36, 6, 187, 92, 7, 248, 45, 134, 85, 240, 112, 74, 224, 70, 64, 198, 59, 26, 195, 192, 140, 101, 118, 175, 17, 160, 195, 142, 133, 1, 139, 5, 130, 245, 17, 73, 176, 232, 107, 130, 172, 110, 20, 190, 37, 108, 250, 178, 187, 151, 158, 35, 248, 246, 143, 38, 212, 133, 226, 24, 45, 33, 164, 46, 125, 200, 157, 253, 225, 132, 181, 60, 90, 7, 240, 80, 232, 97, 206, 164, 28, 12, 75, 68, 126, 230, 145, 216, 45, 180, 203, 19, 152, 29, 203, 9, 4, 145, 122, 206, 146, 179, 44, 145, 191, 126, 199, 175, 171, 127, 189, 222, 108, 126, 161, 80, 190, 47, 44, 8, 40, 65, 68, 95, 61, 109, 148, 175, 113, 226, 8, 93, 126, 53, 39, 192, 196, 6, 152, 194, 105, 169, 226, 192, 201, 184, 198, 134, 210, 153, 170, 12, 241, 90, 250, 233, 20, 152, 119, 142, 120, 83, 2, 164, 80, 178, 125, 227, 253, 207, 240, 201, 127, 213, 196, 100, 90, 65, 120, 50, 108, 175, 34, 192, 197, 173, 202, 176, 210, 131, 22, 216, 57, 169, 241, 28, 40, 44, 62, 11, 42, 50, 46, 204, 242, 109, 158, 114, 41, 127, 206, 25, 194, 255, 128, 245, 232, 193, 189, 229, 51, 93, 94, 64, 117, 33, 132, 75, 253, 114, 64, 116, 155, 183, 137, 112, 201, 243, 13, 221, 142, 164, 59, 98, 152, 249, 40, 133, 70, 185, 231, 249, 151, 253, 240, 122, 214, 60, 18, 132, 177, 37, 42, 75, 206, 12, 100, 214, 248, 234, 78, 165, 74, 212, 248, 32, 162, 254, 227, 218, 46, 9, 87, 0, 118, 13, 249, 107, 83, 5, 138, 223, 9, 247, 70, 160, 228, 197, 54, 87, 18, 1, 37, 199, 162, 84, 189, 161, 10, 26, 75, 45, 168, 185, 153, 245, 243, 51, 176, 208, 187, 235, 135, 239, 231, 42, 43, 233, 150, 46, 249, 73, 229, 138, 84, 89, 75, 129, 238, 211, 80, 147, 67, 159, 227, 214, 131, 188, 130, 70, 224, 1, 77, 139, 239, 185, 53, 68, 41, 193, 207, 16, 2, 33, 139, 214, 103, 240, 14, 141, 223, 24, 236, 50, 64, 79, 178, 6, 79, 38, 165, 35, 173, 203, 101, 3, 162, 49, 51, 4, 151, 127, 49, 47, 223, 244, 157, 229, 7, 88, 106, 141, 167, 183, 220, 15, 8, 119, 12, 82, 218, 14, 207, 0, 73, 27, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    let bytes = [
        147, 235, 138, 182, 249, 146, 149, 28, 58, 36, 144, 99, 188, 155, 153, 135, 239, 79, 76,
        109, 152, 156, 202, 1, 153, 84, 239, 184, 69, 145, 133, 48, 156, 80, 122, 227, 231, 161,
        137, 232, 67, 183, 34, 186, 230, 135, 25, 90, 136, 201, 110, 134, 208, 93, 78, 82, 153,
        239, 208, 236, 160, 231, 192, 150, 215, 128, 193, 255, 107, 39, 133, 12, 136, 148, 119, 17,
        59, 198, 100, 49, 37, 89, 132, 205, 45, 79, 151, 112, 247, 140, 94, 179, 215, 165, 52, 182,
        153, 68, 204, 210, 218, 156, 69, 74, 192, 30, 160, 13, 80, 188, 23, 112, 21, 124, 91, 147,
        21, 140, 217, 226, 248, 60, 182, 119, 18, 34, 32, 41, 181, 128, 165, 97, 168, 76, 98, 44,
        114, 122, 128, 215, 68, 156, 18, 91, 5, 33, 22, 141, 249, 137, 49, 252, 82, 122, 206, 58,
        183, 108, 176, 15, 38, 183, 87, 254, 34, 102, 195, 78, 166, 227, 96, 180, 137, 173, 131,
        178, 179, 25, 89, 159, 5, 73, 125, 24, 25, 86, 227, 19, 184, 117, 228, 173, 150, 1, 82,
        142, 48, 251, 236, 132, 73, 79, 201, 165, 192, 191, 195, 60, 100, 198, 251, 187, 161, 220,
        63, 143, 38, 21, 189, 219, 194, 100, 64, 186, 102, 7, 186, 213, 227, 92, 228, 52, 181, 171,
        223, 222, 218, 206, 221, 22, 15, 46, 77, 175, 34, 43, 221, 110, 21, 89, 149, 213, 68, 242,
        140, 185, 176, 73, 88, 216, 75, 237, 209, 10, 75, 251, 152, 101, 15, 146, 168, 27, 81, 8,
        61, 76, 103, 230, 171, 23, 144, 171, 6, 118, 157, 233, 234, 214, 132, 106, 30, 171, 121,
        77, 147, 175, 170, 62, 48, 251, 12, 221, 202, 109, 80, 97, 180, 27, 45, 87, 162, 19, 168,
        152, 27, 205, 113, 91, 83, 52, 99, 109, 17, 149, 189, 244, 174, 164, 192, 79, 133, 111,
        195, 215, 232, 129, 166, 204, 3, 169, 248, 49, 18, 190, 198, 145, 177, 169, 10, 4, 66, 134,
        46, 11, 163, 170, 94, 230, 234, 234, 43, 122, 51, 230, 100, 106, 149, 228, 208, 217, 87,
        231, 125, 170, 47, 143, 151, 45, 208, 64, 91, 10, 188, 136, 15, 155, 131, 200, 141, 243,
        200, 5, 109, 22, 98, 189, 193, 44, 40, 95, 126, 145, 234, 190, 205, 179, 172, 224, 147,
        253, 238, 162, 157, 60, 126, 9, 174, 34, 16, 161, 197, 60, 243, 211, 241, 78, 114, 51, 167,
        214, 53, 149, 172, 56, 149, 32, 66, 123, 48, 240, 179, 53, 154, 29, 134, 34, 141, 204, 168,
        184, 158, 165, 115, 241, 119, 228, 11, 35, 82, 186, 132, 103, 65, 243, 215, 31, 105, 201,
        191, 155, 210, 53, 194, 76, 63, 199, 181, 28, 138, 181, 181, 211, 145, 15, 139, 244, 38,
        56, 159, 161, 95, 46, 147, 141, 163, 221, 88, 167, 134, 73, 45, 70, 98, 98, 167, 55, 52,
        234, 110, 150, 79, 248, 157, 167, 84, 210, 89, 10, 193, 169, 32, 40, 218, 7, 236, 206, 85,
        178, 174, 157, 132, 181, 192, 119, 60, 205, 46, 217, 120, 97, 59, 82, 121, 11, 189, 21,
        213, 176, 255, 225, 57, 76, 239, 38, 99, 226, 55, 98, 227, 10, 45, 193, 69, 255, 247, 39,
        121, 86, 150, 6, 220, 98, 41, 132, 237, 189, 169, 110, 213, 115, 33, 228, 197, 61, 219,
        202, 58, 54, 70, 223, 179, 208, 139, 232, 103, 76, 165, 169, 68, 6, 148, 47, 244, 26, 203,
        186, 110, 69, 44, 175, 128, 119, 212, 188, 167, 223, 87, 119, 238, 199, 201, 61, 78, 96,
        175, 0, 156, 145, 196, 253, 162, 175, 172, 227, 80, 251, 96, 61, 189, 35, 13, 97, 22, 157,
        86, 249, 128, 148, 172, 66, 80, 172, 208, 222, 131, 0, 207, 80, 163, 27, 155, 113, 57, 186,
        246, 139, 111, 71, 117, 152, 184, 60, 1, 230, 44, 169, 213, 88, 82, 156, 194, 234, 41, 183,
        87, 36, 175, 154, 156, 128, 59, 187, 208, 101, 9, 51, 205, 42, 174, 29, 215, 43, 150, 183,
        129, 125, 2, 84, 210, 149, 245, 126, 140, 166, 255, 134, 116, 162, 107, 82, 178, 158, 38,
        11, 135, 91, 224, 157, 112, 189, 164, 250, 1, 215, 49, 21, 214, 211, 73, 243, 251, 58, 198,
        1, 165, 196, 122, 13, 238, 252, 227, 229, 149, 47, 13, 173, 171, 176, 185, 220, 82, 96,
        163, 4, 36, 199, 152, 88, 3, 162, 49, 51, 4, 151, 127, 49, 47, 223, 244, 157, 229, 7, 88,
        106, 141, 167, 183, 220, 15, 8, 119, 12, 82, 218, 14, 207, 0, 73, 27, 5, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    ];

    // ***** Test deserialization and reconstruction of vk *****
    // let start_key_recons = Instant::now();
    // println!("Start verification key reconstruction");

    // alpha_g1
    let mut vec_alpha_g1 = bytes[0..48].to_vec();
    let r_alpha_g1 = deserialize_g1_wasm(&mut vec_alpha_g1);

    // beta_g1
    let mut vec_beta_g1 = bytes[48..96].to_vec();
    let r_beta_g1 = deserialize_g1_wasm(&mut vec_beta_g1);

    // beta_g2
    let mut vec_beta_g2 = bytes[96..192].to_vec();
    let r_beta_g2 = deserialize_g2_wasm(&mut vec_beta_g2);

    // gamma_g2
    let mut vec_gamma_g2 = bytes[192..288].to_vec();
    let r_gamma_g2 = deserialize_g2_wasm(&mut vec_gamma_g2);

    // delta_g1
    let mut vec_delta_g1 = bytes[288..336].to_vec();
    let r_delta_g1 = deserialize_g1_wasm(&mut vec_delta_g1);

    // delta_g2
    let mut vec_delta_g2 = bytes[336..432].to_vec();
    let r_delta_g2 = deserialize_g2_wasm(&mut vec_delta_g2);

    // ic
    let vec_ic = bytes[432..576].to_vec();
    // println!("\nic vector: {:?}", vec_ic);
    let mut r_ic: Vec<G1Affine> = Vec::new();
    let mut vec_ic_de = vec_ic[0..48].to_vec();
    r_ic.push(deserialize_g1_wasm(&mut vec_ic_de));
    vec_ic_de = vec_ic[48..96].to_vec();
    r_ic.push(deserialize_g1_wasm(&mut vec_ic_de));
    vec_ic_de = vec_ic[96..144].to_vec();
    r_ic.push(deserialize_g1_wasm(&mut vec_ic_de));

    // Reconstruct vk
    // replace following if using bellman::{groth16, groth16::Proof};
    // let deserialized_vk = groth16::VerifyingKey::<Bls12> {
    let deserialized_vk = VerifyingKey {
        alpha_g1: r_alpha_g1,
        beta_g1: r_beta_g1,
        beta_g2: r_beta_g2,
        gamma_g2: r_gamma_g2,
        delta_g1: r_delta_g1,
        delta_g2: r_delta_g2,
        ic: r_ic,
    };

    // Uncomment following if using bellman::{groth16, groth16::Proof};
    // let pvk = groth16::prepare_verifying_key(&deserialized_vk);
    // println!("Key reconstruction time: {:?}", start_key_recons.elapsed());

    // ***** Reconstruct proof *****
    // let start_proof_recons = Instant::now();

    // proof.g1
    let r_a = G1Affine::from_compressed(&bytes[576..624].try_into().unwrap())
        .expect("Failed to deserialize a");
    // proof.g2
    let r_b = G2Affine::from_compressed(&bytes[624..720].try_into().unwrap())
        .expect("Failed to deserialize b");
    // proof.g1
    let r_c = G1Affine::from_compressed(&bytes[720..768].try_into().unwrap())
        .expect("Failed to deserialize c");

    // Replace following if using bellman::{groth16, groth16::Proof};
    // let r_proof: Proof<Bls12>  = Proof{a: r_a, b: r_b, c: r_c};
    let r_proof: Proof = Proof {
        a: r_a,
        b: r_b,
        c: r_c,
    };
    // println!("Proof reconstruction time: {:?}", start_proof_recons.elapsed());

    // ***** Reconstruct input *****
    // let start_input_recons = Instant::now();

    let last_64_bytes = &bytes[bytes.len() - 64..];

    let r_inputs: Vec<Scalar> = last_64_bytes
        .chunks(32) // Each Scalar in bls12_381 uses 32 bytes
        .map(|chunk| {
            Scalar::from_bytes(chunk.try_into().unwrap()).expect("Invalid bytes for Scalar")
        })
        .collect();

    // println!("Input reconstruction time: {:?}", start_input_recons.elapsed());

    /***** proof verification *****/
    // uncomment following if bellman groth16 is used
    // assert!(groth16::verify_proof(&pvk, &r_proof, &r_inputs).is_ok());
    // let start_verify = Instant::now();

    // Ensure the number of inputs matches the vk.ic length minus 1 (for IC[0])
    if (r_inputs.len() + 1) != deserialized_vk.ic.len() {
        return false;
    }

    /***** Compute linear combination: input_acc = IC[0] + sum(input[i] * IC[i+1]) *****/
    let mut acc = deserialized_vk.ic[0].to_curve(); // Convert G1Affine to G1Projective

    // Computes multi-scalar multiplication,
    // which is a weighted sum of elliptic curve points.
    // In Groth16, this builds the point:
    // acc = IC₀ + input₁ × IC₁ + input₂ × IC₂ + ... + inputₙ × ICₙ
    // Where: ICᵢ are fixed elliptic curve points (from the verifying key).
    // inputᵢ are the public inputs to the circuit.
    // Example: public_inputs = [x₁, x₂], vk.ic = [IC₀, IC₁, IC₂], acc = IC₀ + x₁ * IC₁ + x₂ * IC₂
    // This binds the public inputs to the proof
    for (input, ic_point) in r_inputs.iter().zip(&deserialized_vk.ic[1..]) {
        acc += ic_point.to_curve() * input;
    }

    let acc_affine = acc.to_affine(); // converts the point acc from projective form back to affine form.

    //  Preparing G2 elements for pairing by converting them into G2Prepared format.
    let proof_b_prepared = G2Prepared::from(r_proof.b);
    let gamma_g2_prepared = G2Prepared::from(deserialized_vk.gamma_g2);
    let delta_g2_prepared = G2Prepared::from(deserialized_vk.delta_g2);
    let beta_g2_prepared = G2Prepared::from(deserialized_vk.beta_g2);

    // Compute required product of pairings in their Miller loop form
    // Groth16 verifier checks if e(A, B) * e(acc, γ)⁻¹ * e(C, δ)⁻¹ * e(α, β)⁻¹ == 1
    // which boils down to
    // let start_miller = Instant::now();
    let ml_result = multi_miller_loop(&[
        (&r_proof.a, &proof_b_prepared),                   // e(A,B)
        (&(-acc_affine), &gamma_g2_prepared),              // e(acc, γ)⁻¹
        (&(-r_proof.c), &delta_g2_prepared),               // e(C, δ)⁻¹
        (&(-deserialized_vk.alpha_g1), &beta_g2_prepared), //e(α, β)⁻¹
    ]);
    // println!("Miller time: {:?}", start_miller.elapsed());

    // let start_final = Instant::now();
    let result = ml_result.final_exponentiation();
    // println!("Final time: {:?}", start_final.elapsed());
    // println!("Proof verification time: {:?}", start_verify.elapsed());

    // true
    result == bls12_381::Gt::identity()
}
