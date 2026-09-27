from __future__ import annotations

import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/r3'))
from r3lib import w2_build_validation as v


class IdentityFixtures(unittest.TestCase):
    def test_status_preserves_index_and_worktree_columns(self):
        self.assertEqual(v.parse_status(b' M firmware/a.c\0?? tests/new file.py\0'),
                         {'firmware/a.c': ' M', 'tests/new file.py': '??'})

    def test_empty_status(self):
        self.assertEqual(v.parse_status(b''), {})

    def test_unterminated_status_rejected(self):
        with self.assertRaises(v.ValidationError):
            v.parse_status(b' M a.c')

    def test_malformed_status_rejected(self):
        with self.assertRaises(v.ValidationError):
            v.parse_status(b'M broken\0')

    def test_rename_rejected(self):
        with self.assertRaises(v.ValidationError):
            v.parse_status(b'R  b.c\0a.c\0')

    def test_unmerged_rejected(self):
        with self.assertRaises(v.ValidationError):
            v.parse_status(b'UU a.c\0')

    def test_duplicate_path_rejected(self):
        with self.assertRaises(v.ValidationError):
            v.parse_status(b' M a.c\0 M a.c\0')

    def test_frozen_scope_is_6_plus_5_and_unstaged(self):
        self.assertEqual(len(v.CANDIDATE_SHA256), 6)
        self.assertEqual(len(v.HOST_PATHS), 5)
        self.assertEqual(len(v.EXPECTED_DIRTY), 11)
        self.assertEqual(set(v.EXPECTED_DIRTY.values()), {' M', '??'})
        self.assertTrue(all(len(s) == 64 for s in v.CANDIDATE_SHA256.values()))

    def test_snapshot_changed_file_rejected(self):
        before = {'head': 'same', 'files': {'a': 'hash1'}}
        after = copy.deepcopy(before)
        after['files']['a'] = 'hash2'
        with self.assertRaises(v.ValidationError):
            v.validate_snapshot_unchanged(before, after)

    def test_snapshot_changed_index_rejected(self):
        with self.assertRaises(v.ValidationError):
            v.validate_snapshot_unchanged({'index': 'a'}, {'index': 'b'})

    def test_snapshot_unchanged(self):
        v.validate_snapshot_unchanged({'head': 'a'}, {'head': 'a'})

    def test_git_mutation_and_network_commands_rejected(self):
        for command in ('add', 'commit', 'push', 'reset', 'clean', 'fetch', 'ls-remote'):
            with self.assertRaises(v.ValidationError):
                v.git_read(Path('.'), command)


