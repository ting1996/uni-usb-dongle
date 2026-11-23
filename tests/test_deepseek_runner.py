import io
import json
import os
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

from tools import deepseek_runner


class _FakeHttpResponse:
    def __init__(self, body: dict):
        self._raw = json.dumps(body).encode("utf-8")

    def read(self) -> bytes:
        return self._raw

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        return False


class DeepSeekRunnerTests(unittest.TestCase):
    def test_normalize_reasoning_effort_levels_and_numbers(self):
        self.assertEqual(deepseek_runner.normalize_reasoning_effort("none"), ("none", {"type": "disabled"}))
        self.assertEqual(deepseek_runner.normalize_reasoning_effort(0), ("none", {"type": "disabled"}))
        for val in ("low", "minimal", "15", 20, 30, 35):
            self.assertEqual(deepseek_runner.normalize_reasoning_effort(val), ("low", {"type": "enabled"}))
        for val in ("medium", "high", "xhigh", 36, 50, 85):
            self.assertEqual(deepseek_runner.normalize_reasoning_effort(val), ("high", {"type": "enabled"}))
        for val in ("max", "ultra", "100", 86, 100):
            self.assertEqual(deepseek_runner.normalize_reasoning_effort(val), ("max", {"type": "enabled"}))

        for bad in (-1, 101, "101", "invalid", "", True):
            with self.assertRaises(ValueError):
                deepseek_runner.normalize_reasoning_effort(bad)

    def test_build_request_payload_deepseek_flash_contract(self):
        payload = deepseek_runner.build_request_payload(
            model="deepseek-flash",
            role="implementer",
            user_prompt="## HANDOFF SPEC\nFix bounds check",
            effort=25,
            max_tokens=2048,
        )
        self.assertEqual(payload["model"], "deepseek-flash")
        self.assertEqual(payload["reasoning_effort"], "low")
        self.assertEqual(payload["thinking"], {"type": "enabled"})
        self.assertEqual(payload["max_tokens"], 2048)
        self.assertNotIn("temperature", payload)
        self.assertNotIn("presence_penalty", payload)
        self.assertNotIn("frequency_penalty", payload)
        self.assertEqual(payload["messages"][0]["role"], "system")
        self.assertEqual(payload["messages"][1]["role"], "user")

    def test_build_user_prompt_includes_line_numbers_and_blocks_graph_json(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "sample.c"
            target.write_text("int main(void) {\n    return 0;\n}\n", encoding="utf-8")

            prompt = deepseek_runner.build_user_prompt(
                spec_text="Objective: Check main",
                files=[target],
                diff_text="diff --git a/sample.c b/sample.c\n+    return 0;",
                errors_text="sample.c:2: error",
                findings_text="1. [MAJOR] sample.c:2 - fix return",
                verification_text="ctest passed",
            )
            self.assertIn("## HANDOFF SPEC", prompt)
            self.assertIn("L1: int main(void) {", prompt)
            self.assertIn("L2:     return 0;", prompt)
            self.assertIn("## GIT DIFF", prompt)
            self.assertIn("## COMPILER / TEST ERRORS", prompt)
            self.assertIn("## CRITICAL / MAJOR FINDINGS TO FIX", prompt)
            self.assertIn("## VERIFICATION OUTPUT", prompt)

            with self.assertRaises(ValueError):
                deepseek_runner.build_user_prompt(
                    spec_text="Objective: bad read",
                    files=[Path("graphify-out/graph.json")],
                )

    def test_parse_review_verdict(self):
        self.assertEqual(deepseek_runner.parse_review_verdict("Verdict: PASS\nNone."), "PASS")
        self.assertEqual(
            deepseek_runner.parse_review_verdict("Verdict: PASS\nSummary: zero CRITICAL/MAJOR findings."),
            "PASS",
        )
        self.assertEqual(deepseek_runner.parse_review_verdict("Verdict: FAIL\n1. [MINOR] a.c:1 - typo"), "FAIL")
        self.assertEqual(
            deepseek_runner.parse_review_verdict(
                "Verdict: PASS\n1. [CRITICAL] firmware/src/usb.c:42 - Buffer overflow."
            ),
            "FAIL",
        )
        self.assertEqual(
            deepseek_runner.parse_review_verdict(
                "Verdict: PASS\n- Severity: MAJOR\n2. Missing check"
            ),
            "FAIL",
        )
        self.assertEqual(deepseek_runner.parse_review_verdict("Looks okay without explicit verdict"), "FAIL")

    def test_main_dry_run_redacts_secrets_and_skips_network(self):
        with tempfile.TemporaryDirectory() as tmp:
            spec = Path(tmp) / "spec.md"
            spec.write_text("Objective: Dry run test", encoding="utf-8")
            secret = "sk-super-secret-value-12345"
            out = io.StringIO()
            with mock.patch.dict(os.environ, {"DEEPSEEK_API_KEY": secret}, clear=False):
                with mock.patch("urllib.request.urlopen") as urlopen_mock:
                    with redirect_stdout(out):
                        rc = deepseek_runner.main(
                            [
                                "--role",
                                "implementer",
                                "--effort",
                                "20",
                                "--spec",
                                str(spec),
                                "--no-env-file",
                                "--dry-run",
                            ]
                        )
            self.assertEqual(rc, 0)
            urlopen_mock.assert_not_called()
            stdout_text = out.getvalue()
            self.assertNotIn(secret, stdout_text)
            parsed = json.loads(stdout_text)
            self.assertTrue(parsed["dry_run"])
            self.assertEqual(parsed["model"], "deepseek-flash")
            self.assertEqual(parsed["reasoning_effort"], "low")
            self.assertEqual(parsed["thinking"], {"type": "enabled"})
            self.assertTrue(parsed["api_key_present"])

    def test_main_missing_api_key_exits_1(self):
        with tempfile.TemporaryDirectory() as tmp:
            spec = Path(tmp) / "spec.md"
            spec.write_text("Objective: Missing key test", encoding="utf-8")
            err = io.StringIO()
            env_without_key = {k: v for k, v in os.environ.items() if k != "DEEPSEEK_API_KEY"}
            with mock.patch.dict(os.environ, env_without_key, clear=True):
                with mock.patch("urllib.request.urlopen") as urlopen_mock:
                    with redirect_stderr(err):
                        rc = deepseek_runner.main(
                            ["--role", "reviewer", "--spec", str(spec), "--no-env-file"]
                        )
            self.assertEqual(rc, 1)
            urlopen_mock.assert_not_called()
            self.assertIn("DEEPSEEK_API_KEY", err.getvalue())

    def test_dotenv_loading_and_env_precedence(self):
        with tempfile.TemporaryDirectory() as tmp:
            spec = Path(tmp) / "spec.md"
            spec.write_text("Objective: Dotenv precedence test", encoding="utf-8")
            env_file = Path(tmp) / ".env"
            secret = "sk-local-dotenv-secret-xyz"
            env_file.write_text(
                "# comment line\n"
                f"DEEPSEEK_API_KEY={secret}\n"
                "DEEPSEEK_BASE_URL=https://proxy.example.com/v1\n"
                "UNRELATED_KEY=ignored\n",
                encoding="utf-8",
            )
            loaded = deepseek_runner.load_dotenv_values(env_file)
            self.assertEqual(
                loaded,
                {
                    "DEEPSEEK_API_KEY": secret,
                    "DEEPSEEK_BASE_URL": "https://proxy.example.com/v1",
                },
            )

            clean_env = {
                k: v
                for k, v in os.environ.items()
                if k not in ("DEEPSEEK_API_KEY", "DEEPSEEK_BASE_URL", "DEEPSEEK_MODEL")
            }
            out1 = io.StringIO()
            with mock.patch.dict(os.environ, clean_env, clear=True):
                with redirect_stdout(out1):
                    rc1 = deepseek_runner.main(
                        ["--role", "implementer", "--spec", str(spec), "--env-file", str(env_file), "--dry-run"]
                    )
            self.assertEqual(rc1, 0)
            self.assertNotIn(secret, out1.getvalue())
            parsed1 = json.loads(out1.getvalue())
            self.assertEqual(parsed1["endpoint"], "https://proxy.example.com/v1/chat/completions")
            self.assertTrue(parsed1["api_key_present"])

            override_env = dict(clean_env)
            override_env["DEEPSEEK_BASE_URL"] = "https://env-override.example.com/v1"
            out2 = io.StringIO()
            with mock.patch.dict(os.environ, override_env, clear=True):
                with redirect_stdout(out2):
                    rc2 = deepseek_runner.main(
                        ["--role", "implementer", "--spec", str(spec), "--env-file", str(env_file), "--dry-run"]
                    )
            self.assertEqual(rc2, 0)
            parsed2 = json.loads(out2.getvalue())
            self.assertEqual(parsed2["endpoint"], "https://env-override.example.com/v1/chat/completions")

    def test_blocks_reading_dotenv_as_prompt_input(self):
        with tempfile.TemporaryDirectory() as tmp:
            dotenv_file = Path(tmp) / ".env"
            dotenv_file.write_text("DEEPSEEK_API_KEY=secret\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                deepseek_runner.build_user_prompt(
                    spec_text="Objective: bad read",
                    files=[dotenv_file],
                )

    def test_main_implementer_and_reviewer_mocked_api(self):
        with tempfile.TemporaryDirectory() as tmp:
            spec = Path(tmp) / "spec.md"
            spec.write_text("Objective: Mocked API call", encoding="utf-8")
            reasoning_file = Path(tmp) / "reasoning.txt"

            captured_payloads = []

            def _fake_urlopen(req, timeout=None):
                captured_payloads.append(json.loads(req.data.decode("utf-8")))
                if len(captured_payloads) == 1:
                    return _FakeHttpResponse(
                        {
                            "choices": [
                                {
                                    "message": {
                                        "content": "Applied minimal fix",
                                        "reasoning_content": "short chain of thought",
                                    },
                                    "finish_reason": "stop",
                                }
                            ]
                        }
                    )
                if len(captured_payloads) == 2:
                    return _FakeHttpResponse(
                        {
                            "choices": [
                                {
                                    "message": {
                                        "content": "Verdict: PASS\nNone.",
                                        "reasoning_content": "deep audit reasoning",
                                    },
                                    "finish_reason": "stop",
                                }
                            ]
                        }
                    )
                return _FakeHttpResponse(
                    {
                        "choices": [
                            {
                                "message": {
                                    "content": "Verdict: FAIL\n1. [MAJOR] a.c:10 - Null deref. Add null guard.",
                                    "reasoning_content": "found null deref",
                                },
                                "finish_reason": "stop",
                            }
                        ]
                    }
                )

            clean_env = {
                k: v
                for k, v in os.environ.items()
                if k not in ("DEEPSEEK_API_KEY", "DEEPSEEK_BASE_URL", "DEEPSEEK_MODEL")
            }
            clean_env["DEEPSEEK_API_KEY"] = "sk-test-key"
            with mock.patch.dict(os.environ, clean_env, clear=True):
                with mock.patch("urllib.request.urlopen", side_effect=_fake_urlopen):
                    out1 = io.StringIO()
                    with redirect_stdout(out1):
                        rc1 = deepseek_runner.main(
                            [
                                "--role",
                                "implementer",
                                "--effort",
                                "20",
                                "--spec",
                                str(spec),
                                "--no-env-file",
                                "--save-reasoning",
                                str(reasoning_file),
                            ]
                        )
                    self.assertEqual(rc1, 0)
                    self.assertEqual(out1.getvalue().strip(), "Applied minimal fix")
                    self.assertNotIn("short chain of thought", out1.getvalue())
                    self.assertEqual(reasoning_file.read_text(encoding="utf-8"), "short chain of thought")
                    self.assertEqual(captured_payloads[0]["reasoning_effort"], "low")
                    self.assertEqual(captured_payloads[0]["model"], "deepseek-flash")

                    out2 = io.StringIO()
                    with redirect_stdout(out2):
                        rc2 = deepseek_runner.main(
                            ["--role", "reviewer", "--effort", "100", "--spec", str(spec), "--no-env-file"]
                        )
                    self.assertEqual(rc2, 0)
                    self.assertEqual(captured_payloads[1]["reasoning_effort"], "max")

                    out3 = io.StringIO()
                    with redirect_stdout(out3):
                        rc3 = deepseek_runner.main(
                            ["--role", "reviewer", "--effort", "100", "--spec", str(spec), "--no-env-file"]
                        )
                    self.assertEqual(rc3, 2)
                    self.assertEqual(captured_payloads[2]["reasoning_effort"], "max")


if __name__ == "__main__":
    unittest.main()
