import os
import sys
import subprocess
import re


def process_project(project_name):
    project_path = os.path.abspath(
        os.path.join(os.path.dirname(__file__), project_name)
    )
    build_cmd = f"(cd {project_path} && cargo build --target wasm32-unknown-unknown --release && wasm-opt target/wasm32-unknown-unknown/release/{project_name}.wasm -Oz -o target/wasm32-unknown-unknown/release/{project_name}.wasm)"
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
            f"{project_name}/target/wasm32-unknown-unknown/release/{project_name}.wasm",
        )
    )
    with open(src_path, "rb") as f:
        data = f.read()
    wasm = data.hex()

    fixture_name = re.sub(r"_([a-z])", lambda m: m.group(1).upper(), project_name)
    print(f"Updating fixture: {fixture_name}")
    dst_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "fixtures.cpp"))
    with open(dst_path, "r", encoding="utf8") as f:
        dst_content = f.read()
    pattern = rf'extern std::string const {fixture_name} =[ \n]+"[^;]*;'
    updated_content = re.sub(
        pattern,
        f'extern std::string const {fixture_name} = "{wasm}";',
        dst_content,
        flags=re.MULTILINE,
    )
    with open(dst_path, "w", encoding="utf8") as f:
        f.write(updated_content)


if __name__ == "__main__":
    if len(sys.argv) > 2:
        print("Usage: python copyFixtures.py [<project_name>]")
        sys.exit(1)
    if len(sys.argv) == 2:
        process_project(sys.argv[1])
    else:
        dirs = [
            d
            for d in os.listdir(os.path.dirname(__file__))
            if os.path.isdir(os.path.join(os.path.dirname(__file__), d))
        ]
        for d in dirs:
            process_project(d)
