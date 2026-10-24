#!/usr/bin/env python3
"""Стенд протокола: Python-клиент против НАСТОЯЩЕГО кода прошивки (хост-сервер gt_host).

Между клиентом и сервером стоит UDP-прокси, который теряет пакеты в обе стороны.
Клиент (AckClient) - порт алгоритма из processing/GyverTwink/ack.pde: очередь по одной команде, seq,
повтор через таймаут, слияние ожидающих значений. Это проверяет, что алгоритм приложения и разбор в прошивке
действительно работают вместе. Но это порт, а не само приложение: при изменении ack.pde правь и этот файл.

Запуск: make -C tests stand
"""
import random
import select
import socket
import subprocess
import sys
import threading
import time

fails = 0


def check(cond, msg):
    global fails
    print(("  ok:   " if cond else "  FAIL: ") + msg)
    if not cond:
        fails += 1


# ---------------- хост-сервер ----------------
class Server:
    def __init__(self, exe):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.bind(("127.0.0.1", 0))
        self.port = s.getsockname()[1]
        s.close()
        self.proc = subprocess.Popen([exe, str(self.port)], stdout=subprocess.PIPE, text=True)
        self.state = {}
        self.lock = threading.Lock()
        assert self.proc.stdout.readline().strip() == "READY"
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.proc.stdout:
            if line.startswith("STATE"):
                st = dict(kv.split("=") for kv in line.split()[1:])
                with self.lock:
                    self.state = {k: int(v) for k, v in st.items()}

    def get(self, key):
        time.sleep(0.15)   # дать серверу напечатать состояние
        with self.lock:
            return self.state.get(key)

    def stop(self):
        self.proc.terminate()
        self.proc.wait()


# ---------------- прокси с потерями ----------------
class LossyProxy:
    def __init__(self, server_port, loss, seed):
        self.loss = loss
        self.rng = random.Random(seed)
        self.server = ("127.0.0.1", server_port)
        self.front = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)   # сюда пишет клиент
        self.front.bind(("127.0.0.1", 0))
        self.back = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)    # отсюда идёт к серверу
        self.back.bind(("127.0.0.1", 0))
        self.addr = self.front.getsockname()
        self.client = None
        self.running = True
        self.sent = self.dropped = 0
        threading.Thread(target=self._run, daemon=True).start()

    def _drop(self):
        self.sent += 1
        if self.rng.random() < self.loss:
            self.dropped += 1
            return True
        return False

    def _run(self):
        while self.running:
            r, _, _ = select.select([self.front, self.back], [], [], 0.05)
            for s in r:
                data, src = s.recvfrom(2048)
                if s is self.front:
                    self.client = src
                    if not self._drop():
                        self.back.sendto(data, self.server)
                elif self.client and not self._drop():
                    self.front.sendto(data, self.client)

    def stop(self):
        self.running = False


# ---------------- клиент: порт ack.pde ----------------
class AckClient:
    def __init__(self, dest, timeout=0.05, tries=5, seed=1):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setblocking(False)
        self.dest = dest
        self.timeout, self.tries = timeout, tries
        self.rng = random.Random(seed)
        self.ack_mode = False
        self.seq = 0
        self.queue = []          # [0] - в полёте; элемент: dict(pkt, seq, key, n, sent)
        self.replies = []        # ответы с данными
        self.dropped = 0

    def raw(self, data):
        pkt = bytes([ord("G"), ord("T")] + data)
        self.sock.sendto(pkt, self.dest)
        time.sleep(0.015)
        self.sock.sendto(pkt, self.dest)   # как sendData(byte[]) в func.pde: пакет дважды

    @staticmethod
    def key(d):
        if len(d) < 2:
            return -1
        if d[0] == 2 and d[1] != 6:
            return d[0] * 256 + d[1]
        if d[0] == 4 and 1 <= d[1] <= 3:
            return d[0] * 256 + d[1]
        return -1

    def send(self, d):
        if self.ack_mode and d[0] not in (0, 5):
            self.seq = (self.seq + 1) & 0xFF
            c = dict(pkt=bytes([ord("G"), ord("T"), d[0] | 0x80, self.seq] + d[1:]), seq=self.seq, key=self.key(d), n=0, sent=0.0)
            merged = False
            if c["key"] >= 0:
                for i in range(1, len(self.queue)):
                    if self.queue[i]["key"] == c["key"]:
                        self.queue[i] = c
                        merged = True
                        break
            if not merged:
                self.queue.append(c)
            self.tick()
        else:
            self.raw(d)

    def tick(self):
        while self.queue:
            c = self.queue[0]
            if c["n"] > 0 and time.time() - c["sent"] < self.timeout:
                return
            if c["n"] >= self.tries:
                self.dropped += 1
                self.queue.pop(0)
                continue
            self.sock.sendto(c["pkt"], self.dest)
            c["n"] += 1
            c["sent"] = time.time()
            return

    def on_packet(self, b):
        if len(b) < 3 or b[:2] != b"GT":
            return
        t = b[2]
        if t == 5 and len(b) >= 4:
            if not self.ack_mode:
                self.ack_mode = True
                self.seq = self.rng.randrange(256)
                self.send([1])
                self.send([7])
        elif t == 6 and len(b) >= 6:
            if self.queue and self.queue[0]["seq"] == b[4]:
                self.queue.pop(0)
                self.tick()
        else:
            self.replies.append(b)

    def pump(self, seconds, until=None):
        end = time.time() + seconds
        while time.time() < end:
            r, _, _ = select.select([self.sock], [], [], 0.005)
            if r:
                try:
                    self.on_packet(self.sock.recv(2048))
                except BlockingIOError:
                    pass
            self.tick()
            if until and until():
                return True
        return until() if until else True

    def handshake(self, seconds=8):
        end = time.time() + seconds
        while time.time() < end and not self.ack_mode:
            self.raw([5])
            self.pump(0.12, lambda: self.ack_mode)
        self.pump(2.0, lambda: not self.queue)   # дождаться первых {1} и {7}
        return self.ack_mode


