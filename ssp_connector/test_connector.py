"""Check production connector authentication, IPC errors, and binary frames."""
import hashlib
import socket
import struct
import subprocess
import sys
import threading

CONNECTOR = sys.argv[1]


def exact(peer, count):
    data = b""
    while len(data) < count:
        chunk = peer.recv(count - len(data))
        assert chunk, "peer closed prematurely"
        data += chunk
    return data


def packet(peer, data):
    peer.sendall(struct.pack(">I", len(data)) + data)


def handshake(peer, reject=False):
    init = bytearray(63)
    init[0] = 0x64
    init[8:13] = b"mock\0"
    init[43:] = bytes(range(20))
    packet(peer, init)
    reply = exact(peer, 44)
    password_hash = bytes.fromhex("41677f4a2155c60561ede4a16842d01ad10e73f5")
    password_hash_hash = bytes.fromhex("0dc527598982f95b581e6164d3e37b6569521eef")
    digest = hashlib.sha1(bytes(range(20)) + password_hash_hash).digest()
    swapped = b"".join(digest[i:i+4][::-1] for i in range(0, 20, 4))
    expected = bytes(a ^ b for a, b in zip(password_hash, swapped))
    assert reply[24:] == expected, "production authentication token mismatch"
    packet(peer, b"\x02" if reject else b"\x01")
    if not reject:
        assert exact(peer, 13)[4] == 0xCA


def messages(data):
    result = []
    while data:
        assert len(data) >= 8
        kind, length = struct.unpack_from("<II", data)
        assert len(data) >= 8 + length
        result.append((kind, data[8:8+length]))
        data = data[8+length:]
    assert result[0] == (8, b""), "ConnectorOk must precede errors and data"
    return result


def run(server):
    failures = []
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(5)
        def serve():
            try:
                with listener.accept()[0] as peer:
                    peer.settimeout(5)
                    server(peer)
            except BaseException as error:
                failures.append(error)
        thread = threading.Thread(target=serve)
        thread.start()
        process = subprocess.Popen([CONNECTOR, "--host", "127.0.0.1", "--port",
                                    str(listener.getsockname()[1])],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            output, error = process.communicate(timeout=8)
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
            thread.join(timeout=6)
        assert not thread.is_alive(), "mock camera did not exit"
        assert not failures, failures
        return messages(output)


def assert_exception(result):
    payload = [payload for kind, payload in result if kind == 7][-1]
    assert len(payload) >= 9, "missing nested exception message"
    code, length = struct.unpack_from("<II", payload)
    assert code != 0 and length == len(payload) - 8
    assert payload[-1:] == b"\0" and length > 1


assert_exception(run(lambda peer: handshake(peer, reject=True)))
assert_exception(run(lambda peer: packet(peer, b"\x64")))

process = subprocess.run([CONNECTOR, "--host", "invalid-address", "--port", "9999"],
                         capture_output=True, timeout=8)
assert process.returncode != 0
assert_exception(messages(process.stdout))

binary = bytes([0, 10, 13, 26, 255, 0, 10])

def stream(peer):
    handshake(peer)
    packet(peer, b"\x6f" + struct.pack(">QII", 123, 5, 7) + binary)
    packet(peer, b"\x70" + struct.pack(">Q", 456) + binary)

result = run(stream)
assert result[1] == (6, b""), "missing authenticated connection event"
video = next(payload for kind, payload in result if kind == 2)
audio = next(payload for kind, payload in result if kind == 3)
size_format = "Q" if struct.calcsize("P") == 8 else "I"
assert video == struct.pack("<QQII" + size_format, 123, 0, 7, 5, len(binary)) + binary
assert audio == struct.pack("<QQ" + size_format, 456, 0, len(binary)) + binary
assert_exception(result)
print("PASS: authentication, rejected handshake, malformed INIT, binary video/audio, EOF exception")
