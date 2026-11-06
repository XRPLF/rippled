"""Exercise the validation-only CLI against a built xrpld executable."""

import http.client
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest

XRPLD = str(Path(sys.argv.pop(1)).resolve())


class CheckConfig(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.config = self.root / "xrpld.cfg"
        self.contents = """[server]
port_rpc
[port_rpc]
ip=127.0.0.1
port=5005
protocol=http
[node_db]
type=NuDB
path=node-db
[database_path]
database
[debug_logfile]
logs/debug.log
"""
        self.config.write_text(self.contents)

    def check(self, expected=0, message=None, args=(), explicit=True):
        before = self.snapshot()
        result = subprocess.run(
            [XRPLD, "--check-config"]
            + (["--conf", str(self.config)] if explicit else [])
            + list(args),
            cwd=self.root,
            capture_output=True,
            text=True,
            timeout=20,
        )
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        if message:
            self.assertIn(message, result.stdout + result.stderr)
        self.assertEqual(self.snapshot(), before, "Validation modified node state")
        return result

    def snapshot(self):
        return {
            str(p.relative_to(self.root)): (
                (p.read_bytes(), p.stat().st_mtime_ns) if p.is_file() else None
            )
            for p in self.root.rglob("*")
        }

    def test_valid(self):
        self.check(message="Configuration is valid")

    def test_standalone_lifecycle(self):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            port = listener.getsockname()[1]
        self.config.write_text(
            self.contents.replace("port=5005", f"port={port}\nadmin=127.0.0.1").replace(
                "type=NuDB", "type=NuDB\nfast_load=1"
            )
        )
        self.check(args=("--standalone",))

        def rpc(method, **params):
            connection = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
            try:
                connection.request(
                    "POST",
                    "/",
                    json.dumps({"method": method, "params": [params]}),
                    {"Content-Type": "application/json"},
                )
                response = connection.getresponse()
                self.assertEqual(response.status, 200)
                result = json.loads(response.read())["result"]
                self.assertEqual(result["status"], "success", result)
                return result
            finally:
                connection.close()

        def start(*args):
            log = (self.root / "server-output.log").open("a")
            self.addCleanup(log.close)
            process = subprocess.Popen(
                [XRPLD, "--conf", str(self.config), "--standalone", *args],
                cwd=self.root,
                stdout=log,
                stderr=subprocess.STDOUT,
            )

            def cleanup():
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)

            self.addCleanup(cleanup)
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                self.assertIsNone(
                    process.poll(), (self.root / "server-output.log").read_text()
                )
                try:
                    rpc("ping")
                    return process
                except (OSError, http.client.HTTPException):
                    time.sleep(0.1)
            self.fail((self.root / "server-output.log").read_text())

        # --load uses persistent SQLite files in standalone mode; fast_load
        # falls back to a genesis ledger when these databases are still empty.
        process = start("--load")
        self.assertEqual(rpc("server_info")["info"]["server_state"], "full")
        rpc("ledger_accept")
        closed = rpc("ledger_closed")
        # A live node owns the database locks and configured listening port.
        # Its own background writes preclude taking a stable snapshot here.
        result = subprocess.run(
            [XRPLD, "--conf", str(self.config), "--standalone", "--check-config"],
            cwd=self.root,
            capture_output=True,
            text=True,
            timeout=20,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        rpc("stop")
        self.assertEqual(process.wait(timeout=20), 0)
        self.assertTrue((self.root / "database" / "wallet.db").is_file())
        self.assertTrue((self.root / "node-db").is_dir())
        self.check(args=("--standalone",))

        process = start("--load")
        ledger = rpc("ledger", ledger_hash=closed["ledger_hash"])
        self.assertEqual(ledger["ledger_hash"], closed["ledger_hash"])
        rpc("stop")
        self.assertEqual(process.wait(timeout=20), 0)
        self.check(args=("--standalone",))

    def test_default_config(self):
        self.check(explicit=False)

    def test_default_database_path(self):
        self.config.write_text(self.contents.replace("[database_path]\ndatabase\n", ""))
        self.check()

    def test_legacy_config(self):
        self.config.rename(self.root / "rippled.cfg")
        self.check(explicit=False)

    def test_existing_state_unchanged(self):
        for name in ("database/wallet.db", "node-db/data.dat", "logs/debug.log"):
            p = self.root / name
            p.parent.mkdir(exist_ok=True)
            p.write_bytes(b"existing node state\x00\xff")
        self.check()

    def test_does_not_bind_port(self):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen()
            self.config.write_text(
                self.contents.replace("port=5005", f"port={listener.getsockname()[1]}")
            )
            self.check()

    def test_missing_config(self):
        self.config.unlink()
        self.check(1, "Failed to read")

    def test_config_is_directory(self):
        self.config.unlink()
        self.config.mkdir()
        self.check(1, "Failed to read")

    def test_invalid_config(self):
        cases = (
            ("port=5005", "port=0", "port"),
            ("port=5005", "port=70000", "port"),
            ("ip=127.0.0.1", "ip=invalid", "ip"),
            ("protocol=http", "protocol=", "protocol"),
            ("[port_rpc]", "[other_port]", "port_rpc"),
            ("type=NuDB", "type=unknown", "type"),
            ("path=node-db", "nudb_block_size=5000", "nudb_block_size"),
            ("path=node-db", "", "Missing path"),
            ("type=NuDB", "type=NuDB\nonline_delete=1", "online_delete"),
            (
                "type=NuDB",
                "type=NuDB\nonline_delete=512\nrecovery_wait_seconds=0",
                "recovery_wait_seconds",
            ),
        )
        for old, new, error in cases:
            with self.subTest(value=new):
                self.config.write_text(self.contents.replace(old, new))
                self.check(1, error)

    def test_invalid_sections(self):
        cases = (
            ("[features]\nUnknownFeature", "Unknown feature"),
            ("[overlay]\nip_limit=-1", "IP limit"),
            ("[validators]\ninvalid", "validator"),
            ("[validator_token]\ninvalid", "validator"),
            ("[validation_seed]\n", "validation_seed"),
            ("[validator_key_revocation]\ninvalid", "validator"),
            ("[cluster_nodes]\ninvalid", "cluster"),
            ("[validators_file]\nmissing.txt", "missing.txt"),
            ("[ssl_verify_file]\nmissing.pem", "Configuration validation failed"),
            ("[ssl_verify_dir]\nmissing-certs", "ssl_verify_dir"),
            ("[sqlite]\npage_size=513", "page_size"),
            ("[port_grpc]\nip=invalid\nport=50051", "grpc"),
            (
                "[port_grpc]\nip=127.0.0.1\nport=50051\nssl_cert=missing.crt",
                "TLS",
            ),
            (
                "[port_grpc]\nip=127.0.0.1\nport=50051\n"
                "ssl_cert=missing.crt\nssl_key=missing.key",
                "TLS",
            ),
        )
        for section, error in cases:
            with self.subTest(section=section):
                self.config.write_text(self.contents + "\n" + section + "\n")
                self.check(1, error)

    def test_invalid_amendments(self):
        for section in ("amendments", "veto_amendments"):
            with self.subTest(section=section):
                self.config.write_text(self.contents + f"\n[{section}]\ninvalid\n")
                self.check(1, section)

    def test_valid_amendments(self):
        amendment = "0" * 64 + " TestAmendment"
        self.config.write_text(
            self.contents + f"\n[amendments]\n{amendment}\n"
            f"[veto_amendments]\n{amendment}\n"
        )
        self.check()

    def test_relative_validators_file(self):
        (self.root / "validators.txt").write_text("[validators]\n")
        self.config.write_text(self.contents + "\n[validators_file]\nvalidators.txt\n")
        self.check()

    def test_invalid_validators_file(self):
        (self.root / "validators.txt").write_text("[validators]\ninvalid\n")
        self.check(1, "validator")

    def test_tls_files(self):
        self.config.write_text(
            self.contents.replace(
                "protocol=http",
                "protocol=https\nssl_cert=missing.crt\nssl_key=missing.key",
            )
        )
        self.check(1, "Configuration validation failed")

    def test_invalid_tls_contents(self):
        for name in ("cert.pem", "key.pem"):
            (self.root / name).write_text("not PEM data\n")
        self.config.write_text(
            self.contents.replace(
                "protocol=http", "protocol=https\nssl_cert=cert.pem\nssl_key=key.pem"
            )
        )
        self.check(1, "cert.pem")

    def test_anonymous_tls(self):
        self.config.write_text(self.contents.replace("protocol=http", "protocol=https"))
        self.check()

    def test_invalid_cipher_list(self):
        self.config.write_text(
            self.contents.replace(
                "protocol=http", "protocol=https\nssl_ciphers=invalid"
            )
        )
        self.check(1, "cipher")

    def test_online_delete_does_not_create_state(self):
        self.config.write_text(
            self.contents.replace("type=NuDB", "type=NuDB\nonline_delete=512")
        )
        self.check()

    def test_database_paths_are_directories(self):
        for name, error in (("database", "database_path"), ("node-db", "node_db")):
            with self.subTest(path=name):
                path = self.root / name
                path.write_text("not a directory")
                self.check(1, error)
                path.unlink()

    def test_standalone(self):
        self.config.write_text(
            self.contents.replace("type=NuDB", "type=NuDB\nonline_delete=8")
        )
        self.check(args=("--standalone",))

    def test_grpc_does_not_bind_port(self):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen()
            self.config.write_text(
                self.contents + "\n[port_grpc]\nip=127.0.0.1\n"
                f"port={listener.getsockname()[1]}\n"
            )
            self.check()

    def validator_site(self, uri):
        self.config.write_text(
            self.contents + "\n[validator_list_keys]\n"
            "ED2677ABFFD1B33AC6FBC3062B71F1E8397C1505E1C42C64D11AD1B28FF73F4734\n"
            f"[validator_list_sites]\n{uri}\n"
        )

    def test_invalid_validator_site(self):
        self.validator_site("ftp://example.com/validators")
        self.check(1, "Unsupported scheme")

    def test_missing_local_validator_list(self):
        self.validator_site((self.root / "missing.json").as_uri())
        self.check(1, "Failed to read validator list")

    def test_does_not_fetch_validator_sites(self):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen()
            self.validator_site(
                f"http://127.0.0.1:{listener.getsockname()[1]}/validators"
            )
            self.check()
            listener.settimeout(0.1)
            with self.assertRaises(TimeoutError):
                listener.accept()

    def test_does_not_connect_to_peers(self):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen()
            self.config.write_text(
                self.contents
                + f"\n[ips_fixed]\n127.0.0.1 {listener.getsockname()[1]}\n"
            )
            self.check()
            listener.settimeout(0.1)
            with self.assertRaises(TimeoutError):
                listener.accept()

    def test_conflicting_modes(self):
        for args in (("--vacuum",), ("--unittest=Config",), ("--rpc",), ("stop",)):
            with self.subTest(args=args):
                self.check(1, "--check-config", args=args)


if __name__ == "__main__":
    unittest.main()
