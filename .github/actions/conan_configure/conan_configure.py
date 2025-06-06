import os
import shutil
import subprocess
from multiprocessing import cpu_count
from pathlib import Path

conan_version = os.environ.get("CONAN_VERSION", "2.17.0")


def conan_needs_update():
    conan_maj = 2
    output = subprocess.check_output(["conan", "--version"], text=True)
    *_, version = output.strip().split()
    return int(version.split(".")[0]) < conan_maj


install_command = ["install"]

if shutil.which("conan") and conan_needs_update():
    install_command.append("--upgrade")

subprocess.run(["python3", "-m", "pip", *install_command, f"conan~={conan_version}"], check=False)


conanhome = Path(subprocess.check_output(["conan", "config", "home"], text=True).strip())
global_conf = conanhome / "global.conf"
global_defaults = f"""
tools.build:verbosity = verbose
tools.build:jobs = {(n := int(cpu_count() * 9 / 10))}
core.download:parallel = {n}
core.upload:parallel = {n}
"""

if "tools.build:verbosity" not in global_conf.read_text(encoding="utf-8"):
    with global_conf.open("a", encoding="utf-8") as f: f.write(global_defaults)
