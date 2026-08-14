import time
import pynvml
import numpy as np
from ase.visualize import view
from ase import units
from ase.io import read, write
from ase.md.verlet import VelocityVerlet
from fairchem.core import pretrained_mlip, FAIRChemCalculator

pynvml.nvmlInit()
handle = pynvml.nvmlDeviceGetHandleByIndex(0)

duration = 300
end_time = time.time() + duration

gpu_usage = []

zeolite = read("./Zeolite.poscar")
print("Initializing calculator...")
predictor = pretrained_mlip.get_predict_unit("uma-s-1p2", device="cuda", inference_settings="turbo")
zeolite.calc = FAIRChemCalculator(predictor, task_name="oc20")
dyn = VelocityVerlet(zeolite, 1 * units.fs)
print("Initialized! Starting simulation...")
frames = 0
while time.time() < end_time:
    gpu_usage.append(pynvml.nvmlDeviceGetUtilizationRates(handle).gpu)
    dyn.run(1)
    frames += 1
print("Done!")
print("Average GPU Usage:", np.array(gpu_usage).mean())
print("Refresh Rate:", frames / duration)
view(zeolite)
