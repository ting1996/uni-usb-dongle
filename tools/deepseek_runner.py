#!/usr/bin/env python3
"""Lean DeepSeek V4.1-Flash (deepseek-flash) runner for implementation and static review."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple

DEFAULT_BASE_URL = "https://api.deepseek.com/v1"
DEFAULT_MODEL = "deepseek-flash"
API_KEY_ENV = "DEEPSEEK_API_KEY"
BASE_URL_ENV = "DEEPSEEK_BASE_URL"
MODEL_ENV = "DEEPSEEK_MODEL"
ALLOWED_DOTENV_KEYS = frozenset({API_KEY_ENV, BASE_URL_ENV, MODEL_ENV})
DEFAULT_ENV_FILE = Path(__file__).resolve().parent.parent / ".env"
DEFAULT_MAX_FILE_BYTES = 131072

ROLE_DEFAULTS = {
    "implementer": {"effort": "low", "timeout": 180.0},
    "reviewer": {"effort": "max", "timeout": 300.0},
}

_NAMED_EFFORT_MAP = {
    "none": ("none", {"type": "disabled"}),
    "low": ("low", {"type": "enabled"}),
    "minimal": ("low", {"type": "enabled"}),
    "medium": ("high", {"type": "enabled"}),
    "high": ("high", {"type": "enabled"}),
    "xhigh": ("high", {"type": "enabled"}),
    "max": ("max", {"type": "enabled"}),
    "ultra": ("max", {"type": "enabled"}),
}


def normalize_reasoning_effort(value: str | int) -> Tuple[str, Dict[str, str]]:
    """Map named levels or 0-100 effort percentages to DeepSeek reasoning_effort + thinking."""
    if isinstance(value, bool):
        raise ValueError(f"invalid reasoning effort: {value!r}")

    numeric: Optional[int] = None
    if isinstance(value, int):
        numeric = value
    elif isinstance(value, str):
        cleaned = value.strip().lower()
        if not cleaned:
            raise ValueError("reasoning effort cannot be empty")
        if cleaned in _NAMED_EFFORT_MAP:
            return _NAMED_EFFORT_MAP[cleaned]
        if re.fullmatch(r"[+-]?\d+", cleaned):
            numeric = int(cleaned, 10)
        else:
            raise ValueError(f"invalid reasoning effort: {value!r}")
    else:
        raise ValueError(f"invalid reasoning effort: {value!r}")

    if numeric < 0 or numeric > 100:
        raise ValueError(f"reasoning effort percentage must be 0..100, got {numeric}")
    if numeric == 0:
        return "none", {"type": "disabled"}
    if numeric <= 35:
        return "low", {"type": "enabled"}
    if numeric <= 85:
        return "high", {"type": "enabled"}
    return "max", {"type": "enabled"}


def role_system_prompt(role: str) -> str:
    """Return the system prompt for the scoped DeepSeek worker role."""
    if role == "implementer":
        return (
            "You are the scoped Implementer for uni-usb-dongle (Embedded C11 core, "
            "BL616 FreeRTOS firmware, WebHID UI, and host tools).\n"
            "Rules:\n"
            "1. Strictly follow the Handoff Spec and edit ONLY the Target Files within Scope.\n"
            "2. Honor all Non-Goals / Out-of-Scope boundaries; do NOT refactor or restyle unrelated code.\n"
            "3. Preserve Embedded C11 portability (no FreeRTOS/SDK headers in include/dongle/ or src/core/), "
            "BL616 invariants (configCPU_CLOCK_HZ=1000000UL, xPortIsInsideInterrupt() instead of no-op "
            "portSET_INTERRUPT_MASK_FROM_ISR, <=60B HID reply frames, 2-ACL BT / central-role rules, "
            "M62 slot-0 flash limits), and mandatory FW <-> Web <-> Host-Tool parity when protocol/UI changes.\n"
            "4. Output minimal, self-contained edits using fenced code blocks labeled with the exact "
            "relative file path or clean unified diffs, plus any verification notes."
        )
    if role == "reviewer":
        return (
            "You are the Read-Only Static Code Reviewer for uni-usb-dongle. "
            "Do NOT output rewritten source files or request edits outside the spec.\n"
            "Audit strictly across these axes:\n"
            "1. Spec compliance & scope creep (check every target file, contract, and non-goal).\n"
            "2. Memory safety, bounds/integer overflow, uninitialized state, and resource leaks.\n"
            "3. Concurrency, race conditions, locking/synchronization, and timing/blocking/ISR violations "
            "(BL616 configCPU_CLOCK_HZ=1000000UL, xPortIsInsideInterrupt(), no-op "
            "portSET_INTERRUPT_MASK_FROM_ISR, USB 60B HID reply cap, BT 2-ACL/scatternet rules).\n"
            "4. Error handling, edge cases, and test coverage of failure paths.\n"
            "5. Mandatory FW <-> Web (web/) <-> Host-Tool parity whenever include/dongle/, src/core/, "
            "or firmware/ changes.\n\n"
            "Required Output Format:\n"
            "Verdict: PASS   (or Verdict: FAIL -- MUST be FAIL if ANY CRITICAL or MAJOR defect exists)\n"
            "Followed by numbered findings (if any) in exact format:\n"
            "1. [SEVERITY] path/to/file:line - Root cause. Minimal fix.\n"
            "Use severities CRITICAL, MAJOR, or MINOR only. If there are no findings, write 'None.'."
        )
    raise ValueError(f"unsupported role: {role!r}")


def load_dotenv_values(dotenv_path: Path) -> Dict[str, str]:
    """Load allowed DeepSeek config keys from a local .env file without mutating os.environ."""
    if not dotenv_path.is_file():
        return {}
    values: Dict[str, str] = {}
    for raw_line in dotenv_path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[7:].strip()
        if "=" not in line:
            continue
        key, raw_val = line.split("=", 1)
        key = key.strip()
        if key not in ALLOWED_DOTENV_KEYS:
            continue
        val = raw_val.strip()
        if len(val) >= 2 and val[0] == val[-1] and val[0] in ("'", '"'):
            val = val[1:-1].strip()
        if val:
            values[key] = val
    return values


def _forbidden_input_reason(path_str: str | Path) -> Optional[str]:
    raw = str(path_str).replace("\\", "/").strip()
    if raw.lower().endswith("graphify-out/graph.json"):
        return "never read graphify-out/graph.json into context"
    name_lower = Path(path_str).name.lower()
    if name_lower == ".env" or name_lower.startswith(".env."):
        return ".env files may contain secrets and must not be passed in prompts"
    try:
        resolved = Path(path_str).resolve()
        parts = [p.lower() for p in resolved.parts[-2:]]
        if parts == ["graphify-out", "graph.json"]:
            return "never read graphify-out/graph.json into context"
    except OSError:
        pass
    return None


def _read_text_input(path_str: str) -> str:
    if path_str == "-":
        return sys.stdin.read()
    reason = _forbidden_input_reason(path_str)
    if reason:
        raise ValueError(f"refusing to load {path_str}: {reason}")
    path = Path(path_str)
    if not path.is_file():
        raise FileNotFoundError(f"input file not found: {path_str}")
    return path.read_text(encoding="utf-8", errors="replace")


def _format_numbered_file(path_str: str | Path, max_file_bytes: int) -> str:
    reason = _forbidden_input_reason(path_str)
    if reason:
        raise ValueError(f"refusing to load {path_str}: {reason}")
    path = Path(path_str)
    if not path.is_file():
        raise FileNotFoundError(f"scoped file not found: {path_str}")
    raw = path.read_bytes()
    truncated = False
    total_bytes = len(raw)
    if max_file_bytes > 0 and total_bytes > max_file_bytes:
        raw = raw[:max_file_bytes]
        truncated = True
    text = raw.decode("utf-8", errors="replace")
    numbered_lines = [f"L{idx}: {line}" for idx, line in enumerate(text.splitlines(), start=1)]
    if truncated:
        numbered_lines.append(f"[TRUNCATED: showed {max_file_bytes} of {total_bytes} bytes]")
    display_path = str(path_str).replace("\\", "/")
    return f"### FILE: {display_path}\n" + "\n".join(numbered_lines)


def build_user_prompt(
    spec_text: str,
    files: Sequence[str | Path] = (),
    diff_text: Optional[str] = None,
    errors_text: Optional[str] = None,
    findings_text: Optional[str] = None,
    verification_text: Optional[str] = None,
    max_file_bytes: int = DEFAULT_MAX_FILE_BYTES,
) -> str:
    """Assemble a lean, explicitly delimited prompt for DeepSeek."""
    if not spec_text.strip():
        raise ValueError("handoff spec text cannot be empty")

    sections: List[str] = [f"## HANDOFF SPEC\n{spec_text.strip()}"]

    if findings_text and findings_text.strip():
        sections.append(f"## CRITICAL / MAJOR FINDINGS TO FIX\n{findings_text.strip()}")

    if errors_text and errors_text.strip():
        sections.append(f"## COMPILER / TEST ERRORS\n{errors_text.strip()}")

    if diff_text is not None and diff_text.strip():
        sections.append(f"## GIT DIFF\n```diff\n{diff_text.rstrip()}\n```")

    if verification_text and verification_text.strip():
        sections.append(f"## VERIFICATION OUTPUT\n{verification_text.strip()}")

    if files:
        rendered_files = [_format_numbered_file(f, max_file_bytes=max_file_bytes) for f in files]
        sections.append("## SCOPED FILES\n" + "\n\n".join(rendered_files))

    return "\n\n".join(sections)


def build_request_payload(
    model: str,
    role: str,
    user_prompt: str,
    effort: str | int,
    max_tokens: Optional[int] = None,
) -> Dict[str, Any]:
    """Build the DeepSeek V4.1-Flash Chat Completions payload."""
    normalized_effort, thinking_cfg = normalize_reasoning_effort(effort)
    payload: Dict[str, Any] = {
        "model": model,
        "messages": [
            {"role": "system", "content": role_system_prompt(role)},
            {"role": "user", "content": user_prompt},
        ],
        "reasoning_effort": normalized_effort,
        "thinking": thinking_cfg,
    }
    if max_tokens is not None:
        if max_tokens <= 0:
            raise ValueError(f"max_tokens must be positive, got {max_tokens}")
        payload["max_tokens"] = max_tokens
    return payload


_VERDICT_RE = re.compile(r"(?im)^\s*(?:\*\*)?verdict(?:\*\*)?\s*:\s*(?:\*\*)?(PASS|FAIL)\b")
_NUMBERED_SEVERE_RE = re.compile(r"(?im)^\s*\d+\.\s+.*\b(CRITICAL|MAJOR)\b")
_SEVERITY_FIELD_RE = re.compile(r"(?im)^\s*(?:[-*]\s*)?(?:\*\*)?severity(?:\*\*)?\s*:\s*(?:\*\*)?(CRITICAL|MAJOR)\b")
_TAG_SEVERE_RE = re.compile(r"(?im)^\s*(?:\[(CRITICAL|MAJOR)\]|(CRITICAL|MAJOR)\s*[:\-])")
_NEGATED_SEVERE_RE = re.compile(r"(?i)\b(?:zero|no|0)\s+(?:CRITICAL|MAJOR)\b")


def parse_review_verdict(text: str) -> str:
    """Return 'PASS' or 'FAIL'; enforce FAIL on any CRITICAL/MAJOR finding or missing verdict."""
    for line in text.splitlines():
        if _NEGATED_SEVERE_RE.search(line):
            continue
        if _NUMBERED_SEVERE_RE.search(line) or _SEVERITY_FIELD_RE.search(line) or _TAG_SEVERE_RE.search(line):
            return "FAIL"

    matches = _VERDICT_RE.findall(text)
    if not matches:
        return "FAIL"
    return matches[-1].upper()


def _resolve_git_executable() -> str:
    found = shutil.which("git")
    if found:
        return found
    if os.name == "nt":
        win_git = Path(r"C:\Program Files\Git\cmd\git.exe")
        if win_git.is_file():
            return str(win_git)
    raise FileNotFoundError("git executable not found on PATH or standard Windows install path")


def capture_git_diff(repo_root: Path) -> str:
    """Capture combined unstaged and staged diffs without shell wrappers."""
    git_exe = _resolve_git_executable()
    chunks: List[str] = []
    for label, args in (
        ("unstaged", [git_exe, "-C", str(repo_root), "diff"]),
        ("staged", [git_exe, "-C", str(repo_root), "diff", "--cached"]),
    ):
        proc = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)
        if proc.returncode != 0:
            err = proc.stderr.strip() or f"exit code {proc.returncode}"
            raise RuntimeError(f"git {label} diff failed: {err}")
        if proc.stdout.strip():
            chunks.append(f"# {label}\n{proc.stdout.rstrip()}")
    return "\n\n".join(chunks)


def _redact_secret(text: str, secret: str) -> str:
    if secret:
        return text.replace(secret, "[REDACTED]")
    return text


def invoke_deepseek_api(
    base_url: str,
    api_key: str,
    payload: Dict[str, Any],
    timeout: float,
) -> Dict[str, Any]:
    """Call the DeepSeek OpenAI-compatible Chat Completions endpoint."""
    endpoint = base_url.rstrip("/") + "/chat/completions"
    body = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        endpoint,
        data=body,
        method="POST",
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
            "Accept": "application/json",
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw_body = resp.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as exc:
        detail = ""
        try:
            detail = exc.read().decode("utf-8", errors="replace")[:500]
        except Exception:
            detail = str(exc.reason)
        raise RuntimeError(_redact_secret(f"DeepSeek API HTTP {exc.code}: {detail}", api_key)) from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(_redact_secret(f"DeepSeek API connection error: {exc.reason}", api_key)) from exc
    except Exception as exc:
        raise RuntimeError(_redact_secret(f"DeepSeek API request failed: {exc}", api_key)) from exc

    try:
        parsed = json.loads(raw_body)
        choice = parsed["choices"][0]
        message = choice["message"]
        content = message.get("content") or ""
        reasoning_content = message.get("reasoning_content") or ""
    except (KeyError, IndexError, TypeError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"Unexpected DeepSeek API response format: {exc}") from exc

    return {
        "content": content,
        "reasoning_content": reasoning_content,
        "usage": parsed.get("usage", {}),
        "finish_reason": choice.get("finish_reason"),
    }


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--role", choices=("implementer", "reviewer"), required=True)
    parser.add_argument("--spec", required=True, help="Path to Handoff Spec markdown/text file, or '-' for stdin")
    parser.add_argument("--files", nargs="*", default=(), help="Scoped source/test files to pass")
    parser.add_argument("--diff", help="Path to diff file")
    parser.add_argument("--git-diff", action="store_true", help="Include current git diff (unstaged + staged)")
    parser.add_argument("--errors", help="Path to compiler/test error output for Implementer retry")
    parser.add_argument("--findings", help="Path to CRITICAL/MAJOR findings for Fix Loop")
    parser.add_argument("--verification-output", help="Path to verification command output for Reviewer")
    parser.add_argument("--model", default=None, help=f"Model ID (default: {MODEL_ENV} env/.env or {DEFAULT_MODEL})")
    parser.add_argument("--base-url", default=None, help=f"Base URL (default: {BASE_URL_ENV} env/.env or {DEFAULT_BASE_URL})")
    parser.add_argument("--env-file", help="Path to .env file (default: repo root .env)")
    parser.add_argument("--no-env-file", action="store_true", help="Disable automatic .env loading")
    parser.add_argument("--effort", help="Reasoning effort: none|low|high|max|minimal|medium|xhigh|ultra or 0..100")
    parser.add_argument("--max-tokens", type=int)
    parser.add_argument("--max-file-bytes", type=int, default=DEFAULT_MAX_FILE_BYTES)
    parser.add_argument("--timeout", type=float)
    parser.add_argument("-o", "--output", help="Optional path to write assistant response content")
    parser.add_argument("--save-reasoning", help="Optional path to save reasoning_content (not printed to stdout)")
    parser.add_argument("--dry-run", action="store_true", help="Validate inputs and print redacted config without API call")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_arg_parser()
    args = parser.parse_args(argv)

    defaults = ROLE_DEFAULTS[args.role]
    effort_input = args.effort if args.effort is not None else defaults["effort"]
    timeout_sec = args.timeout if args.timeout is not None else defaults["timeout"]
    if timeout_sec <= 0:
        print("Error: --timeout must be positive", file=sys.stderr)
        return 1

    dotenv_values = {}
    if not args.no_env_file:
        env_path = Path(args.env_file) if args.env_file else DEFAULT_ENV_FILE
        dotenv_values = load_dotenv_values(env_path)

    model = (
        (args.model or "").strip()
        or os.environ.get(MODEL_ENV, "").strip()
        or dotenv_values.get(MODEL_ENV, "").strip()
        or DEFAULT_MODEL
    )
    base_url = (
        (args.base_url or "").strip()
        or os.environ.get(BASE_URL_ENV, "").strip()
        or dotenv_values.get(BASE_URL_ENV, "").strip()
        or DEFAULT_BASE_URL
    )
    api_key = os.environ.get(API_KEY_ENV, "").strip() or dotenv_values.get(API_KEY_ENV, "").strip()

    try:
        spec_text = _read_text_input(args.spec)
        diff_chunks: List[str] = []
        if args.diff:
            diff_chunks.append(_read_text_input(args.diff))
        if args.git_diff:
            git_diff = capture_git_diff(Path.cwd())
            if git_diff:
                diff_chunks.append(git_diff)
        diff_text = "\n\n".join(diff_chunks) if diff_chunks else None
        errors_text = _read_text_input(args.errors) if args.errors else None
        findings_text = _read_text_input(args.findings) if args.findings else None
        verification_text = _read_text_input(args.verification_output) if args.verification_output else None

        user_prompt = build_user_prompt(
            spec_text=spec_text,
            files=args.files,
            diff_text=diff_text,
            errors_text=errors_text,
            findings_text=findings_text,
            verification_text=verification_text,
            max_file_bytes=args.max_file_bytes,
        )
        payload = build_request_payload(
            model=model,
            role=args.role,
            user_prompt=user_prompt,
            effort=effort_input,
            max_tokens=args.max_tokens,
        )
    except (OSError, ValueError, RuntimeError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    if args.dry_run:
        summary = {
            "dry_run": True,
            "endpoint": base_url.rstrip("/") + "/chat/completions",
            "model": payload["model"],
            "role": args.role,
            "reasoning_effort": payload["reasoning_effort"],
            "thinking": payload["thinking"],
            "timeout_sec": timeout_sec,
            "files": [str(f).replace("\\", "/") for f in args.files],
            "has_diff": bool(diff_text and diff_text.strip()),
            "prompt_chars": len(user_prompt),
            "api_key_present": bool(api_key),
        }
        print(json.dumps(summary, indent=2))
        return 0

    if not api_key:
        print(
            f"Error: {API_KEY_ENV} environment variable is not set and not found in .env. "
            "Set it before invoking DeepSeek V4.1-Flash.",
            file=sys.stderr,
        )
        return 1

    try:
        result = invoke_deepseek_api(
            base_url=base_url,
            api_key=api_key,
            payload=payload,
            timeout=timeout_sec,
        )
    except RuntimeError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    if args.save_reasoning:
        Path(args.save_reasoning).write_text(result["reasoning_content"], encoding="utf-8")

    content = result["content"]
    if args.output:
        Path(args.output).write_text(content, encoding="utf-8")
    print(content)

    if args.role == "reviewer":
        verdict = parse_review_verdict(content)
        return 0 if verdict == "PASS" else 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
