# cspell: disable
import os
import sys
import subprocess
import re
import tempfile
import zipfile
from difflib import get_close_matches

OPT = "-Oz"


def pascal_case(name):
    return "".join(word[:1].upper() + word[1:] for word in re.split(r"[_\W]+", name))


def normalize_name(name):
    name = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name)
    return re.sub(r"[^a-z0-9]", "", name.lower())


def fixture_key(name):
    name = normalize_name(name).removeprefix("k")
    return name.removesuffix("wasmhex").removesuffix("hex")


def declared_fixtures():
    h_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "fixtures.h"))
    with open(h_path, "r", encoding="utf8") as f:
        return re.findall(
            r"extern std::string const ([A-Za-z_][A-Za-z0-9_]*);", f.read()
        )


def find_fixture_name(project_name, suffix):
    default = re.sub(r"_([a-z])", lambda m: m.group(1).upper(), project_name) + suffix
    k_default = f"k{pascal_case(project_name)}{suffix}"
    declarations = declared_fixtures()
    normalized = {normalize_name(name): name for name in declarations}
    fixture_keys = {fixture_key(name): name for name in declarations}

    for name in (default, k_default):
        if normalize_name(name) in normalized:
            return normalized[normalize_name(name)]

    project_key = normalize_name(project_name)
    matches = [
        name
        for key, name in fixture_keys.items()
        if key.endswith(project_key)
        or key.startswith(project_key)
        or project_key.endswith(key)
        or project_key.startswith(key)
    ]
    if len(matches) == 1:
        return matches[0]

    close = get_close_matches(project_key, fixture_keys.keys(), n=1, cutoff=0.82)
    if close:
        return fixture_keys[close[0]]

    return k_default


def fixture_cpp_path(fixture_name):
    base_path = os.path.dirname(__file__)
    pattern = rf"extern std::string const {fixture_name} ="
    for file_name in os.listdir(base_path):
        if not file_name.endswith(".cpp"):
            continue
        cpp_path = os.path.abspath(os.path.join(base_path, file_name))
        with open(cpp_path, "r", encoding="utf8") as f:
            if re.search(pattern, f.read()):
                return cpp_path
    return os.path.abspath(os.path.join(base_path, "fixtures.cpp"))


def update_fixture(project_name, wasm, suffix="WasmHex"):
    fixture_name = find_fixture_name(project_name, suffix)
    print(f"Updating fixture: {fixture_name}")

    cpp_path = fixture_cpp_path(fixture_name)
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
            h_content.rstrip() + f"\n\nextern std::string const {fixture_name};\n"
        )
        with open(h_path, "w", encoding="utf8") as f:
            f.write(updated_h_content)
        updated_cpp_content = (
            cpp_content.rstrip()
            + f'\n\nextern std::string const {fixture_name} = "{wasm}";\n'
        )

    with open(cpp_path, "w", encoding="utf8") as f:
        f.write(updated_cpp_content)


def read_wasm_hex(path):
    with open(path, "rb") as f:
        return f.read().hex()


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
        subprocess.run(build_cmd, shell=True, check=True)
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
    update_fixture(project_name, read_wasm_hex(src_path))


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
        subprocess.run(build_cmd, shell=True, check=True)
        print(
            f"WASM file for {project_name} has been built with WASI support using clang."
        )
    except subprocess.CalledProcessError as e:
        print(f"exec error: {e}")
        sys.exit(1)

    update_fixture(project_name, read_wasm_hex(wasm_path))


def wat2Wasm(wat_path, wasm_path):
    build_cmd = ["wat2wasm", wat_path, "-o", wasm_path]
    try:
        subprocess.run(build_cmd, check=True)
        print(f"WASM file for {os.path.basename(wat_path)} has been built.")
    except FileNotFoundError:
        print("exec error: wat2wasm is required to build WAT fixtures")
        sys.exit(1)
    except subprocess.CalledProcessError as e:
        print(f"exec error: {e}")
        sys.exit(1)


def process_wat_file(wat_path):
    project_name = os.path.splitext(os.path.basename(wat_path))[0]
    with open(wat_path, "r", encoding="utf8") as f:
        if "(module" not in f.read():
            print(f"Skipping WAT fixture without a module: {project_name}")
            return

    with tempfile.TemporaryDirectory() as tmpdir:
        wasm_path = os.path.join(tmpdir, f"{project_name}.wasm")
        wat2Wasm(wat_path, wasm_path)
        update_fixture(project_name, read_wasm_hex(wasm_path), "Hex")


def process_wat_zip(zip_path):
    project_name = os.path.splitext(os.path.basename(zip_path))[0]
    with tempfile.TemporaryDirectory() as tmpdir:
        with zipfile.ZipFile(zip_path) as archive:
            wat_names = [name for name in archive.namelist() if name.endswith(".wat")]
            if len(wat_names) != 1:
                print(f"exec error: expected one .wat file in {zip_path}")
                sys.exit(1)
            archive.extract(wat_names[0], tmpdir)

        wasm_path = os.path.join(tmpdir, f"{project_name}.wasm")
        wat2Wasm(os.path.join(tmpdir, wat_names[0]), wasm_path)
        update_fixture(project_name, read_wasm_hex(wasm_path), "Hex")


def process_wat(project_name):
    base_path = os.path.dirname(__file__)
    candidates = [
        os.path.join(base_path, f"{project_name}.wat"),
        os.path.join(base_path, "wat", f"{project_name}.wat"),
        os.path.join(base_path, "wat", f"{project_name}.zip"),
    ]
    for path in candidates:
        if os.path.isfile(path):
            if path.endswith(".zip"):
                process_wat_zip(path)
            else:
                process_wat_file(path)
            return

    print(f"exec error: fixture {project_name} not found")
    sys.exit(1)


if __name__ == "__main__":
    if len(sys.argv) > 2:
        print("Usage: python copyFixtures.py [<project_name>]")
        sys.exit(1)
    if len(sys.argv) == 2:
        project_name = os.path.splitext(os.path.basename(sys.argv[1]))[0]
        if os.path.isdir(os.path.join(os.path.dirname(__file__), project_name)):
            process_rust(project_name)
        elif os.path.isfile(
            os.path.join(os.path.dirname(__file__), f"{project_name}.c")
        ):
            process_c(project_name)
        else:
            process_wat(project_name)
        print("Fixture has been processed.")
    else:
        base_path = os.path.dirname(__file__)
        dirs = [
            d
            for d in os.listdir(base_path)
            if os.path.isfile(os.path.join(base_path, d, "Cargo.toml"))
        ]
        c_files = [f for f in os.listdir(base_path) if f.endswith(".c")]
        wat_files = [f for f in os.listdir(base_path) if f.endswith(".wat")]
        wat_path = os.path.join(base_path, "wat")
        wat_fixture_files = [
            f for f in os.listdir(wat_path) if f.endswith((".wat", ".zip"))
        ]

        for d in sorted(dirs):
            process_rust(d)
        for c in sorted(c_files):
            process_c(c[:-2])
        for wat in sorted(wat_files):
            process_wat_file(os.path.join(base_path, wat))
        for wat_fixture in sorted(wat_fixture_files):
            path = os.path.join(wat_path, wat_fixture)
            if wat_fixture.endswith(".zip"):
                process_wat_zip(path)
            else:
                process_wat_file(path)
        print("All fixtures have been processed.")
