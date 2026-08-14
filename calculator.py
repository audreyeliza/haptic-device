# Python side of the ASE calculator.
# Resolves a short calculator spec string to an ASE-compatible calculator and
# computes interatomic forces and potential energies.
# The C++ ASE calculator (potentials.cpp) drives the simulation and delegates
# UMA construction here via uma_wrapper, while get_values is a self-contained
# Python path that builds an Atoms object and returns forces/energy directly.

import sys
sys.stdout.reconfigure(line_buffering=True)
print("LOADED", __file__, flush=True)

from importlib import import_module
from ast import literal_eval

import paramiko
import pickle
import numpy as np
import re
import socket
import struct
import time

import threading

USERNAME = "sc73369"
REMOTE_HOST = "saskatchewan.cm.utexas.edu"
REMOTE_PYTHON = f"/home/{USERNAME}/uma_env/bin/python3"
NUM_SHARDS = 1

import threading
import time
from collections import defaultdict

class GPUMonitor:
    def __init__(self, ssh, jobid, username, interval_ms=500):
        self.ssh = ssh
        self.jobid = jobid
        self.interval_ms = interval_ms
        self._lock = threading.Lock()
        self._samples = defaultdict(list)   # index -> [(util, used, total, name), ...]
        self._stop = threading.Event()
        self._thread = None
        self._stdout = None
        self._t0 = self._t1 = None

    def start(self):
        query = "index,name,utilization.gpu,memory.used,memory.total"
        cmd = (f"srun --jobid={self.jobid} --overlap "
               f"nvidia-smi --query-gpu={query} "
               f"--format=csv,noheader,nounits -lms {self.interval_ms}")
        _, self._stdout, _ = self.ssh.exec_command(cmd)
        self._t0 = time.time()
        self._thread = threading.Thread(target=self._read_loop, daemon=True)
        self._thread.start()
        return self

    def _read_loop(self):
        for line in iter(self._stdout.readline, ""):
            if self._stop.is_set():
                break
            parts = [p.strip() for p in line.strip().split(",")]
            if len(parts) != 5:
                continue  # skip blanks / warnings
            try:
                idx, name = int(parts[0]), parts[1]
                util, used, total = float(parts[2]), float(parts[3]), float(parts[4])
            except ValueError:
                continue
            with self._lock:
                self._samples[idx].append((util, used, total, name))

    def stop(self):
        self._stop.set()
        self._t1 = time.time()
        try:
            self._stdout.channel.close()   # closes this channel only, keeps ssh alive
        except Exception:
            pass
        if self._thread:
            self._thread.join(timeout=2)
        return self.average()

    def average(self):
        with self._lock:
            gpus = {}
            for idx, s in self._samples.items():
                if not s:
                    continue
                utils = [x[0] for x in s]
                useds = [x[1] for x in s]
                gpus[idx] = {
                    "name": s[-1][3],
                    "n_samples": len(s),
                    "util_percent_avg": sum(utils) / len(utils),
                    "util_percent_max": max(utils),
                    "mem_used_mib_avg": sum(useds) / len(useds),
                    "mem_used_mib_max": max(useds),
                    "mem_total_mib": s[-1][2],
                }
            dur = (self._t1 or time.time()) - self._t0
            return {"duration_s": dur, "gpus": gpus}

    def __enter__(self):
        return self.start()

    def __exit__(self, *exc):
        self.stop()

