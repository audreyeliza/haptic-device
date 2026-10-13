# Python side of the ASE calculator.
# Resolves a short calculator spec string to an ASE-compatible calculator and
# computes interatomic forces and potential energies.
# The C++ ASE calculator (potentials.cpp) drives the simulation and delegates
# UMA construction here via uma_wrapper, while get_values is a self-contained
# Python path that builds an Atoms object and returns forces/energy directly.

from importlib import import_module
from ast import literal_eval

import paramiko
import pickle
import numpy as np
import struct
import time

import threading

USERNAME = "sc73369"
REMOTE_PYTHON = f"/home/{USERNAME}/uma_env/bin/python3"
NUM_SHARDS = 1

class Atoms:
    def __init__(self, **kwargs):
        self.num_atoms = len(kwargs["numbers"])
        self.ssh = paramiko.SSHClient()
        self.ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        self.ssh.connect(hostname="saskatchewan.cm.utexas.edu", username=USERNAME)
        sftp = self.ssh.open_sftp()
        sftp.put("../haptic-device/server.py", f"/home/{USERNAME}/.cache/server.py")
        sftp.close()
        self.stdin, self.stdout, self.stderr = self.ssh.exec_command(f"bash -l -c 'srun --gres=shard:{NUM_SHARDS} {REMOTE_PYTHON} -u /home/{USERNAME}/.cache/server.py'", get_pty=False)
        
        
        print("Waiting for server to be ready...")
        while True:
            line = self.stdout.readline()
            if line == "":  # EOF — server exited before signaling ready
                err = self.stderr.read().decode(errors="replace")
                raise RuntimeError(
                    "server.py exited before becoming ready:\n" + err
                )
            print(line.strip())
            if "Ready to accept instructions" in line:
                print("Server is ready")
                break

        # only now start the background drain, for the steady-state request loop
        threading.Thread(target=self._drain, args=(self.stderr, "server"), daemon=True).start()
        data = pickle.dumps(kwargs)
        self.stdin.write(struct.pack("!I", len(data)))
        self.stdin.write(data)
        self.stdin.flush()

    def _drain(self, stream, prefix):
        for line in iter(stream.readline, ""):
            if not line:
                break
            print(f"[{prefix}] {line}", end="")

    def set_positions(self, positions):
        self.stdin.write(np.array(positions, dtype=np.float32).tobytes())

    def get_forces(self):
        size = np.dtype(np.float32).itemsize * self.num_atoms * 3
        data = self.stdout.read(size)
        return np.frombuffer(data, dtype=np.float32).reshape((self.num_atoms, 3))

    def get_potential_energy(self):
        data = self.stdout.read(8 + 1)[:8]
        return struct.unpack("d", data)[0]

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
