"""Optional Windows helper. Importing or opening this tool makes no connections.

SPDX-License-Identifier: GPL-3.0-or-later
"""
import ftplib
import hashlib
import io
import ipaddress
import json
from pathlib import Path
import queue
import re
import socket
import threading
import time
import uuid

TITLE_ID = "PPSA99179"


def validate_target(host, port):
    address = ipaddress.IPv4Address(host.strip())
    private_lan = any(address in net for net in (
        ipaddress.IPv4Network("10.0.0.0/8"),
        ipaddress.IPv4Network("172.16.0.0/12"),
        ipaddress.IPv4Network("192.168.0.0/16")))
    if not private_lan:
        raise ValueError("Enter the console's private LAN IPv4 address.")
    port = int(port)
    if not 1 <= port <= 65535:
        raise ValueError("Port must be between 1 and 65535.")
    return str(address), port


def load_build(path):
    path = Path(path).resolve()
    manifest = json.loads((path.parent / "build-manifest.json").read_text())
    item = next((a for a in manifest["artifacts"] if a["name"] == path.name), None)
    if not item or path.name != TITLE_ID + ".ffpfsc":
        raise ValueError("Choose the Media Hub image beside its build manifest.")
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != item["sha256"] or len(data) != item["size"]:
        raise ValueError("Build hash or size does not match its manifest.")
    return data, digest


def redact_log(text):
    # Best effort only. Logs stay local and must be reviewed before sharing.
    text = re.sub(r"(?im)(authorization\s*[:=]\s*)([^\r\n]+)", r"\1[REDACTED]", text)
    text = re.sub(r"(?i)((?:api[_-]?key|access_token|refresh_token|token|password|client_secret)\s*[=:]\s*)[^\s&\"']+", r"\1[REDACTED]", text)
    text = re.sub(r"https?://[^\s\"'<>]+", "[URL REDACTED]", text)
    return text


def transfer(host, port, user, password, data):
    """Only called after the UI has approved exact bytes, address and action."""
    with ftplib.FTP() as ftp:
        ftp.connect(host, port, timeout=15)
        ftp.login(user or "anonymous", password)
        ftp.cwd("/data/homebrew")
        staging = "." + TITLE_ID + "." + uuid.uuid4().hex + ".uploading"
        ftp.storbinary("STOR " + staging, io.BytesIO(data))
        remote_hash = hashlib.sha256()
        remote_size = 0
        def verify(part):
            nonlocal remote_size
            remote_size += len(part)
            if remote_size > len(data):
                raise ValueError("Remote image exceeds the approved size.")
            remote_hash.update(part)
        ftp.retrbinary("RETR " + staging, verify)
        if remote_size != len(data) or remote_hash.digest() != hashlib.sha256(data).digest():
            raise ValueError("Remote verification failed; staged image was not activated.")
        # Only this rename exposes a fully verified image to the loader.
        ftp.rename(staging, TITLE_ID + ".ffpfsc")


def collect_log(host, port):
    chunks = []
    size = 0
    deadline = time.monotonic() + 15
    with socket.create_connection((host, port), timeout=3) as conn:
        conn.settimeout(1)
        while time.monotonic() < deadline and size < 2 * 1024 * 1024:
            try:
                part = conn.recv(min(65536, 2 * 1024 * 1024 - size))
            except socket.timeout:
                continue
            if not part:
                break
            chunks.append(part)
            size += len(part)
    return redact_log(b"".join(chunks).decode("utf-8", errors="replace"))


def main():
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
    window = tk.Tk()
    window.title("PS5 Media Hub Companion — approval required")
    window.geometry("730x530")
    values = {k: tk.StringVar(value=v) for k, v in (
        ("Image", ""), ("Console IPv4", ""), ("FTP port", "2121"),
        ("FTP username", "anonymous"), ("FTP password", ""), ("Log port", "9081"))}
    for row, (label, value) in enumerate(values.items()):
        ttk.Label(window, text=label).grid(row=row, column=0, padx=12, pady=6, sticky="w")
        ttk.Entry(window, textvariable=value, width=70,
                  show="*" if label == "FTP password" else "").grid(row=row, column=1, padx=8)
    status = tk.StringVar(value="Offline. No console connections have been made.")
    ttk.Label(window, textvariable=status, wraplength=680).grid(row=10, column=0, columnspan=2, padx=12, pady=15)
    ttk.Label(window, text="FTP uses an unencrypted LAN connection. A loader may launch an uploaded image.\nLogs stay on this PC; automatic redaction is best effort. Review before sharing.",
              wraplength=680).grid(row=11, column=0, columnspan=2, padx=12)
    results = queue.Queue()
    busy = False

    def choose():
        selected = filedialog.askopenfilename(filetypes=[("PS5 image", "*.ffpfsc")])
        if selected:
            values["Image"].set(selected)
            try:
                data, digest = load_build(selected)
                status.set(f"Verified {len(data):,} bytes. SHA-256: {digest}")
            except Exception as exc:
                messagebox.showerror("Build verification failed", str(exc))

    def run(action):
        nonlocal busy
        if busy:
            return
        try:
            host, port = validate_target(values["Console IPv4"].get(), values["FTP port" if action == "transfer" else "Log port"].get())
            if action == "transfer":
                data, digest = load_build(values["Image"].get())
                approved = messagebox.askyesno("Approve console transfer and possible launch",
                    f"Send {len(data):,} bytes to {host}:{port}\n/data/homebrew/{TITLE_ID}.ffpfsc\nSHA-256: {digest}\n\nThis can replace an existing Media Hub test image. The console's loader may automatically mount and launch it. Firmware 10.00 has not been validated. Approve this transfer and possible launch?")
                user = values["FTP username"].get()
                password = values["FTP password"].get()
                task = lambda: transfer(host, port, user, password, data)
            else:
                save = filedialog.asksaveasfilename(defaultextension=".txt", initialfile="media-hub-log.txt")
                if not save:
                    return
                approved = messagebox.askyesno("Approve log collection",
                    f"Connect to {host}:{port} and receive logs for up to 15 seconds / 2 MiB?\nThe log will be saved locally. It may contain private data despite redaction.")
                task = lambda: Path(save).write_text(collect_log(host, port), encoding="utf-8")
            if not approved:
                status.set("Cancelled. No connection made.")
                return
            busy = True
            status.set("Performing the approved action once; no automatic retries.")
            def worker():
                try:
                    task()
                    results.put("Approved action completed. Console behavior is not verified by a transfer.")
                except Exception as exc:
                    results.put("Action failed. No retry. " + redact_log(str(exc)))
            threading.Thread(target=worker, daemon=True).start()
        except Exception as exc:
            messagebox.showerror("Cannot proceed", str(exc))

    def poll():
        nonlocal busy
        try:
            status.set(results.get_nowait())
            busy = False
        except queue.Empty:
            pass
        window.after(150, poll)

    ttk.Button(window, text="Choose and verify image (offline)", command=choose).grid(row=7, column=1, pady=7)
    ttk.Button(window, text="Review transfer approval…", command=lambda: run("transfer")).grid(row=8, column=1, pady=7)
    ttk.Button(window, text="Review log collection approval…", command=lambda: run("logs")).grid(row=9, column=1, pady=7)
    poll()
    window.mainloop()


if __name__ == "__main__":
    main()