class Atoms:
    def __init__(self, **kwargs):
        print("Initializing...", flush=True)
        self.num_atoms = len(kwargs["numbers"])
        self.ssh = paramiko.SSHClient()
        self.ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())

        print("Preconnecting...", flush=True)
        self.ssh.connect(
            hostname=REMOTE_HOST,
            username=USERNAME,
            timeout=10,
            banner_timeout=10,
            auth_timeout=10,
        )
        # Without this, small position/force writes are subject to Nagle's
        # algorithm colliding with the remote's delayed ACKs, adding tens of
        # ms of latency to every round trip.
        self.ssh.get_transport().sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        print("Connected! Opening SFTP...", flush=True)

        sftp = self.ssh.open_sftp()
        print("SFTP Open! Uploading server scripts...", flush=True)
        sftp.put("../haptic-device/server.py", f"/home/{USERNAME}/.cache/server.py")
        sftp.close()
        print("Uploaded submitting job...", flush=True)

        # Reserve the GPU through Slurm (for accounting/fairness) but run the
        # actual interactive server directly over this SSH channel rather
        # than wrapping it in `srun`. slurmstepd polls NVML for GPU
        # accounting on jobs holding a gres/gpu allocation, and that
        # contends with the job's own NVML use (torch queries NVML at CUDA
        # init) -- the contention stalls srun's stdio forwarding by ~45ms on
        # every single read, independent of network latency or the actual
        # model compute time. Bypassing srun's forwarding for the hot loop
        # avoids that entirely while still holding a real allocation.
        # --time bounds worst-case GPU leakage if cleanup below doesn't run.
        _, salloc_out, salloc_err = self.ssh.exec_command(
            f"bash -l -c 'salloc --gres=shard:{NUM_SHARDS} --time=04:00:00 --no-shell'",
            get_pty=False,
        )
        salloc_text = salloc_out.read().decode(errors="replace") + salloc_err.read().decode(errors="replace")
        jobid_match = re.search(r"Granted job allocation (\d+)", salloc_text)
        if not jobid_match:
            raise RuntimeError("Failed to obtain Slurm GPU allocation:\n" + salloc_text)
        self.jobid = jobid_match.group(1)

        _, env_out, _ = self.ssh.exec_command(
            f"bash -l -c 'srun --jobid={self.jobid} --overlap env'", get_pty=False
        )
        cuda_devices_match = re.search(r"CUDA_VISIBLE_DEVICES=(\S+)", env_out.read().decode(errors="replace"))
        cuda_devices = cuda_devices_match.group(1) if cuda_devices_match else "0"

        # This host only ships the versioned driver library (libcuda.so.1),
        # not the unversioned libcuda.so symlink that `-lcuda` needs at link
        # time. That's fine for torch/CUDA's own runtime dlopen (it loads by
        # SONAME), but it breaks Triton's JIT kernel compilation the first
        # time a new kernel shape is hit, well after startup. Stub it in a
        # directory we control and add that to LIBRARY_PATH.
        _, stub_out, stub_err = self.ssh.exec_command(
            "bash -l -c 'mkdir -p ~/.cache/cuda_stub_lib && ln -sf "
            "\"$(find /usr/lib/x86_64-linux-gnu -maxdepth 1 -name \"libcuda.so.*\" | sort -V | tail -1)\" "
            "~/.cache/cuda_stub_lib/libcuda.so'"
        )
        stub_err_text = stub_err.read().decode(errors="replace")
        stub_out.read()
        if stub_err_text.strip():
            print("Warning: libcuda.so stub setup:", stub_err_text.strip(), flush=True)

        self.stdin, self.stdout, self.stderr = self.ssh.exec_command(
            f"bash -l -c 'SLURM_JOB_ID={self.jobid} CUDA_VISIBLE_DEVICES={cuda_devices} "
            f"LIBRARY_PATH=$HOME/.cache/cuda_stub_lib:$LIBRARY_PATH "
            f"{REMOTE_PYTHON} -u /home/{USERNAME}/.cache/server.py'",
            get_pty=False,
        )
        print("Waiting for job to run (this might take a while)...", flush=True)

        while True:
            line = self.stdout.readline()
            if line == "":
                err = self.stderr.read().decode(errors="replace")
                raise RuntimeError("server.py exited before becoming ready:\n" + err)
            print("server:", line.strip(), flush=True)
            if "Ready to accept instructions" in line:
                print("Ready to go!", flush=True)
                break
        threading.Thread(target=self._drain, args=(self.stderr, "server"), daemon=True).start()
        data = pickle.dumps(kwargs)
        self.stdin.write(struct.pack("!I", len(data)))
        self.stdin.write(data)
        self.stdin.flush()
        print("Initialization done!", flush=True)

    def _drain(self, stream, prefix):
        for line in iter(stream.readline, ""):
            if not line:
                break
            print(f"[{prefix}] {line}", end="")

    def __del__(self):
        # The GPU allocation from salloc is independent of this process's
        # lifetime (unlike the old srun-wrapped job step), so it must be
        # released explicitly or it leaks until the --time limit expires.
        jobid = getattr(self, "jobid", None)
        if jobid is not None:
            try:
                self.ssh.exec_command(f"scancel {jobid}")
            except Exception:
                pass

    def set_positions(self, positions):
        self.stdin.write(np.array(positions, dtype=np.float32).tobytes())
        self.stdin.flush()

    def get_forces(self):
        # Read forces and energy in one shot instead of two separate reads:
        # each read is a round trip subject to network latency, and the
        # server always writes both together (see server.py).
        forces_size = np.dtype(np.float32).itemsize * self.num_atoms * 3
        data = self.stdout.read(forces_size + 8 + 1)
        self._cached_energy = struct.unpack("d", data[forces_size:forces_size + 8])[0]
        return np.frombuffer(data[:forces_size], dtype=np.float32).reshape((self.num_atoms, 3))

    def get_potential_energy(self):
        return self._cached_energy

    def gpu_monitor(self, interval_ms=500):
        return GPUMonitor(self.ssh, self.jobid, USERNAME, interval_ms)

