import time
from ase.visualize import view
from ase import units
from ase.io import read, write
from ase.md.verlet import VelocityVerlet
from fairchem.core import pretrained_mlip, FAIRChemCalculator

duration = 300
end_time = time.time() + duration

zeolite = read("./Zeolite.poscar")
print("Initializing calculator...")
predictor = pretrained_mlip.get_predict_unit("uma-s-1p2", device="cuda", inference_settings="turbo")
zeolite.calc = FAIRChemCalculator(predictor, task_name="oc20")
dyn = VelocityVerlet(zeolite, 1 * units.fs)
print("Initialized! Starting simulation...")
while time.time() < end_time:
    dyn.run(1)
print("Done!")
view(zeolite)