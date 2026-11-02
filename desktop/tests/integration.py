"""End-to-end checks against the actual bundled node, on a disposable regtest chain."""
import argparse
import base64
import json
import pathlib
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request


def run(binary):
    with tempfile.TemporaryDirectory(prefix="whippet-regtest-") as directory:
        root = pathlib.Path(directory)
        (root / "whippet.conf").touch()
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        args = [str(pathlib.Path(binary).resolve()), "-regtest", "-server", "-listen=0", "-discover=0",
                f"-datadir={root}", f"-rpcport={port}", "-rpcbind=127.0.0.1", "-rpcallowip=127.0.0.1"]
        process = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))

        def call(method, params=None):
            cookie = (root / "regtest" / ".cookie").read_bytes().strip()
            request = urllib.request.Request(f"http://127.0.0.1:{port}/",
                data=json.dumps({"jsonrpc": "1.0", "id": 1, "method": method, "params": params or []}).encode(),
                headers={"Authorization": "Basic " + base64.b64encode(cookie).decode(), "Content-Type": "application/json"})
            try:
                response = urllib.request.urlopen(request, timeout=120)
            except urllib.error.HTTPError as error:
                response = error
            result = json.load(response)
            if result.get("error"):
                raise RuntimeError(result["error"])
            return result["result"]

        try:
            deadline = time.monotonic() + 40
            while True:
                try:
                    info = call("getblockchaininfo")
                    break
                except (OSError, RuntimeError):
                    if process.poll() is not None:
                        raise RuntimeError((root / "regtest" / "debug.log").read_text(errors="replace")[-3000:])
                    if time.monotonic() > deadline:
                        raise RuntimeError("Node startup timed out")
                    time.sleep(0.2)
            assert info["chain"] == "regtest", info
            address = call("getnewaddress", ["Integration"])
            assert call("validateaddress", [address])["isvalid"]
            assert not call("validateaddress", ["not-an-address"])["isvalid"]
            # Exercise both sides of the regtest AuxPoW activation at height 20.
            for height in range(1, 66):
                result = call("generatetoaddress", [1, address, 1_000_000, int(height >= 20)])
                assert len(result) == 1, (height, result)
            assert call("getblockcount") == 65
            assert call("getwalletinfo")["balance"] > 0
            recipient = call("getnewaddress", ["Payment"])
            raw = call("createrawtransaction", [[], {recipient: 1.25}])
            funded = call("fundrawtransaction", [raw, {"lockUnspents": False}])
            assert funded["fee"] > 0
            signed = call("signrawtransaction", [funded["hex"]])
            assert signed["complete"]
            txid = call("sendrawtransaction", [signed["hex"]])
            assert call("gettransaction", [txid])["confirmations"] == 0
            call("generatetoaddress", [1, address, 1_000_000, 1])
            assert call("gettransaction", [txid])["confirmations"] >= 1
            backup = root / "backup.dat"
            call("backupwallet", [str(backup)])
            assert (root / "regtest" / "backups" / backup.name).stat().st_size > 0
            assert call("listlockunspent") == []
            call("stop")
            process.wait(timeout=30)
            assert process.returncode == 0
            print(json.dumps({"passed": True, "blocks": 66, "auxpow_boundary": 20,
                              "payment": "funded, signed, broadcast, confirmed", "backup": True, "clean_shutdown": True}))
        finally:
            if process.poll() is None:
                try:
                    call("stop")
                    process.wait(timeout=30)
                except Exception:
                    # This process and directory are owned disposable regtest fixtures only.
                    process.terminate()
                    process.wait(timeout=10)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("node")
    run(parser.parse_args().node)