class ArtifactFixtures(unittest.TestCase):
    def test_historical_profiles_exactly_13_and_hw_off(self):
        rows = v.historical_profiles()
        self.assertEqual(set(rows), set(v.HISTORICAL_HASHES))
        self.assertEqual(len(rows), 13)
        for options in rows.values():
            self.assertEqual(options['STREAM_LAB_R3_W2_HW'], 'OFF')
            self.assertEqual(options['STREAM_LAB_R3_WORKER_TASKS'], 'OFF')
            self.assertEqual(options['STREAM_LAB_R3_W2_HW_CASE'], '')

    def test_hardware_cases_exactly_5_and_authority_off(self):
        self.assertEqual(list(v.CASES.values()), [1, 2, 3, 4, 5])
        for selector in v.CASES:
            opts = v.hardware_definitions(selector)
            self.assertEqual(opts['STREAM_LAB_FOUNDATION_QUEUE_ADAPTER'], 'OFF')
            self.assertEqual(opts['STREAM_LAB_R3_WORKER_TASKS'], 'ON')
            self.assertEqual(opts['STREAM_LAB_R3_WORKER_CONTRACT'], 'ON')

    def test_negative_matrix_has_six_distinct_rejections(self):
        rows = v.negative_profiles()
        self.assertEqual(len(rows), 6)
        self.assertEqual(len({r[0] for r in rows}), 6)
        for _, options, diagnostic in rows:
            self.assertTrue(options)
            self.assertTrue(diagnostic)

    def test_expected_negative_failure(self):
        v.check_negative(1, 'CMake Error at CMakeLists.txt:80 (message):\n  exact\n  reason', 'exact reason')

    def test_success_is_not_negative_pass(self):
        with self.assertRaises(v.ValidationError):
            v.check_negative(0, 'CMake Error exact reason', 'exact reason')

    def test_wrong_negative_failure_reason_rejected(self):
        with self.assertRaises(v.ValidationError):
            v.check_negative(1, 'CMake Error: compiler not found', 'case not supported')

    def test_flag_boundaries(self):
        self.assertTrue(v.flag_present('gcc -Wall -Werror -DCASE=1U -c f.c', '-Werror'))
        self.assertFalse(v.flag_present('gcc -Werror=something', '-Werror'))
        self.assertTrue(v.flag_present('gcc "-DCASE=1U" -c f.c', '-DCASE=1U'))
        self.assertFalse(v.flag_present('gcc -DCASE=11U', '-DCASE=1U'))

    def test_nm_strong_symbols(self):
        text = ''.join(name + ' T 08000100 20\n' for name in v.REQUIRED_TEXT_SYMBOLS)
        text += 'g_r3_w2_hw_result B 20000100 94\n'
        v.check_symbols(v.parse_nm(text))

    def test_nm_weak_symbol_rejected(self):
        symbols = {name: [('T', 1)] for name in v.REQUIRED_TEXT_SYMBOLS}
        symbols['g_r3_w2_hw_result'] = [('B', 1)]
        symbols['TIM6_DAC_IRQHandler'] = [('W', 1)]
        with self.assertRaises(v.ValidationError):
            v.check_symbols(symbols)

    def test_nm_duplicate_symbol_rejected(self):
        symbols = {name: [('T', 1)] for name in v.SHIM_SYMBOLS}
        symbols[v.SHIM_SYMBOLS[0]].append(('T', 2))
        with self.assertRaises(v.ValidationError):
            v.check_symbols(symbols, object_shim=True)

    def test_nm_malformed_rejected(self):
        with self.assertRaises(v.ValidationError):
            v.parse_nm('nm: could not read archive\n')

    def test_vector_proves_real_handler(self):
        vec = bytearray(400)
        struct.pack_into('<I', vec, (16 + 54) * 4, 0x08001235)
        v.check_vector(bytes(vec), 54, 0x08001234)
        with self.assertRaises(v.ValidationError):
            v.check_vector(bytes(vec), 54, 0x08009998)

    def test_truncated_vector_rejected(self):
        with self.assertRaises(v.ValidationError):
            v.check_vector(b'\0' * 16, 54, 0x08001234)

    def _database(self, root: Path, mixed: bool = False, wrong_id: bool = False) -> tuple[Path, Path]:
        repo, build = root / 'repo', root / 'build'
        build.mkdir()
        rows = []
        flags = '-Wall -Wextra -Werror -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -DSTREAM_LAB_R3_W2_HW=1'
        sources = list(v.RUNTIME_SOURCES) + ['tasks.c', 'queue.c', 'list.c', 'port.c']
        if mixed:
            sources += ['stream_run_authority.c']
        for name in sources:
            f = repo / 'firmware/runtime' / name
            f.parent.mkdir(parents=True, exist_ok=True)
            f.write_text('/* fixture, not a target build */\n', encoding='utf-8')
            rows.append({'file': str(f), 'directory': str(build),
                         'command': f'gcc {flags} -DR3_W2_HW_CASE_ID={2 if wrong_id else 1}U -c {f}'})
        (build / 'compile_commands.json').write_text(json.dumps(rows), encoding='utf-8')
        return repo, build

    def test_compile_database_strict_profile(self):
        with tempfile.TemporaryDirectory() as td:
            repo, build = self._database(Path(td))
            proof = v.check_compile_database(build, repo, selector='T03_A')
            self.assertEqual(proof['case_id'], 1)
            self.assertEqual(len(proof['runtime_commands']), 4)

    def test_compile_database_mixed_authority_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            repo, build = self._database(Path(td), mixed=True)
            with self.assertRaises(v.ValidationError):
                v.check_compile_database(build, repo, selector='T03_A')

    def test_compile_database_wrong_case_id_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            repo, build = self._database(Path(td), wrong_id=True)
            with self.assertRaises(v.ValidationError):
                v.check_compile_database(build, repo, selector='T03_A')

    def test_historical_module_intrusion_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            repo, build = self._database(Path(td))
            with self.assertRaises(v.ValidationError):
                v.check_compile_database(build, repo, selector=None)


