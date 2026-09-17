#!/usr/bin/env python3
"""Read-only project handoff collector. Python 3.9+, standard library only.

Never builds, runs a project script, bootstraps VS, accesses a board, changes
Git state, or performs a network operation. Writes only to a NEW external
capture directory. Captures current files, not just the last Git commit.
"""
import argparse
import csv
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import traceback
import uuid
import zipfile

VERSION = "1.0"
REPORTED_HEAD = "2752c0e915ab4725cc13e400d16fe47b14adfd4e"
REPORTED_VERIFIER = "BF632BB6C59998C5DBF00D36B734154AA261205847329728B13DDCC838C1AA42"
REPORTED_ELF = "301417FD7E57F3B74927286F37359035D8FF932CDEA526775E6FB9F243E4DEAE"
PER_FILE = 32 * 1024 * 1024
TOTAL_COPY = 256 * 1024 * 1024
MAX_BASELINE_FILES = 80

SOURCE_EXT = {
    ".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hh", ".inc", ".s",
    ".asm", ".ld", ".lds", ".ioc", ".cmake", ".ps1", ".psm1", ".psd1",
    ".py", ".bat", ".cmd", ".sh", ".md", ".rst", ".txt", ".json",
    ".yaml", ".yml", ".toml", ".ini", ".cfg", ".conf", ".xml", ".csv",
    ".tsv", ".log", ".diff", ".patch", ".sha256", ".sha1", ".lock",
    ".gdb", ".jlink", ".svd", ".rpt", ".ninja", ".png", ".jpg", ".pdf",
}
BUILD_EXT = {
    ".txt", ".log", ".json", ".yaml", ".yml", ".csv", ".tsv", ".sha256",
    ".diff", ".patch", ".cmake", ".ninja", ".gdb", ".map", ".rpt",
    ".elf", ".hex", ".bin", ".md",
}
BINARY_HASH_ONLY = {".exe", ".o", ".obj", ".a", ".lib", ".pdb", ".dll"}
SPECIAL_NAMES = {
    "cmakelists.txt", "makefile", "gnumakefile", ".gitignore", ".gitattributes",
    ".gitmodules", ".editorconfig", "license", "copying", ".ninja_log",
}
PRUNE = {
    ".git", ".svn", ".hg", ".venv", "venv", "env", "node_modules",
    "__pycache__", ".cache", ".pytest_cache", ".mypy_cache", ".ruff_cache",
    ".vs", ".pio", "cmakefiles", "debug", "release", "relwithdebinfo",
    "out", "dist", ".ssh", ".aws", ".azure", ".kube", ".codex",
}
ENV_NAMES = {
    "PATH", "PATHEXT", "COMSPEC", "INCLUDE", "EXTERNAL_INCLUDE", "LIB",
    "LIBPATH", "__VSCMD_PREINIT_PATH", "VSCMD_VER", "VSCMD_ARG_TGT_ARCH",
    "VSCMD_ARG_HOST_ARCH", "VCTOOLSINSTALLDIR", "VCTOOLSVERSION",
    "VCTOOLSREDISTDIR", "VSINSTALLDIR", "VCINSTALLDIR", "WINDOWSSDKDIR",
    "WINDOWSSDKVERSION", "WINDOWSSDKLIBVERSION", "UNIVERSALCRTSDKDIR",
    "UCRTVERSION", "WINDOWSLIBPATH", "PSMODULEPATH", "TEMP", "TMP",
    "SYSTEMROOT", "PROCESSOR_ARCHITECTURE", "PROCESSOR_ARCHITEW6432",
    "OS", "CC", "CXX", "AS", "AR", "CL", "_CL_", "LINK", "_LINK_",
    "CFLAGS", "CXXFLAGS", "LDFLAGS", "CMAKE_GENERATOR", "CMAKE_PREFIX_PATH",
    "CMAKE_TOOLCHAIN_FILE", "CMAKE_BUILD_PARALLEL_LEVEL",
}
TOOL_NAMES = [
    "git.exe", "cl.exe", "link.exe", "cmake.exe", "ctest.exe", "ninja.exe",
    "arm-none-eabi-gcc.exe", "arm-none-eabi-gdb.exe", "python.exe", "py.exe",
    "uv.exe", "STM32_Programmer_CLI.exe", "ST-LINK_gdbserver.exe",
]


