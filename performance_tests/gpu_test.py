import sys
import subprocess
import numpy as np
import psutil
import matplotlib.pyplot as plt
import pynvml
import ase
from ase.io import write
from ase.io import read
from ase import units
from threading import Timer
from ase.md.verlet import VelocityVerlet
from fairchem.core import pretrained_mlip, FAIRChemCalculator
import gc
import torch

TEST_DURATION = 300.0

cpu_usage = [0]
gpu_usage = [0]
frame_averages = [0]

hapticless_cpu_usage = [0]
hapticless_gpu_usage = [0]
hapticless_frame_averages = [0]

atom_counts = [0]

def isInt(s):
    try:
        int(s)
        return True
    except (ValueError): 
        return False

def runSimulation(handle):
    print(sys.argv)

    global runTest
    runTest = True

    initialized = False
    t = Timer(TEST_DURATION, stopTest)
    

    process = subprocess.Popen(
        sys.argv,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1 
    )

    testCPUUsage = []
    testGPUUsage = []
    frames = []
    print("Looking for frames...")
    for line in process.stdout:
        try:
            frames.append(float(line))
            testCPUUsage.append(psutil.cpu_percent(interval=None))
            testGPUUsage.append(pynvml.nvmlDeviceGetUtilizationRates(handle).gpu)
            if not initialized:
                initialized = True
                t.start()
        except (ValueError):
            pass
        if not runTest:
            break
    print("Done looking for frames! Collected " + str(len(frames)) + " frames!")
    cpu_usage.append(np.array(testCPUUsage).mean())
    gpu_usage.append(np.array(testGPUUsage).mean())
    frame_averages.append(np.mean(np.array(frames)))
    print("Test " + str(i))
    print("CPU Usage: " + str(cpu_usage[i]))
    print("GPU Usage: " + str(gpu_usage[i]))
    print("Average FPS: " + str(frame_averages[i]))
    process.terminate()
    process.wait()

def stopTest():
    global runTest
    runTest = False

def runAseTest(structure, calc):
    global runTest
    print("Running Test w/o Haptic Device " + str(i))
    runTest = True
    structure.calc = calc
    dyn = VelocityVerlet(structure, .5 * units.fs)
    t = Timer(TEST_DURATION, stopTest)
    t.start()
    avgCPUUsage = []
    avgGPUUsage = []
    steps = 0
    while runTest:
        dyn.run(1)
        steps += 1
        avgCPUUsage.append(psutil.cpu_percent(interval=None))
        avgGPUUsage.append(pynvml.nvmlDeviceGetUtilizationRates(handle).gpu)
    del dyn, structure
    gc.collect()
    if torch.cuda.is_available():
        torch.cuda.empty_cache()
    return steps / TEST_DURATION, np.array(avgCPUUsage).mean(), np.array(avgGPUUsage).mean()

sys.argv.pop(0)
sys.argv.insert(0, "bin/lin-x86_64/haptic-device")
sys.argv.append("ase")
sys.argv.append("uma")



pynvml.nvmlInit()
handle = pynvml.nvmlDeviceGetHandleByIndex(0)

if isInt(sys.argv[2]):
    maxAtoms = int(sys.argv[2])
    for i in range(1, maxAtoms + 1):
        sys.argv[2] = str(i)
        runSimulation(handle)
else:
    print("Initializing UMA...")
    predictor = pretrained_mlip.get_predict_unit("uma-s-1p2", device="cuda", inference_settings="turbo")
    calc = FAIRChemCalculator(predictor, task_name="oc20")

    poscar = sys.argv[2]
    sys.argv[2] = "fps-test.poscar"
    repeats = sys.argv[3]
    sys.argv.pop(3)
    for i in range(1, int(repeats) + 1):
        structure = read(poscar)
        structure = structure.repeat((i, 1, 1))
        # structure.pbc = False
        atom_counts.append(len(structure))
        write(sys.argv[2], structure, format='vasp', direct=True)
        runSimulation(handle)
        f, c, g = runAseTest(structure, calc)
        hapticless_frame_averages.append(f)
        hapticless_cpu_usage.append(c)
        hapticless_gpu_usage.append(g)
    print("Running tests without haptic device...")
        

fig, ax = plt.subplots(nrows=2, ncols=1)

ax[0].plot(atom_counts, cpu_usage, label='CPU w/ haptic-device')
ax[0].plot(atom_counts, gpu_usage, label='GPU w/ haptic-device')
ax[0].plot(atom_counts, hapticless_cpu_usage, label='CPU w/o haptic-device')
ax[0].plot(atom_counts, hapticless_gpu_usage, label='GPU w/o haptic-device')
ax[1].plot(atom_counts, frame_averages, label='w/ haptic-device')
ax[1].plot(atom_counts, hapticless_frame_averages, label='w/o haptic-device')
ax[0].set_xlabel('Atoms')
ax[0].set_ylabel('Usage (%)')
ax[1].set_xlabel('Atoms')
ax[1].set_ylabel('FPS')
ax[0].legend()
ax[1].legend()

plt.savefig("performance-test.png")









