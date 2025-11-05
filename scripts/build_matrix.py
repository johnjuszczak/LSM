#!/usr/bin/env python3
import os, sys, json, shutil, subprocess, argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
os.chdir(ROOT)

VERBOSE = False
LOG_FD = None

def emit(message: str):
    if LOG_FD:
        LOG_FD.write(message + "\n")
        LOG_FD.flush()
    else:
        print(message)

def run(cmd, cwd=None):
    try:
        if VERBOSE and LOG_FD:
            emit(f"[cmd] {' '.join(cmd)}")
            return subprocess.run(cmd, cwd=cwd, shell=False, stdout=LOG_FD, stderr=subprocess.STDOUT).returncode
        if VERBOSE:
            emit(f"[cmd] {' '.join(cmd)}")
            return subprocess.run(cmd, cwd=cwd, shell=False).returncode
        if LOG_FD:
            emit(f"[cmd] {' '.join(cmd)}")
            return subprocess.run(cmd, cwd=cwd, shell=False, stdout=LOG_FD, stderr=subprocess.STDOUT).returncode
        return subprocess.run(cmd, cwd=cwd, shell=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode
    except Exception:
        return 1

def has(exe):
    return shutil.which(exe) is not None

def load_presets():
    p = ROOT / "CMakePresets.json"
    if not p.exists():
        return {}, {}, {}
    data = json.loads(p.read_text(encoding="utf-8"))
    cfg = {x["name"]: x for x in data.get("configurePresets", [])}
    bld = {x["name"]: x for x in data.get("buildPresets", [])}
    tst = {x["name"]: x for x in data.get("testPresets", [])}
    return cfg, bld, tst

CFG, BLD, TST = load_presets()

def preset_exists(name, kind="build"):
    return name in (BLD if kind=="build" else (TST if kind=="test" else CFG))

def cmake_configure_preset(name):
    return run(["cmake", "--preset", name], cwd=ROOT)

def cmake_build_preset(name):
    return run(["cmake", "--build", "--preset", name], cwd=ROOT)

def ctest_preset(name):
    return run(["ctest", "--preset", name], cwd=ROOT)

def wsl_path(win_path: Path) -> str:
    p = subprocess.run(["wsl.exe", "-e", "wslpath", "-u", str(win_path)], capture_output=True, text=True)
    if LOG_FD:
        LOG_FD.write(f"[cmd] {' '.join(['wsl.exe', '-e', 'wslpath', '-u', str(win_path)])}\n")
        LOG_FD.write(p.stdout)
        LOG_FD.flush()
    return p.stdout.strip()

def wsl_cmd(script: str):
    return run(["wsl.exe", "-e", "bash", "-lc", script])

def wsl_configure_preset(name, wroot):
    return wsl_cmd(f"cd '{wroot}' && cmake --preset '{name}'")

def wsl_build_preset(name, wroot):
    return wsl_cmd(f"cd '{wroot}' && cmake --build --preset '{name}'")

def wsl_ctest_preset(name, wroot):
    return wsl_cmd(f"cd '{wroot}' && ctest --preset '{name}'")

def want(name, kind="build"):
    return preset_exists(name, kind=kind)

configured = set()
wsl_configured = set()
results = []

parser = argparse.ArgumentParser(
    prog="build_matrix.py",
    description=(
        "Drive CMake build/test presets across toolchains and platforms.\n"
    ),
    formatter_class=argparse.RawTextHelpFormatter,
    epilog=(
        "Examples:\n"
        "  python scripts/build_matrix.py -all        # run all configured presets\n"
        "  python scripts/build_matrix.py -msvc       # only MSVC/ClangCL presets (Windows)\n"
        "  python scripts/build_matrix.py -gcc        # only GCC/g++ presets (Linux/WSL/Windows if present)\n"
        "  python scripts/build_matrix.py -clang      # only Clang/clang++/ClangCL presets\n"
        "  python scripts/build_matrix.py -gcc -clang # both GCC and Clang presets\n"
    )
)
parser.add_argument("-all", action="store_true", help="enable all toolchain groups: msvc, gcc, clang")
parser.add_argument("-msvc", action="store_true", help="enable MSVC/ClangCL (Windows native) presets")
parser.add_argument("-gcc", action="store_true", help="enable GCC/g++ presets (Linux/WSL/Windows if available)")
parser.add_argument("-clang", action="store_true", help="enable Clang/clang++/ClangCL presets")
parser.add_argument("-v", "--verbose", action="store_true", help="print commands and cmake output")
parser.add_argument("-log", metavar="PATH", nargs="?", const="build_matrix.log", help="write output to PATH (default: build_matrix.log)")

args, unknown = parser.parse_known_args()

def enabled(tool: str) -> bool:
    if args.all:
        return True
    if tool == "msvc":
        return args.msvc
    if tool == "gcc":
        return args.gcc
    if tool == "clang":
        return args.clang
    return False

VERBOSE = args.verbose
if args.log:
    log_path = Path(args.log)
    if not log_path.is_absolute():
        log_path = ROOT / log_path
    log_path.parent.mkdir(parents=True, exist_ok=True)
    LOG_FD = log_path.open("w", encoding="utf-8")
else:
    LOG_FD = None

if not (args.all or args.msvc or args.gcc or args.clang):
    parser.print_help(sys.stderr)
    sys.exit(0)

def step(label, fn):
    emit(f"Running {label}...")
    code = fn()
    status = "pass" if code == 0 else "fail"
    emit(f"{label} {status}\n")
    results.append((label, code == 0))
    return code

def ensure_configured(name):
    if not name or name in configured:
        return True
    if not preset_exists(name, kind="configure"):
        emit(f"Missing configure preset '{name}', skipping.")
        results.append((f"{name} configure", False))
        return False
    if step(f"{name} configure", lambda n=name: cmake_configure_preset(n)) == 0:
        configured.add(name)
        return True
    return False

def ensure_wsl_configured(name, wroot):
    if not name or name in wsl_configured:
        return True
    if not preset_exists(name, kind="configure"):
        emit(f"Missing configure preset '{name}' for WSL, skipping.")
        results.append((f"wsl-{name} configure", False))
        return False
    if step(f"wsl-{name} configure", lambda n=name: wsl_configure_preset(n, wroot)) == 0:
        wsl_configured.add(name)
        return True
    return False

def tool_exists_in_wsl(tool):
    return wsl_cmd(f"command -v {tool} >/dev/null 2>&1") == 0

win_builds = []
for std in ("23",):
    for cfg in ("debug","release"):
        win_builds += [
            f"build-win-msvc-{std}-{cfg}",
            f"build-win-clangcl-{std}-{cfg}",
        ]
        if has("g++"):
            win_builds.append(f"build-win-gpp-{std}-{cfg}")
        if has("clang++"):
            win_builds.append(f"build-win-clangpp-{std}-{cfg}")

win_tests = []
for std in ("23",):
    for cfg in ("debug","release"):
        win_tests += [
            f"test-win-msvc-{std}-{cfg}",
            f"test-win-clangcl-{std}-{cfg}",
        ]
        if has("g++"):
            win_tests.append(f"test-win-gpp-{std}-{cfg}")
        if has("clang++"):
            win_tests.append(f"test-win-clangpp-{std}-{cfg}")

linux_builds = [f"build-linux-{comp}-{std}-{cfg}"
                for comp in ("gpp","clangpp")
                for std in ("23",)
                for cfg in ("debug","release")]

linux_tests = [f"test-linux-{comp}-{std}-{cfg}"
               for comp in ("gpp","clangpp")
               for std in ("23",)
               for cfg in ("debug","release")]

try:
    if has("cmake"):
        if os.name == "nt":
            for name in win_builds:
                if ("-msvc-" in name and not enabled("msvc")):
                    continue
                if ("-clangcl-" in name and not enabled("clang")):
                    continue
                if ("-gpp-" in name and not enabled("gcc")):
                    continue
                if ("-clangpp-" in name and not enabled("clang")):
                    continue
                if want(name, "build"):
                    cfg_name = BLD[name].get("configurePreset")
                    if not ensure_configured(cfg_name):
                        continue
                    step(name, lambda n=name: cmake_build_preset(n))
            for name in win_tests:
                if ("-msvc-" in name and not enabled("msvc")):
                    continue
                if ("-clangcl-" in name and not enabled("clang")):
                    continue
                if ("-gpp-" in name and not enabled("gcc")):
                    continue
                if ("-clangpp-" in name and not enabled("clang")):
                    continue
                if want(name, "test"):
                    cfg_name = TST[name].get("configurePreset")
                    if not ensure_configured(cfg_name):
                        continue
                    step(name, lambda n=name: ctest_preset(n))

            if has("wsl.exe"):
                wroot = wsl_path(ROOT)
                if wroot:
                    have_gpp = tool_exists_in_wsl("g++")
                    have_clangpp = tool_exists_in_wsl("clang++")
                    for name in linux_builds:
                        if ("-gpp-" in name and not enabled("gcc")):
                            continue
                        if ("-clangpp-" in name and not enabled("clang")):
                            continue
                        if ("-gpp-" in name and not have_gpp) or ("-clangpp-" in name and not have_clangpp):
                            continue
                        if want(name, "build"):
                            cfg_name = BLD[name].get("configurePreset")
                            if not ensure_wsl_configured(cfg_name, wroot):
                                continue
                            step(f"wsl-{name}", lambda n=name: wsl_build_preset(n, wroot))
                    for name in linux_tests:
                        if ("-gpp-" in name and not enabled("gcc")):
                            continue
                        if ("-clangpp-" in name and not enabled("clang")):
                            continue
                        if ("-gpp-" in name and not have_gpp) or ("-clangpp-" in name and not have_clangpp):
                            continue
                        if want(name, "test"):
                            cfg_name = TST[name].get("configurePreset")
                            if not ensure_wsl_configured(cfg_name, wroot):
                                continue
                            step(f"wsl-{name}", lambda n=name: wsl_ctest_preset(n, wroot))
        else:
            have_gpp = has("g++")
            have_clangpp = has("clang++")
            for name in linux_builds:
                if ("-gpp-" in name and not enabled("gcc")):
                    continue
                if ("-clangpp-" in name and not enabled("clang")):
                    continue
                if ("-gpp-" in name and not have_gpp) or ("-clangpp-" in name and not have_clangpp):
                    continue
                if want(name, "build"):
                    cfg_name = BLD[name].get("configurePreset")
                    if not ensure_configured(cfg_name):
                        continue
                    step(name, lambda n=name: cmake_build_preset(n))
            for name in linux_tests:
                if ("-gpp-" in name and not enabled("gcc")):
                    continue
                if ("-clangpp-" in name and not enabled("clang")):
                    continue
                if ("-gpp-" in name and not have_gpp) or ("-clangpp-" in name and not have_clangpp):
                    continue
                if want(name, "test"):
                    cfg_name = TST[name].get("configurePreset")
                    if not ensure_configured(cfg_name):
                        continue
                    step(name, lambda n=name: ctest_preset(n))
finally:
    emit(f"\nRESULTS:\n")
    for name, ok in results:
        emit(f"{name}: {'pass' if ok else 'fail'}")
    if LOG_FD:
        LOG_FD.close()

if any(not ok for _, ok in results):
    sys.exit(1)
sys.exit(0)
