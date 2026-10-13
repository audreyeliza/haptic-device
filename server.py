import os, sys
sys.stderr = open("server_errors.txt", "w")
os.environ["LD_LIBRARY_PATH"] = f'/usr/lib/x86_64-linux-gnu:{os.environ["LD_LIBRARY_PATH"]}'

from fairchem.core import pretrained_mlip, FAIRChemCalculator
from ase import Atoms
from ase.calculators.lj import LennardJones
import numpy as np
import pickle
import struct
import time

predictor = pretrained_mlip.get_predict_unit(
   "uma-s-1p2",
   inference_settings="turbo",
   device="cuda"
)

print("Ready to accept instructions", flush=True)

length = struct.unpack("!I", sys.stdin.buffer.read(4))[0]
kwargs = pickle.loads(sys.stdin.buffer.read(length))
num_atoms = len(kwargs["numbers"])
atoms = Atoms(**kwargs)
atoms.calc = FAIRChemCalculator(predictor, "oc20")
while True:
    data = sys.stdin.buffer.read(np.dtype(np.float32).itemsize * num_atoms * 3)
    atoms.set_positions(np.frombuffer(data, dtype=np.float32).reshape((num_atoms, 3)))
    forces = atoms.get_forces()
    sys.stdout.buffer.write(forces.tobytes() + struct.pack("d", atoms.get_potential_energy()) + b"\n")
    sys.stdout.flush()

