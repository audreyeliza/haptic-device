# haptic-device
Code for the FRI haptic device project

**NOTE:** When launching the program, you may need to calibrate the device by pushing it in and out until it is recognized.

For hardware assembly/wiring, see [docs/HARDWARE.md](docs/HARDWARE.md); for
Windows/WSL setup, see [docs/WSL_SETUP.md](docs/WSL_SETUP.md); for project
background and semester write-ups, see [docs/journal.md](docs/journal.md).

## Desktop Launcher

`launcher/` contains a cross-platform (Mac/Windows) PySide6 GUI for
configuring parameters and launching `haptic-device`, plus a "Live controls"
panel (freeze, haptic mode, potential, anchors) that talks to a loopback IPC
server built into `LJ.cpp` while the simulation is running. See
[launcher/README.md](launcher/README.md) for setup and usage.

## OPTIONS

The haptic mode is a **required** first argument: `force`, `position`, or
`standby`. Without it, the binary throws "Missing haptic mode argument" and
shows a black window. Every option below shifts one slot later because of
this, e.g. `./haptic-device force 38`.

Specify the # of atoms at launch like so:
```
./haptic-device force 38
```
If you don't pass in an atom count, the default is five.

You can also read in an existing configuration:
```
./haptic-device force example.con
```
Make sure the .con file is in ../resources/data.

Choose the potential energy surface by adding a third argument:
```
./haptic-device force 25 morse
```
The default is Lennard-Jones(lj). Other options are morse and ase.

When using `ase`, you can optionally provide a fourth argument to choose a full
ASE calculator spec:
```
./haptic-device force structure.xyz ase
./haptic-device force structure.xyz ase lj
./haptic-device force structure.xyz ase morse
./haptic-device force structure.xyz ase ase.calculators.emt:EMT
./haptic-device force structure.xyz ase ase.calculators.lj:LennardJones:{'sigma': 2.5, 'epsilon': 0.8}
```
Supported ASE shortcuts are `lj`, `morse`, `emt`, and `uma` (Meta's universal
ML potential; `uma:omol`, `uma:omat`, `uma:oc20` select the prediction head).
For any other ASE calculator, use `module:Class[:kwargs]`.

A fifth argument controls periodic boundary conditions: `on` forces PBC on,
`off` forces it off, and `keep` (or omitting the argument) leaves whatever
the loaded structure file specified untouched:
```
./haptic-device force 25 lj "" off
```
(the empty 4th argument is a placeholder for the ASE calculator spec, which
is only meaningful when the potential is `ase`)

The simulation time step (seconds) can be set at launch via the
`HAPTIC_DEVICE_TIME_STEP` environment variable (default `0.001`, valid range
`0.0001`-`0.005`), and changed live while running through the desktop
launcher or the IPC command server (see below).

## Build Instructions

### Windows

Native Windows builds go through CMake rather than `make` (which needs
WSL/Linux or macOS — see below).

1. Install a Python 3 build from [python.org](https://www.python.org/downloads/)
   (it bundles the dev headers/import library CMake's `find_package(Python3)`
   needs — the Microsoft Store build does not).
2. Download or clone a CHAI3D tree and build it first
   ```
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release
   ```
3. Clone this repo into the CHAI3D directory so it sits next to `src`,
   `examples`, and `build`
4. Create the directory `data` in `bin/resources` and move the file
   `global_minima.txt` there
5. Build `haptic-device` itself
   ```
   cd haptic-device
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release
   ```
6. The binary will be written to `bin/win-x64/haptic-device.exe` (or
   `bin/win-Win32` for a 32-bit build)

Force Dimension/Novint Falcon (DHD) support isn't compiled in on Windows —
upstream CHAI3D only links it on macOS/Linux/QNX — so a native Windows build
always runs in keyboard/mouse-only mode. If you need the physical haptic
device on a Windows box, build and run through WSL instead — see
[docs/WSL_SETUP.md](docs/WSL_SETUP.md) for the full walkthrough, including
USB passthrough and the no-admin-access fallback.

### Linux / macOS

The repo needs to live inside a CHAI3D checkout, and CHAI3D + haptic-device
both build via CMake (the repo also carries an old `Makefile` from before
the CMake build existed — ignore it).

1. Install build dependencies.

   Linux (Debian/Ubuntu):
   ```
   sudo add-apt-repository universe && sudo apt update
   sudo apt-get install libusb-1.0-0-dev libasound2-dev freeglut3-dev xorg-dev python3-dev cmake build-essential git
   ```
   macOS: install Xcode Command Line Tools (`xcode-select --install`) and
   CMake; see CHAI3D's own `doc/getting-started.html` for any other
   platform prerequisites.

2. Download or clone a CHAI3D tree and build it first:
   ```
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build -j"$(nproc)"   # macOS: -j$(sysctl -n hw.ncpu)
   ```
   Newer CMake (4.x) rejects CHAI3D's old minimum-version line — if you hit
   `Compatibility with CMake < 3.5 has been removed`, add
   `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` to the command above.
3. Clone this repo into the CHAI3D directory so it sits next to `src`,
   `examples`, and `build`.
4. Create the directory `data` in `bin/resources` and move the file
   `global_minima.txt` there (it's a move, so it only exists in the new
   location afterward).
5. Build haptic-device itself:
   ```
   cd haptic-device
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build -j"$(nproc)"
   ```
6. The main binary is written to `bin/lin-x86_64/haptic-device` (Linux) or
   the equivalent macOS output directory. The diagnostic tools
   (`chai3d_device_test`, `chai3d_visualizer`) build to the haptic-device
   repo root instead.
7. (Linux only, for Force Dimension/Novint Falcon support) from the CHAI3D
   root:
   ```
   sudo cp ./externals/DHD/doc/linux/51-forcedimension.rules /etc/udev/rules.d
   sudo udevadm control --reload-rules && udevadm trigger
   ```

At this point the software runs with mouse and keyboard. To run it with the
physical 1-DOF device, plug it in and confirm the serial device shows up:
```
ls /dev/cu.usbmodem*        # Mac
ls /dev/ttyACM*             # Linux
```
Sanity-check the serial link with the diagnostic visualizer first — move
the shaft and confirm the on-screen numbers change in real time:
```
./chai3d_visualizer /dev/cu.usbmodem####
```
Then launch the full simulation with the port set via env var:
```
export HAPTIC_DEVICE_SERIAL_PORT=/dev/cu.usbmodem####
./haptic-device force
```

For hardware assembly, wiring, and CAD references, see
[docs/HARDWARE.md](docs/HARDWARE.md).


## Reference
The textbook is too big to upload so here's the link: http://www.charleshouserjr.com/Cplus2.pdf


## Notes

### Controls
* The buttons are labeled 0-3, starting at the center and going clockwise for user switches
* Button naming convention in LJ-test.cpp (example = name in LJ-test.cp
* p)
    * button 0 = button
        * turns off forces while pressed
    * button 1 = button2
        * this button changes the current atom being used
    * button3 = freebutton
        * also does nothing
    * button2  = button3
        * this changes the camera position
* Keyboard hotkeys:
    * `q` or `ESC`
        * quit program
    * `f`
        * toggle fullscreen
    * `u`
        * unanchor all atoms
    * `s`
        * screenshot atomic configuration without graph
    * `SPACE`
        * freeze atom movement
    * `c`
        * save configuration to .con file
    * `a`
        * anchor all atoms     
    * `ARROW KEYS`
        * move camera
    * `[` and `]`
        * zoom in/out
    * `r`
        * reset camera
    * `CTRL`
        * toggle help panel