class RunnerFixtures(unittest.TestCase):
    def test_normal_stderr_is_not_failure(self):
        with tempfile.TemporaryDirectory() as td:
            log = v.CommandLog(Path(td))
            rc, out, err = log.run([sys.executable, '-c', "import sys;print('OK',file=sys.stderr)"], name='stderr')
            self.assertEqual(rc, 0)
            self.assertEqual(err.rstrip(), 'OK')
            self.assertEqual(out, '')
            self.assertEqual(len(list(Path(td).glob('commands/*/result.json'))), 1)

    def test_failure_output_preserved(self):
        with tempfile.TemporaryDirectory() as td:
            log = v.CommandLog(Path(td))
            with self.assertRaises(v.ValidationError):
                log.run([sys.executable, '-c', "import sys;print('REAL FAILURE',file=sys.stderr);sys.exit(7)"], name='bad')
            paths = list(Path(td).glob('commands/*/result.json'))
            result = json.loads(paths[0].read_text())
            self.assertEqual(result['exit_code'], 7)
            self.assertIn(b'REAL FAILURE', (paths[0].parent / 'stderr.bin').read_bytes())

    def test_expected_failure_is_observable(self):
        with tempfile.TemporaryDirectory() as td:
            rc, _, _ = v.CommandLog(Path(td)).run([sys.executable, '-c', 'raise SystemExit(3)'], name='negative', allow_failure=True)
            self.assertEqual(rc, 3)

    def test_spawn_error_has_report(self):
        with tempfile.TemporaryDirectory() as td:
            log = v.CommandLog(Path(td))
            with self.assertRaises(v.ValidationError):
                log.run([str(Path(td) / 'does-not-exist')], name='missing')
            result = json.loads(next(Path(td).glob('commands/*/result.json')).read_text())
            self.assertIsNotNone(result['spawn_error'])

    def test_timeout_has_report_and_no_auto_retry(self):
        with tempfile.TemporaryDirectory() as td:
            log = v.CommandLog(Path(td))
            with self.assertRaises(v.ValidationError):
                log.run([sys.executable, '-c', 'import time; time.sleep(30)'], name='timeout', timeout=0.05)
            paths = list(Path(td).glob('commands/*/result.json'))
            self.assertEqual(len(paths), 1)
            self.assertTrue(json.loads(paths[0].read_text())['timed_out'])

    def test_reports_cannot_be_overwritten(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / 'report.json'
            v.write_json_new(p, {'result': 'FAIL'})
            with self.assertRaises(FileExistsError):
                v.write_json_new(p, {'result': 'PASS'})
            self.assertEqual(json.loads(p.read_text())['result'], 'FAIL')

    def test_attempt_directories_are_unique(self):
        with tempfile.TemporaryDirectory() as td:
            parent, repo = Path(td) / 'outputs', Path(td) / 'repo'
            a, b = v.create_attempt(parent, repo), v.create_attempt(parent, repo)
            self.assertNotEqual(a, b)

    def test_outputs_inside_repo_forbidden(self):
        with tempfile.TemporaryDirectory() as td:
            repo = Path(td)
            with self.assertRaises(v.ValidationError):
                v.create_attempt(repo / 'build', repo)

    def test_manifest_paths_are_relative(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / 'raw').mkdir()
            (root / 'raw/log.bin').write_bytes(b'log')
            v.manifest_attempt(root)
            text = (root / 'MANIFEST.sha256').read_text()
            self.assertEqual(text, v.digest(b'log') + '  raw/log.bin\n')


if __name__ == '__main__':
    unittest.main()
