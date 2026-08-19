import json
import os
import re
import signal
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = Path(__file__).with_name("mutations.json")
BUILD_COMMAND = ["xmake", "build", "-P", ".", "-y", "vmm-tests"]
TEST_COMMAND = [str(ROOT / ".Build/Tests/vmm-tests.exe")]
SUMMARY_PATTERN = re.compile(r"^(\d+)/(\d+) checks passed$")
REQUIRED_FIELDS = {"name", "target", "find", "replace", "check", "message"}
RESTORING = False
DEFERRED_INTERRUPT = False


class MutationFailure(RuntimeError):
    pass


def atomic_write(path: Path, content: bytes) -> None:
    mode = path.stat().st_mode
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.mutation-",
        dir=path.parent,
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(content)
            output.flush()
            os.fsync(output.fileno())
        os.chmod(temporary, mode)
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def restore_target(path: Path, original: bytes, mutated: bytes, entry: dict[str, str]) -> None:
    global DEFERRED_INTERRUPT
    global RESTORING

    RESTORING = True
    try:
        current = path.read_bytes()
        if current == original:
            pass
        elif current == mutated:
            atomic_write(path, original)
        else:
            find = entry["find"].encode("utf-8")
            replacement = entry["replace"].encode("utf-8")
            if current.count(replacement) == 1 and current.count(find) == 0:
                atomic_write(path, current.replace(replacement, find, 1))
                raise MutationFailure(
                    f"{entry['name']}: target changed concurrently; the mutation was removed "
                    f"without overwriting the other edit, but byte-identical restoration is impossible"
                )
            if current.count(find) == 1 and current.count(replacement) == 0:
                raise MutationFailure(
                    f"{entry['name']}: target changed concurrently after the mutation was removed; "
                    f"byte-identical restoration is impossible"
                )
            raise MutationFailure(
                f"{entry['name']}: target changed concurrently and the mutation cannot be removed safely"
            )

        if path.read_bytes() != original:
            raise MutationFailure(
                f"{entry['name']}: byte-identical restoration failed for {entry['target']}"
            )
        print(f"[restore] {entry['target']}: byte-identical", flush=True)
    finally:
        RESTORING = False
        if DEFERRED_INTERRUPT:
            DEFERRED_INTERRUPT = False
            raise KeyboardInterrupt


def run_command(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )


def command_text(command: list[str]) -> str:
    return subprocess.list2cmdline(command)


def require_success(command: list[str], purpose: str) -> str:
    result = run_command(command)
    if result.returncode != 0:
        raise MutationFailure(
            f"{purpose} failed with exit code {result.returncode}: "
            f"{command_text(command)}\n{result.stdout.rstrip()}"
        )
    return result.stdout


def successful_summary(output: str, purpose: str) -> str:
    for line in reversed(output.splitlines()):
        match = SUMMARY_PATTERN.fullmatch(line.strip())
        if match:
            passed, total = match.groups()
            if passed != total:
                break
            return line.strip()
    raise MutationFailure(f"{purpose} did not report a fully green suite\n{output.rstrip()}")


def verify_green(label: str) -> None:
    print(f"[verify] {label}: building vmm-tests", flush=True)
    require_success(BUILD_COMMAND, f"{label} build")
    output = require_success(TEST_COMMAND, f"{label} test run")
    print(f"[pass] {label}: {successful_summary(output, label)}", flush=True)


def load_manifest() -> list[dict[str, str]]:
    entries = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if not isinstance(entries, list) or not entries:
        raise MutationFailure("mutation manifest must be a non-empty array")
    for index, entry in enumerate(entries, start=1):
        if not isinstance(entry, dict) or set(entry) != REQUIRED_FIELDS:
            raise MutationFailure(
                f"manifest entry {index} must contain exactly "
                f"{', '.join(sorted(REQUIRED_FIELDS))}"
            )
        if not all(isinstance(value, str) and value for value in entry.values()):
            raise MutationFailure(f"manifest entry {index} fields must be non-empty strings")
        target = (ROOT / entry["target"]).resolve()
        try:
            target.relative_to(ROOT)
        except ValueError as error:
            raise MutationFailure(
                f"manifest entry {index} target escapes the repository: {entry['target']}"
            ) from error
        if not target.is_file():
            raise MutationFailure(f"manifest entry {index} target does not exist: {entry['target']}")
    return entries


def run_mutation(entry: dict[str, str], index: int, total: int) -> None:
    target = ROOT / entry["target"]
    original = target.read_bytes()
    find = entry["find"].encode("utf-8")
    replacement = entry["replace"].encode("utf-8")
    occurrences = original.count(find)
    if occurrences != 1:
        raise MutationFailure(
            f"{entry['name']}: expected one exact find string in {entry['target']}, "
            f"found {occurrences}"
        )
    mutated = original.replace(find, replacement, 1)
    expected_line = f"[FAIL] {entry['check']}: {entry['message']}"
    restoration_required = False

    print(f"[mutation {index}/{total}] {entry['name']}", flush=True)
    try:
        restoration_required = True
        atomic_write(target, mutated)
        if target.read_bytes() != mutated:
            raise MutationFailure(f"{entry['name']}: mutation write verification failed")

        require_success(BUILD_COMMAND, f"{entry['name']} mutated build")
        result = run_command(TEST_COMMAND)
        lines = [line.strip() for line in result.stdout.splitlines()]
        if result.returncode == 0:
            raise MutationFailure(
                f"{entry['name']}: mutated suite stayed green; expected {expected_line}\n"
                f"{result.stdout.rstrip()}"
            )
        if result.returncode != 1:
            raise MutationFailure(
                f"{entry['name']}: mutated suite exited abnormally with "
                f"{result.returncode}; expected the test failure exit code 1\n"
                f"{result.stdout.rstrip()}"
            )
        if expected_line not in lines:
            raise MutationFailure(
                f"{entry['name']}: expected exact failure line was absent: {expected_line}\n"
                f"{result.stdout.rstrip()}"
            )
        print(f"[pass] {expected_line}", flush=True)
    finally:
        if restoration_required:
            restore_target(target, original, mutated, entry)


def install_interrupt_handler() -> None:
    def interrupt(_signum: int, _frame: object) -> None:
        global DEFERRED_INTERRUPT
        if RESTORING:
            DEFERRED_INTERRUPT = True
            return
        raise KeyboardInterrupt

    signal.signal(signal.SIGINT, interrupt)
    signal.signal(signal.SIGTERM, interrupt)
    if hasattr(signal, "SIGBREAK"):
        signal.signal(signal.SIGBREAK, interrupt)


def main() -> int:
    install_interrupt_handler()
    entries = load_manifest()
    baseline_green = False
    failure: BaseException | None = None

    try:
        verify_green("baseline")
        baseline_green = True
        for index, entry in enumerate(entries, start=1):
            run_mutation(entry, index, len(entries))
    except BaseException as error:
        failure = error
    finally:
        if baseline_green and not isinstance(failure, KeyboardInterrupt):
            try:
                verify_green("restored")
            except BaseException as final_error:
                if failure is None:
                    failure = final_error
                else:
                    failure = MutationFailure(
                        f"{failure}\nrestored-suite verification also failed: {final_error}"
                    )

    if failure is not None:
        raise failure
    print(f"[pass] {len(entries)}/{len(entries)} mutation controls proved", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("[fail] interrupted; active source restoration completed", file=sys.stderr)
        sys.exit(130)
    except Exception as error:
        print(f"[fail] {error}", file=sys.stderr)
        sys.exit(1)
