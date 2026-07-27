# cspell: disable
import os
import re
import shlex
import subprocess
import sys
import tempfile
import zipfile
from difflib import get_close_matches

OPT = "-Oz"
BASE_PATH = os.path.abspath(os.path.dirname(__file__))


def pascal_case(name):
    return "".join(word[:1].upper() + word[1:] for word in re.split(r"[_\W]+", name))


def normalize_name(name):
    name = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name)
    return re.sub(r"[^a-z0-9]", "", name.lower())


def fixture_key(name):
    name = normalize_name(name).removeprefix("k")
    return name.removesuffix("wasmhex").removesuffix("hex")


def declared_fixtures():
    h_path = os.path.join(BASE_PATH, "fixtures.h")
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
    pattern = rf"extern std::string const {fixture_name} ="
    for file_name in os.listdir(BASE_PATH):
        if not file_name.endswith(".cpp"):
            continue
        cpp_path = os.path.join(BASE_PATH, file_name)
        with open(cpp_path, "r", encoding="utf8") as f:
            if re.search(pattern, f.read()):
                return cpp_path
    return os.path.join(BASE_PATH, "fixtures.cpp")


def update_fixture(project_name, wasm, suffix="WasmHex"):
    fixture_name = find_fixture_name(project_name, suffix)
    print(f"Updating fixture: {fixture_name}")

    cpp_path = fixture_cpp_path(fixture_name)
    h_path = os.path.join(BASE_PATH, "fixtures.h")
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
    project_path = os.path.join(BASE_PATH, project_name)
    wasm_location = os.path.join(
        project_path, "target", "wasm32v1-none", "release", f"{project_name}.wasm"
    )
    try:
        subprocess.run(
            ["cargo", "build", "--target", "wasm32v1-none", "--release"],
            cwd=project_path,
            check=True,
        )
        subprocess.run(
            ["wasm-opt", wasm_location, OPT, "-o", wasm_location], check=True
        )
        print(f"WASM file for {project_name} has been built and optimized.")
    except FileNotFoundError as e:
        print(f"exec error: {e.filename} is required to build Rust fixtures")
        sys.exit(1)
    except subprocess.CalledProcessError as e:
        print(f"exec error: {e}")
        sys.exit(1)

    update_fixture(project_name, read_wasm_hex(wasm_location))


def process_c(project_name):
    project_path = os.path.join(BASE_PATH, f"{project_name}.c")
    wasm_path = os.path.join(BASE_PATH, f"{project_name}.wasm")
    cc = os.environ.get("CC")
    sysroot = os.environ.get("SYSROOT")
    if not cc or not sysroot:
        print("exec error: CC and SYSROOT are required to build C fixtures")
        sys.exit(1)

    build_cmd = [
        *shlex.split(cc),
        f"--sysroot={sysroot}",
        "-O3",
        "-ffast-math",
        "--target=wasm32",
        "-fno-exceptions",
        "-fno-threadsafe-statics",
        "-fvisibility=default",
        "-Wl,--export-all",
        "-Wl,--no-entry",
        "-Wl,--allow-undefined",
        "-DNDEBUG",
        "--no-standard-libraries",
        "-fno-builtin-memset",
        "-o",
        wasm_path,
        project_path,
    ]
    try:
        subprocess.run(build_cmd, check=True)
        subprocess.run(["wasm-opt", wasm_path, OPT, "-o", wasm_path], check=True)
        print(
            f"WASM file for {project_name} has been built with WASI support using clang."
        )
    except FileNotFoundError as e:
        print(f"exec error: {e.filename} is required to build C fixtures")
        sys.exit(1)
    except subprocess.CalledProcessError as e:
        print(f"exec error: {e}")
        sys.exit(1)

    update_fixture(project_name, read_wasm_hex(wasm_path))


def wat_to_wasm(wat_path, wasm_path):
    build_cmd = ["wat2wasm", "--enable-all", wat_path, "-o", wasm_path]
    try:
        subprocess.run(build_cmd, check=True)
        print(f"WASM file for {os.path.basename(wat_path)} has been built.")
        return
    except FileNotFoundError:
        print("exec error: wat2wasm is required to build WAT fixtures")
        sys.exit(1)
    except subprocess.CalledProcessError:
        # wat2wasm (wabt) does not support some proposal text syntax such as
        # the GC instructions, so fall back to wasm-tools which does.
        pass

    fallback_cmd = ["wasm-tools", "parse", wat_path, "-o", wasm_path]
    try:
        subprocess.run(fallback_cmd, check=True)
        print(
            f"WASM file for {os.path.basename(wat_path)} has been built with wasm-tools."
        )
    except FileNotFoundError:
        print("exec error: wasm-tools is required to build this WAT fixture")
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
        wat_to_wasm(wat_path, wasm_path)
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
        wat_to_wasm(os.path.join(tmpdir, wat_names[0]), wasm_path)
        update_fixture(project_name, read_wasm_hex(wasm_path), "Hex")


def process_wat(project_name):
    candidates = [
        os.path.join(BASE_PATH, f"{project_name}.wat"),
        os.path.join(BASE_PATH, "wat", f"{project_name}.wat"),
        os.path.join(BASE_PATH, "wat", f"{project_name}.zip"),
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
        if os.path.isfile(os.path.join(BASE_PATH, project_name, "Cargo.toml")):
            process_rust(project_name)
        elif os.path.isfile(os.path.join(BASE_PATH, f"{project_name}.c")):
            process_c(project_name)
        else:
            process_wat(project_name)
        print("Fixture has been processed.")
    else:
        dirs = [
            d
            for d in os.listdir(BASE_PATH)
            if os.path.isfile(os.path.join(BASE_PATH, d, "Cargo.toml"))
        ]
        c_files = [f for f in os.listdir(BASE_PATH) if f.endswith(".c")]
        wat_files = [f for f in os.listdir(BASE_PATH) if f.endswith(".wat")]
        wat_path = os.path.join(BASE_PATH, "wat")
        wat_fixture_files = [
            f
            for f in (os.listdir(wat_path) if os.path.isdir(wat_path) else [])
            if f.endswith((".wat", ".zip"))
        ]

        for d in sorted(dirs):
            process_rust(d)
        for c in sorted(c_files):
            process_c(c[:-2])
        for wat in sorted(wat_files):
            process_wat_file(os.path.join(BASE_PATH, wat))
        for wat_fixture in sorted(wat_fixture_files):
            path = os.path.join(wat_path, wat_fixture)
            if wat_fixture.endswith(".zip"):
                process_wat_zip(path)
            else:
                process_wat_file(path)
        print("All fixtures have been processed.")