def now():
    return dt.datetime.now(dt.timezone.utc).isoformat()


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest().upper()


def inside(path, root):
    try:
        Path(path).resolve().relative_to(Path(root).resolve())
        return True
    except ValueError:
        return False


def is_link(path):
    s = Path(path).lstat()
    return stat.S_ISLNK(s.st_mode) or bool(
        getattr(s, "st_file_attributes", 0) & 0x400
    )


def sensitive(rel):
    parts = Path(rel).parts
    for part in parts:
        p = part.lower()
        if p in {".ssh", ".aws", ".azure", ".kube", "secrets", "credentials"}:
            return True
        if p == ".env" or p.startswith(".env."):
            return True
        if re.match(r"^(secrets?|credentials?|tokens?)(\.|$)", p):
            return True
        if p in {"id_rsa", "id_ed25519", ".netrc", ".npmrc", ".pypirc"}:
            return True
    return Path(rel).suffix.lower() in {".pem", ".key", ".pfx", ".p12", ".kdbx"}


def nul_paths(data):
    return [x.decode("utf-8", "surrogateescape") for x in data.split(b"\0") if x]


class Capture:
    def __init__(self, repo, parent):
        self.repo = repo.resolve()
        self.parent = parent.resolve()
        if inside(self.parent, self.repo):
            raise RuntimeError("Output parent must be OUTSIDE the repository.")
        self.git = shutil.which("git.exe") or shutil.which("git")
        if not self.git:
            raise RuntimeError("git executable not found. No repair was attempted.")
        self.root = self.parent / (
            "stm32-handoff-" + dt.datetime.now().strftime("%Y%m%d-%H%M%S")
            + "-" + uuid.uuid4().hex[:8]
        )
        self.root.mkdir(parents=True, exist_ok=False)
        self.issues = []
        self.inventory = []
        self.copies = []
        self.used = 0
        self.counter = 0
        self.commands = []
        self.changed_during_capture = []
        self.tracked = set()
        self.untracked = set()

    def write(self, rel, value):
        p = self.root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(value, bytes):
            p.write_bytes(value)
        elif isinstance(value, str):
            with p.open("w", encoding="utf-8", newline="") as f:
                f.write(value)
        else:
            p.write_text(json.dumps(value, indent=2, ensure_ascii=True), encoding="utf-8")
        return p

    def issue(self, kind, path, detail):
        self.issues.append({"kind": kind, "path": str(path), "detail": str(detail)})

    def git_read(self, label, args, required=True):
        """Direct git.exe invocation: NO cmd.exe; streams preserved as raw bytes."""
        self.counter += 1
        base = "git/commands/{:03d}-{}".format(self.counter, label)
        out = self.write(base + ".stdout", b"")
        err = self.write(base + ".stderr", b"")
        argv = [self.git, "--no-optional-locks", "--no-pager", "--literal-pathspecs", "-c",
                "core.fsmonitor=false", "-c", "color.ui=false", "-c",
                "core.quotePath=false", "-c", "i18n.logOutputEncoding=UTF-8",
                "-C", str(self.repo)] + list(args)
        env = os.environ.copy()
        env.update(GIT_OPTIONAL_LOCKS="0", GIT_TERMINAL_PROMPT="0",
                   GIT_NO_LAZY_FETCH="1", GIT_ALLOW_PROTOCOL="")
        record = {"argv": argv, "started_utc": now(), "exit_code": None,
                  "stdout": base + ".stdout", "stderr": base + ".stderr"}
        try:
            with out.open("wb") as stdout, err.open("wb") as stderr:
                run = subprocess.run(argv, shell=False, stdin=subprocess.DEVNULL,
                                     stdout=stdout, stderr=stderr, env=env,
                                     timeout=45, check=False)
            record["exit_code"] = run.returncode
        except (OSError, subprocess.TimeoutExpired) as exc:
            record["exception"] = str(exc)
        record["finished_utc"] = now()
        self.commands.append(record)
        self.write(base + ".json", record)
        if record["exit_code"] != 0:
            self.issue("READ_ERROR" if required else "OPTIONAL_GIT_RESULT", label, record)
            if required:
                raise RuntimeError("Git read failed: {}; preserved in {}".format(label, base))
        return out.read_bytes()

    def state(self, label):
        head = self.git_read(label + "-head", ["rev-parse", "HEAD"]).decode().strip()
        status_bytes = self.git_read(label + "-status", ["status", "--porcelain=v1", "-z", "--untracked-files=all"])
        self.git_read(label + "-status-readable", ["status", "--short", "--branch", "--untracked-files=all"])
        refs = self.git_read(label + "-refs", ["for-each-ref", "--format=%(refname)%09%(objectname)%09%(*objectname)", "refs/heads", "refs/remotes", "refs/tags"])
        tracked = self.git_read(label + "-tracked", ["ls-files", "--cached", "-z"])
        untracked = self.git_read(label + "-untracked", ["ls-files", "--others", "--exclude-standard", "-z"])
        index_name = self.git_read(label + "-index-path", ["rev-parse", "--git-path", "index"]).decode("utf-8").strip()
        index = Path(index_name)
        if not index.is_absolute():
            index = self.repo / index
        return {
            "head": head,
            "status_sha256": hashlib.sha256(status_bytes).hexdigest(),
            "refs_sha256": hashlib.sha256(refs).hexdigest(),
            "index_sha256": sha(index) if index.is_file() else None,
            "tracked": nul_paths(tracked), "untracked": nul_paths(untracked),
            "note": "origin/main is a LOCAL cached ref. No network verification occurred.",
        }

    def copy(self, src, dest, category, relative=None):
        rel = relative if relative is not None else str(src)
        row = {"path": str(rel), "category": category, "result": "PENDING"}
        self.inventory.append(row)
        try:
            src = Path(src)
            if sensitive(rel):
                row["result"] = "EXCLUDED_SENSITIVE_NAME"
                return
            if is_link(src) or not src.is_file():
                row["result"] = "EXCLUDED_LINK_OR_NONFILE"
                return
            s = src.stat()
            row.update(bytes=s.st_size, modified_ns=s.st_mtime_ns)
            if s.st_size > PER_FILE or self.used + s.st_size > TOTAL_COPY:
                row["result"] = "OMITTED_SIZE_LIMIT"
                self.issue("COVERAGE_GAP", rel, row["result"])
                return
            before = sha(src)
            target = self.root / dest
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.exists():
                raise RuntimeError("Refusing to overwrite a capture entry: " + str(dest))
            shutil.copyfile(src, target)
            copied = sha(target)
            after = sha(src)
            row.update(result="COPIED", sha256=copied, capture_path=str(dest))
            self.used += s.st_size
            self.copies.append({"source": str(src), "capture_path": str(dest),
                                "sha256": copied, "source_before": before,
                                "source_after_copy": after})
            if not before == copied == after:
                self.changed_during_capture.append(str(src))
        except (OSError, RuntimeError) as exc:
            row.update(result="READ_ERROR", detail=str(exc))
            self.issue("READ_ERROR", rel, exc)

    def walk(self, root, build=False):
        """No symlinks/junctions. In source mode prune caches, not vendor source."""
        root = Path(root)
        if not root.exists():
            return
        if is_link(root):
            self.issue("COVERAGE_GAP", root, "Root is a link/junction; not followed.")
            return
        pending = [root]
        while pending:
            folder = pending.pop()
            try:
                entries = sorted(folder.iterdir(), key=lambda x: x.name.lower())
            except OSError as exc:
                self.issue("READ_ERROR", folder, exc)
                continue
            for p in entries:
                rel = p.relative_to(self.repo).as_posix()
                try:
                    if is_link(p):
                        self.inventory.append({"path": rel, "result": "EXCLUDED_LINK"})
                        continue
                    if p.is_dir():
                        n = p.name.lower()
                        prune = n == ".git" or sensitive(rel)
                        if not build:
                            prune = prune or n in PRUNE or n.startswith("build") or n.startswith("cmake-build-")
                        if prune:
                            self.inventory.append({"path": rel + "/", "result": "PRUNED_DIRECTORY"})
                        else:
                            pending.append(p)
                        continue
                    if not p.is_file():
                        continue
                    ext = p.suffix.lower()
                    allowed = ext in (BUILD_EXT if build else SOURCE_EXT) or p.name.lower() in SPECIAL_NAMES
                    category = "build-evidence" if build else (
                        "tracked" if rel in self.tracked else
                        "untracked" if rel in self.untracked else "ignored-or-extra-source"
                    )
                    if allowed:
                        dest = ("build-evidence/" if build else "working-tree/") + rel
                        self.copy(p, dest, category, rel)
                    else:
                        r = {"path": rel, "category": category, "result": "NOT_COPIED_FILE_TYPE", "bytes": p.stat().st_size}
                        if build and ext in BINARY_HASH_ONLY:
                            r["sha256"] = sha(p)
                            r["result"] = "BINARY_IDENTITY_ONLY"
                        self.inventory.append(r)
                except OSError as exc:
                    self.issue("READ_ERROR", rel, exc)

    def verify_copies(self):
        for row in self.copies:
            try:
                row["source_at_end"] = sha(row["source"])
                row["capture_at_end"] = sha(self.root / row["capture_path"])
                if row["source_at_end"] != row["sha256"] or row["capture_at_end"] != row["sha256"]:
                    self.changed_during_capture.append(row["source"])
            except OSError as exc:
                row["end_check_error"] = str(exc)
                self.changed_during_capture.append(row["source"])

    def finish(self, before, after, fatal=None):
        self.verify_copies()
        checks = {}
        if before and after:
            for k in ("head", "status_sha256", "refs_sha256", "index_sha256", "tracked", "untracked"):
                checks[k + "_unchanged"] = before[k] == after[k]
        consistency = bool(checks) and all(checks.values()) and not self.changed_during_capture
        bad = any(x["kind"] in {"READ_ERROR", "COVERAGE_GAP"} for x in self.issues)
        capture_status = "COMPLETE" if consistency and not bad and not fatal else "PARTIAL"
        architecture = [r["path"] for r in self.inventory if r.get("result") == "COPIED" and re.search(r"Architecture.*v3[._]2[._]2.*Implementation.*Baseline", r["path"], re.I)]
        verifier = self.repo / "tools/r2/verify_w6.ps1"
        verifier_hash = sha(verifier) if verifier.is_file() else None
        summary = {
            "collector_version": VERSION, "finished_utc": now(),
            "repository": str(self.repo), "capture_directory": str(self.root),
            "capture_status": capture_status,
            "consistency_for_captured_scope": consistency, "consistency_checks": checks,
            "changed_during_capture": sorted(set(self.changed_during_capture)),
            "head": after["head"] if after else (before["head"] if before else None),
            "current_verifier_sha256": verifier_hash,
            "matches_last_reported_verifier": verifier_hash == REPORTED_VERIFIER,
            "copied_files": len(self.copies), "copied_bytes": self.used,
            "issue_count": len(self.issues), "architecture_filename_candidates": architecture,
            "fatal_error": fatal,
            "new_test_evidence": "NONE: no compiler, native suite, target build, or hardware run was performed.",
            "limitations": [
                "Not an atomic filesystem snapshot; before/after checks cover the selected files and Git state.",
                "Existing source and logs are copied verbatim; filename-based secret exclusions are not a full secret scan.",
                "No credentials, complete environment dump, Git config dump, shell history, private keys, or remote URLs are requested.",
                "Local paths and selected development environment values remain in this private handoff package.",
                "Previous console stdout cannot be recovered unless already logged; supplied context remains reported evidence.",
                "No board RAM/registers, live timing behavior, external toolchain installation, or full Git object database is captured.",
                "Architecture filename candidates are not proof that the formal specification has been read or approved.",
                "Per-file limit 32 MiB; copied-file budget 256 MiB; omissions are listed, never silently promoted to PASS.",
            ],
        }
        self.write("summary.json", summary)
        self.write("git/before.json", before)
        self.write("git/after.json", after)
        self.write("inventory.json", self.inventory)
        self.write("copy-integrity.json", self.copies)
        self.write("issues.json", self.issues)
        self.write("git/command-index.json", self.commands)
        self.write("START_HERE.txt", (
            "CAPTURE STATUS: " + capture_status + "\n"
            "This is a state-capture result, NOT an R2/W6 test result.\n\n"
            "Read in this order:\n"
            "1. summary.json and reported-context.json\n"
            "2. host/powershell-context.json and host/environment.json\n"
            "3. git/before.json, git/after.json, git/commands/\n"
            "4. working-tree/tools/r2/verify_w6.ps1 (CURRENT exact bytes)\n"
            "5. working-tree/tests/native/ and working-tree/firmware/\n"
            "6. working-tree/docs/ and working-tree/project-journal/\n"
            "7. baseline/ and git/diffs/ (tracked changes only)\n"
            "8. build-evidence/, packages/, inventory.json, issues.json\n\n"
            "Do not replay a patch merely because it appears in the conversation.\n"
            "Check the current file SHA256 and exact content first.\n"
            "Do not infer a native or hardware PASS from this capture.\n"
        ))
        manifest = []
        for p in sorted(self.root.rglob("*")):
            if p.is_file():
                manifest.append(sha(p) + "  " + p.relative_to(self.root).as_posix())
        self.write("manifest.sha256", "\n".join(manifest) + "\n")
        zip_path = self.root.with_suffix(".zip")
        with zipfile.ZipFile(zip_path, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as z:
            for p in sorted(self.root.rglob("*")):
                if p.is_file():
                    z.write(p, p.relative_to(self.root).as_posix())
        with zipfile.ZipFile(zip_path, "r") as z:
            bad_entry = z.testzip()
            if bad_entry:
                raise RuntimeError("Archive CRC check failed: " + bad_entry)
        zip_hash = sha(zip_path)
        zip_path.with_suffix(".zip.sha256.txt").write_text(zip_hash + "  " + zip_path.name + "\n", encoding="ascii")
        print("\n=== STM32 HANDOFF CAPTURE SUMMARY ===")
        print("Capture status: " + capture_status)
        print("Captured-scope consistency: " + ("PASS" if consistency else "NOT PROVEN"))
        print("HEAD: " + str(summary["head"]))
        print("Verifier SHA256: " + str(verifier_hash))
        print("Copied files: " + str(len(self.copies)))
        print("Issues / omissions: " + str(len(self.issues)))
        print("Architecture filename candidates: " + str(len(architecture)))
        print("ZIP CRC check: PASS")
        print("ZIP: " + str(zip_path))
        print("ZIP SHA256: " + zip_hash)
        print("Existing files / logs / build directories: not deleted or repaired")
        print("Native / ARM / hardware tests: NOT RUN BY THIS COLLECTOR")
        return 0 if capture_status == "COMPLETE" else 2


def collect_environment(c, host_context):
    selected = {k: v for k, v in os.environ.items() if k.upper() in ENV_NAMES}
    size_only = sorted([{"name": k, "characters": len(v)} for k, v in os.environ.items()], key=lambda x: -x["characters"])
    path = next((v for k, v in os.environ.items() if k.upper() == "PATH"), "")
    entries = path.split(os.pathsep)
    seen = {}
    rows = []
    for i, p in enumerate(entries):
        key = p.strip().strip('"').lower()
        rows.append({"index": i, "entry": p, "characters": len(p), "duplicate_of": seen.get(key)})
        seen.setdefault(key, i)
    c.write("host/environment.json", {"selected_values": selected, "all_variable_names_and_lengths_only": size_only, "path_characters": len(path), "path_entries": rows})
    c.write("host/python.json", {"executable": sys.executable, "version": sys.version, "platform": sys.platform, "filesystem_encoding": sys.getfilesystemencoding(), "timestamp_utc": now()})
    if host_context:
        c.copy(host_context, "host/powershell-context.json", "host-context")
    else:
        c.issue("MISSING_CONTEXT", "PowerShell parent process", "Context file not supplied.")
    names = list(TOOL_NAMES)
    if os.name != "nt":
        names = ["git", "python3", "cmake", "ninja"]
    tool_paths = {}
    for name in names:
        resolved = shutil.which(name)
        tool_paths[name] = Path(resolved) if resolved else None
    known = {
        "configured-cmake": r"E:\DevTools\cmake-3.31.12-windows-x86_64\bin\cmake.exe",
        "configured-ctest": r"E:\DevTools\cmake-3.31.12-windows-x86_64\bin\ctest.exe",
        "configured-ninja": r"E:\DevTools\ninja-1.13.1-windows-x86_64\ninja.exe",
        "configured-arm-gcc": r"E:\DevTools\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi\bin\arm-none-eabi-gcc.exe",
        "configured-arm-gdb": r"E:\DevTools\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi\bin\arm-none-eabi-gdb.exe",
        "configured-VsDevCmd": r"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat",
    }
    if os.name == "nt":
        tool_paths.update({k: Path(v) for k, v in known.items()})
    records = []
    for name, p in tool_paths.items():
        r = {"name": name, "path": str(p) if p else None, "executed": False}
        try:
            r["exists"] = bool(p and p.is_file())
            if r["exists"]:
                r.update(bytes=p.stat().st_size, sha256=sha(p))
        except OSError as exc:
            r["read_error"] = str(exc)
        records.append(r)
    c.write("host/tool-identities.json", records)


def collect_baselines(c):
    changed = nul_paths(c.git_read("changed-tracked-paths", ["diff", "--no-ext-diff", "--no-textconv", "--name-only", "-z", "HEAD", "--"]))
    c.write("git/changed-tracked-paths.json", changed)
    for i, rel in enumerate(changed):
        if i >= MAX_BASELINE_FILES:
            c.issue("COVERAGE_GAP", rel, "Baseline/diff file-count limit reached.")
            continue
        if sensitive(rel) or not (Path(rel).suffix.lower() in SOURCE_EXT or Path(rel).name.lower() in SPECIAL_NAMES):
            c.issue("EXCLUDED_DIFF", rel, "Excluded by filename/type policy.")
            continue
        if not inside(c.repo / rel, c.repo):
            c.issue("COVERAGE_GAP", rel, "Path escapes repository.")
            continue
        blob = c.git_read("head-file-{:03d}".format(i), ["show", "HEAD:" + rel], required=False)
        if c.commands[-1]["exit_code"] == 0:
            c.write("baseline/HEAD/" + rel, blob)
        for label, args in (
            ("unstaged", ["diff", "--no-ext-diff", "--no-textconv", "--no-color", "--binary", "--full-index", "--", rel]),
            ("staged", ["diff", "--cached", "--no-ext-diff", "--no-textconv", "--no-color", "--binary", "--full-index", "--", rel]),
        ):
            data = c.git_read("{}-{:03d}".format(label, i), args)
            c.write("git/diffs/{:03d}-{}.patch".format(i, label), data)


def collect_builds_and_packages(c):
    build_root = c.repo / "build"
    chosen = []
    if build_root.is_dir() and not is_link(build_root):
        for p in sorted(build_root.iterdir()):
            if p.is_dir():
                chosen.append({"path": p.relative_to(c.repo).as_posix(), "modified_ns": p.stat().st_mtime_ns,
                               "collect_contents": bool(re.match(r"r2[-_]w[56]", p.name, re.I))})
        for row in chosen:
            if row["collect_contents"]:
                c.walk(c.repo / row["path"], build=True)
    c.write("build-evidence/directory-index.json", chosen)
    # Only known project packages. No recursive scan of Downloads or other projects.
    package_root = Path.home() / "Downloads" / "tempa"
    packages = {
        "r2_w6_k_matrix.zip": "92E05DFD771AE6DCC04303C6442D07780734D92A7D6F76940562172CB82CFE0F",
        "r2_w6_preflight_source.zip": "91F69C7434FAB3007A74B8721A5C8FCAE2594CE3EC8451182D54EE1CD9F70EDE",
    }
    records = []
    for name, expected in packages.items():
        p = package_root / name
        r = {"path": str(p), "reported_expected_sha256": expected, "exists": p.is_file()}
        if p.is_file():
            try:
                r["actual_sha256"] = sha(p)
                r["matches_reported_hash"] = r["actual_sha256"] == expected
                c.copy(p, "packages/" + name, "existing-package")
            except OSError as exc:
                c.issue("READ_ERROR", p, exc)
        records.append(r)
    c.write("packages/package-identities.json", records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--output-parent", required=True, type=Path)
    parser.add_argument("--host-context", type=Path)
    args = parser.parse_args()
    if sys.version_info < (3, 9):
        raise RuntimeError("Python 3.9 or later is required; nothing was installed.")
    c = Capture(args.repo, args.output_parent)
    before = after = None
    fatal = None
    print("Capture directory: " + str(c.root), flush=True)
    try:
        root = c.git_read("repository-root", ["rev-parse", "--show-toplevel"]).decode("utf-8").strip()
        if Path(root).resolve() != c.repo:
            raise RuntimeError("--repo must identify the actual Git repository ROOT.")
        print("[1/6] Capturing Git identity and file membership", flush=True)
        before = c.state("before")
        c.tracked = set(before["tracked"])
        c.untracked = set(before["untracked"])
        c.git_read("recent-commits", ["log", "-20", "--date=iso-strict", "--format=%H%x09%ad%x09%s"])
        c.git_read("index-entries", ["ls-files", "--stage"])
        c.git_read("submodules", ["submodule", "status", "--recursive"], required=False)
        c.git_read("format-settings", ["config", "--get-regexp", r"^(core\.(autocrlf|eol|safecrlf|symlinks|longpaths)|i18n\.(commitencoding|logoutputencoding))$"], required=False)
        print("[2/6] Capturing selected host environment and tool identities", flush=True)
        collect_environment(c, args.host_context)
        c.copy(Path(__file__).resolve(), "collector/stm32_handoff_capture.py", "collector-source")
        c.write("reported-context.json", {
            "provenance": "Prior user-supplied conversation; NOT reverified by this collector.",
            "last_hardware_known_good_package": "R2-W5 CLOSED / KNOWN-GOOD",
            "reported_checkpoint": REPORTED_HEAD, "reported_W5_ELF_SHA256": REPORTED_ELF,
            "reported_last_verifier_SHA256": REPORTED_VERIFIER,
            "W6_candidate_installation": "Reported PASS; uncommitted working-tree changes.",
            "Windows_native": "Original attempt failed at MSVC environment setup; 116-test PASS not supplied.",
            "compile_probe": "Reported cl.exe /c exit 0 and probe.obj produced; this is not a link or test-suite result.",
            "latest_action": "Old bootstrap patch was repeated; exact-match guard rejected it and reported no file change.",
            "hypothesis_not_root_cause_proof": "Redundant VsDevCmd initialization with enlarged inherited PATH may cause command-length failure.",
            "R2_overall": "IN PROGRESS; r2-pass acceptance not granted in supplied context.",
            "known_documentation_gap": "docs/status.md and docs/evidence/index.md were stale in supplied output.",
        })
        print("[3/6] Copying current tracked, untracked and generated source files", flush=True)
        c.walk(c.repo)
        captured_rel = {r["path"] for r in c.inventory if r.get("result") == "COPIED" and r.get("category") != "build-evidence"}
        for rel in sorted(set(before["tracked"] + before["untracked"]) - captured_rel):
            # Membership is retained even when content is excluded or deleted.
            c.inventory.append({"path": rel, "result": "GIT_MEMBER_NOT_IN_SOURCE_COPY",
                                "exists": (c.repo / rel).exists()})
        print("[4/6] Capturing HEAD counterparts and staged/unstaged differences", flush=True)
        collect_baselines(c)
        print("[5/6] Preserving existing W5/W6 build logs and original packages", flush=True)
        collect_builds_and_packages(c)
        print("[6/6] Rechecking state and sealing the capture", flush=True)
        after = c.state("after")
    except Exception:
        fatal = traceback.format_exc()
        c.write("fatal-capture-error.txt", fatal)
        print("Capture has an error; preserving partial evidence, not cleaning up.", flush=True)
        if before is not None:
            try:
                after = c.state("after-error")
            except Exception as exc:
                c.issue("READ_ERROR", "final-state", exc)
    return c.finish(before, after, fatal)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception:
        traceback.print_exc()
        print("CAPTURE COULD NOT FINISH. Preserve the printed capture directory.", file=sys.stderr)
        sys.exit(1)