# Module-level cache of UMA predictors. Building a predictor loads a large model
# into memory, so we keep one per (model, device) alive for the whole session
# and hand the same instance back on every subsequent call.
_uma_predictor_cache = {}

# Returns the cached UMA predictor for the given model and device, building it
# lazily on first use. "turbo" inference settings trade a little accuracy for
# the speed the haptic loop needs.
def _get_uma_predictor(model_name="uma-s-1p2", device="cuda"):
    from fairchem.core import pretrained_mlip
    key = (model_name, device)

    if key not in _uma_predictor_cache:
        _uma_predictor_cache[key] = pretrained_mlip.get_predict_unit(
            model_name,
            device=device,
            inference_settings="turbo"
        )

    return _uma_predictor_cache[key]

# Resolves a short spec string to a constructed ASE calculator. Accepts built-in
# aliases for common potentials (lj, morse, emt, uma) or a generic
# "module:Class[:kwargs]" format for anything else. Mirrors parseCalculatorSpec
# on the C++ side.
def create_calculator(spec):

    # Lennard-Jones is the default when no spec is given. ASE's built-in pair
    # potential.
    if not spec or spec in {"lj", "lennard-jones"}:
        module_name = "ase.calculators.lj"
        class_name = "LennardJones"
        kwargs = {}

        calculator_class = getattr(import_module(module_name), class_name)
        return calculator_class(**kwargs)

    # Morse pair potential.
    elif spec == "morse":
        module_name = "ase.calculators.morse"
        class_name = "MorsePotential"
        kwargs = {}

        calculator_class = getattr(import_module(module_name), class_name)
        return calculator_class(**kwargs)

    # Effective Medium Theory, a fast empirical potential for metals.
    elif spec == "emt":
        module_name = "ase.calculators.emt"
        class_name = "EMT"
        kwargs = {}

        calculator_class = getattr(import_module(module_name), class_name)
        return calculator_class(**kwargs)

    # UMA is Meta's universal ML potential. The optional ":task" suffix selects
    # the prediction head (omol, omat, oc20, ...); omol is the default.
    elif spec == "uma":
        from fairchem.core import FAIRChemCalculator

        # Examples:
        # "uma"
        # "uma:omol"
        # "uma:omat"
        # "uma:oc20"

        parts = spec.split(":")

        task_name = "omat"

        if len(parts) > 1:
            task_name = parts[1]

        predictor = _get_uma_predictor(
            model_name="uma-s-1p2",
            device="cuda"
        )

        return FAIRChemCalculator(
            predictor,
            task_name=task_name
        )

    elif spec == "uma-remote":
        return None

    # Generic format: "module:ClassName" or "module:ClassName:{...kwargs...}".
    else:
        parts = spec.split(":", 2)

        if len(parts) < 2:
            raise ValueError(
                "Calculator spec must be empty, a known alias, or module:Class[:kwargs]"
            )

        module_name, class_name = parts[0], parts[1]

        # The kwargs string is user-supplied calculator configuration. We use
        # literal_eval rather than eval for safety: it only accepts Python
        # literals, not arbitrary code.
        kwargs = literal_eval(parts[2]) if len(parts) == 3 else {}

        if not isinstance(kwargs, dict):
            raise ValueError("Calculator kwargs must evaluate to a dict")

        calculator_class = getattr(import_module(module_name), class_name)

        return calculator_class(**kwargs)