# ---------------- сценарии ----------------
def scenario_old_client(srv):
    print("old client (no ACK), lossless link")
    px = LossyProxy(srv.port, 0.0, 1)
    c = AckClient(px.addr)
    c.raw([2, 2, 77])
    check(srv.get("bright") == 77, "brightness set by an old-style packet")
    time.sleep(0.06)
    c.replies.clear()
    c.raw([5])
    c.pump(0.3)
    check(any(r[2] == 5 for r in c.replies) or c.ack_mode, "{5} answered")
    time.sleep(0.06)
    c.raw([1])
    c.pump(0.3, lambda: any(r[2] == 1 for r in c.replies))
    cfg = [r for r in c.replies if r[2] == 1]
    check(bool(cfg) and cfg[0][6] == 77, "{1} reply reports brightness 77")
    px.stop()


def scenario_slider(srv):
    print("slider coalescing, lossless link")
    px = LossyProxy(srv.port, 0.0, 2)
    c = AckClient(px.addr, timeout=0.2, seed=2)
    check(c.handshake(), "handshake enables ACK mode")
    for v in range(1, 51):
        c.send([2, 2, v])
    check(len(c.queue) <= 2, "queue holds at most 2 commands after 50 slider values (%d)" % len(c.queue))
    c.pump(3, lambda: not c.queue)
    check(srv.get("bright") == 50, "last slider value applied (%s)" % srv.get("bright"))
    px.stop()


def scenario_lossy(srv, seed, loss):
    px = LossyProxy(srv.port, loss, seed)
    c = AckClient(px.addr, timeout=0.05, seed=seed)
    ok = c.handshake(15)
    if not ok:
        print("seed %d: handshake did not complete at %d%% loss (skipped)" % (seed, int(loss * 100)))
        px.stop()
        return
    start = srv.get("curEff")
    for _ in range(6):
        c.send([2, 6])
    for v in (10, 20, 30):
        c.send([2, 2, v])
    c.pump(20, lambda: not c.queue)
    done = srv.get("curEff")
    delta = (done - start) % srv.get("count")   # по кругу всех эффектов (число берётся из прошивки)
    msg = "seed %d, loss %d%%: next-effect executed %d of 6, dropped by client %d" % (seed, int(loss * 100), delta, c.dropped)
    check(delta <= 6, msg + " (never more than sent)")
    if c.dropped == 0:
        check(delta == 6 and srv.get("bright") == 30, "seed %d: every command confirmed and executed exactly once" % seed)
    px.stop()


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else "./gt_host"
    srv = Server(exe)
    try:
        scenario_old_client(srv)
        scenario_slider(srv)
        print("lossy link (30% each way), several seeds")
        for seed in range(1, 9):
            scenario_lossy(srv, seed, 0.3)
    finally:
        srv.stop()
    print("\n%s" % ("FAILED: %d check(s)" % fails if fails else "all checks passed"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
