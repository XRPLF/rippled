import os
import sys
import subprocess
import re

OPT = "-Oz"


def update_fixture(project_name, wasm):
    fixture_name = (
        re.sub(r"_([a-z])", lambda m: m.group(1).upper(), project_name) + "WasmHex"
    )
    print(f"Updating fixture: {fixture_name}")

    cpp_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "fixtures.cpp"))
    h_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "fixtures.h"))
    with open(cpp_path, "r", encoding="utf8") as f:
        cpp_content = f.read()

    pattern = rf'extern std::string const {fixture_name} =[ \n]+"[^;]*;'
    if re.search(pattern, cpp_content, flags=re.MULTILINE):
        updated_cpp_content = re.sub(
            pattern,
            f'extern std::string const {fixture_name} = "{wasm}";',
            cpp_content,
            flags=re.MULTILINE,
        )
    else:
        with open(h_path, "r", encoding="utf8") as f:
            h_content = f.read()
        updated_h_content = (
            h_content.rstrip() + f"\n\n extern std::string const {fixture_name};\n"
        )
        with open(h_path, "w", encoding="utf8") as f:
            f.write(updated_h_content)
        updated_cpp_content = (
            cpp_content.rstrip()
            + f'\n\nextern std::string const {fixture_name} = "{wasm}";\n'
        )

    with open(cpp_path, "w", encoding="utf8") as f:
        f.write(updated_cpp_content)


def process_rust(project_name):
    project_path = os.path.abspath(
        os.path.join(os.path.dirname(__file__), project_name)
    )
    wasm_location = f"target/wasm32v1-none/release/{project_name}.wasm"
    build_cmd = (
        f"(cd {project_path} "
        f"&& cargo build --target wasm32v1-none --release "
        f"&& wasm-opt {wasm_location} {OPT} -o {wasm_location}"
        ")"
    )
    try:
        result = subprocess.run(
            build_cmd, shell=True, check=True, capture_output=True, text=True
        )
        print(f"stdout: {result.stdout}")
        if result.stderr:
            print(f"stderr: {result.stderr}")
        print(f"WASM file for {project_name} has been built and optimized.")
    except subprocess.CalledProcessError as e:
        print(f"exec error: {e}")
        sys.exit(1)

    src_path = os.path.abspath(
        os.path.join(
            os.path.dirname(__file__),
            f"{project_name}/target/wasm32v1-none/release/{project_name}.wasm",
        )
    )
    with open(src_path, "rb") as f:
        data = f.read()
    wasm = data.hex()
    update_fixture(project_name, wasm)


def process_c(project_name):
    project_path = os.path.abspath(
        os.path.join(os.path.dirname(__file__), f"{project_name}.c")
    )
    wasm_path = os.path.abspath(
        os.path.join(os.path.dirname(__file__), f"{project_name}.wasm")
    )
    build_cmd = (
        f"$CC --sysroot=$SYSROOT "
        f"-O3 -ffast-math --target=wasm32 -fno-exceptions -fno-threadsafe-statics -fvisibility=default -Wl,--export-all -Wl,--no-entry -Wl,--allow-undefined -DNDEBUG --no-standard-libraries -fno-builtin-memset "
        f"-o {wasm_path} {project_path}"
        f"&& wasm-opt {wasm_path} {OPT} -o {wasm_path}"
    )
    try:
        result = subprocess.run(
            build_cmd, shell=True, check=True, capture_output=True, text=True
        )
        print(f"stdout: {result.stdout}")
        if result.stderr:
            print(f"stderr: {result.stderr}")
        print(
            f"WASM file for {project_name} has been built with WASI support using clang."
        )
    except subprocess.CalledProcessError as e:
        print(f"exec error: {e}")
        sys.exit(1)

    with open(wasm_path, "rb") as f:
        data = f.read()
    wasm = data.hex()
    update_fixture(project_name, wasm)


if __name__ == "__main__":
    if len(sys.argv) > 2:
        print("Usage: python copyFixtures.py [<project_name>]")
        sys.exit(1)
    if len(sys.argv) == 2:
        if os.path.isdir(os.path.join(os.path.dirname(__file__), sys.argv[1])):
            process_rust(sys.argv[1])
        else:
            process_c(sys.argv[1])
        print("Fixture has been processed.")
    else:
        dirs = [
            d
            for d in os.listdir(os.path.dirname(__file__))
            if os.path.isdir(os.path.join(os.path.dirname(__file__), d))
        ]
        c_files = [f for f in os.listdir(os.path.dirname(__file__)) if f.endswith(".c")]
        for d in dirs:
            process_rust(d)
        for c in c_files:
            process_c(c[:-2])
        print("All fixtures have been processed.")
