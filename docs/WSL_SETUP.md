# Windows/WSL Setup (First Time on a New Machine)

This covers building and running the physical device on Windows through
WSL2, including the fallback path for machines without admin access. For a
native (non-WSL) Windows build, or for Linux/macOS builds, see the main
[README.md](../README.md). For running the desktop launcher GUI (not just
the raw binary) under WSL, see
[launcher/README.md's "Running under WSL" section](../launcher/README.md#running-under-wsl).

**Note:** the repo needs to live inside a CHAI3D checkout, and CHAI3D +
haptic-device both build via CMake.

## 1. Install WSL2 (Ubuntu)

Skip this step if it's already installed on the machine. Otherwise, from an
elevated (Run as Administrator) PowerShell:

```
wsl --install
```

This installs WSL2 with Ubuntu as the default distro. Restart when
prompted, then finish the Ubuntu first-run setup (create a username/password)
before continuing.

## 2. Clone the repos inside WSL's own filesystem

Do not clone on the Windows side (`/mnt/c/...`) — builds across the
WSL/Windows filesystem boundary are slow and occasionally flaky.

```
cd ~ && mkdir -p dev && cd dev
git clone https://github.com/chai3d/chai3d.git chai3d
cd chai3d
git clone https://github.com/audreyeliza/haptic-device.git
```

## 3. Install build dependencies

```
sudo add-apt-repository universe && sudo apt update
sudo apt-get install libusb-1.0-0-dev libasound2-dev freeglut3-dev xorg-dev python3-dev cmake build-essential git socat
```

## 4. Build CHAI3D

Newer CMake (4.x) rejects CHAI3D's old minimum-version line. Without the
policy flag below, you'll hit: `Compatibility with CMake < 3.5 has been
removed.`

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build -j"$(nproc)"
```

## 5. Move the resource file

**Note:** this goes in the CHAI3D root's `bin/` — not haptic-device's. It's
a move, so `global_minima.txt` only exists in the new location afterward;
don't go looking for it back at the repo root.

```
mkdir -p ~/dev/chai3d/bin/resources/data
mv ~/dev/chai3d/haptic-device/global_minima.txt ~/dev/chai3d/bin/resources/data/
```

## 6. Build haptic-device

```
cd ~/dev/chai3d/haptic-device
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Output locations:

* Main binary: `~/dev/chai3d/bin/lin-x86_64/haptic-device`
* Diagnostic tools (`chai3d_device_test`, `chai3d_visualizer`) build to the
  haptic-device repo root instead

# Running the Simulation

## Mac/Linux — Device Plugged In Directly (No Bridge Needed)

```
ls /dev/cu.usbmodem*        # Mac
ls /dev/ttyACM*             # Linux
./chai3d_visualizer /dev/cu.usbmodem####
```

## Windows/WSL — Without Admin Access (usbipd-win workaround)

### If admin access is available, use this instead (simpler, lower-latency)

Install usbipd-win, in an elevated (Run as Administrator) PowerShell:

```
winget install --interactive --exact dorssel.usbipd-win
```

Then, each session, list attached USB devices:

```
usbipd list
```

Find your device's BUSID in the list, then bind and attach it to WSL (also
elevated PowerShell):

```
usbipd bind --busid <BUSID>
usbipd attach --wsl --busid <BUSID>
```

The device now shows up inside WSL as `/dev/ttyACM0` (or similar) — skip the
rest of this section entirely and go straight to "Running the Simulation."

**Note:** everything below (mirrored networking + PowerShell bridge) is only
needed when nobody with admin access is around.

### One-time setup: mirrored networking

WSL2 needs "mirrored" networking mode, or it can't reach a TCP server
running on Windows via localhost. This is a **config file**, not a terminal
command.

Open (or create) it, in PowerShell:

```
notepad $env:USERPROFILE\.wslconfig
```

If Notepad asks to create a new file, say yes. Copy this into the file, then
save and close:

```
[wsl2]
networkingMode=mirrored
```

Then, back in PowerShell (this part IS a terminal command), run:

```
wsl --shutdown
```

and reopen the WSL terminal.

### Every session: three things running at once

#### 1 — PowerShell (Windows side): bridge the COM port to a TCP socket

Find the port first, in PowerShell:

```
[System.IO.Ports.SerialPort]::GetPortNames()
```

Cross-check against Device Manager → Ports (COM & LPT) if multiple show up
and you're not sure which one is your device.

**Note:** `DtrEnable`/`RtsEnable` must be set true after `Open()`. Most
Arduino boards hold the chip in reset until DTR is asserted, and .NET's
`SerialPort` defaults it to false — without this, the bridge "connects" fine
but no real data ever comes through (looks like a dead device even though it
isn't).

**Tip:** turn off Notepad's autocorrect/text suggestions before pasting the
script below — it silently turns straight quotes into curly smart quotes,
which breaks the PowerShell parser in confusing ways (looks like "missing
paren" errors).

Create the file, in PowerShell:

```
notepad serial-bridge.ps1
```

If Notepad asks to create a new file, say yes. Copy this into the file, then
save and close (keep a copy on the lab PC, or regenerate it fresh each
time):

```powershell
param(
  [string]$ComPort = "COM4",
  [int]$BaudRate = 115200,
  [int]$ListenPort = 9001
)
try {
    $serial = New-Object System.IO.Ports.SerialPort $ComPort, $BaudRate, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
    $serial.ReadTimeout = 50
    $serial.Open()
    $serial.DtrEnable = $true
    $serial.RtsEnable = $true
} catch {
    Write-Host "FAILED to open $ComPort : $_"
    exit 1
}
try {
    $listener = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, $ListenPort)
    $listener.Start()
} catch {
    Write-Host "FAILED to listen on port $ListenPort : $_"
    exit 1
}
Write-Host "Bridging $ComPort @ $BaudRate baud <-> tcp/$ListenPort (loopback only)"
while ($true) {
    $client = $listener.AcceptTcpClient()
    Write-Host "Client connected."
    $stream = $client.GetStream()
    while ($client.Connected) {
        try {
            $n = $serial.BytesToRead
            if ($n -gt 0) {
                $buf = New-Object byte[] $n
                $serial.Read($buf, 0, $n) | Out-Null
                $stream.Write($buf, 0, $n)
            }
        } catch {}
        try {
            if ($stream.DataAvailable) {
                $buf = New-Object byte[] 256
                $r = $stream.Read($buf, 0, $buf.Length)
                if ($r -eq 0) { break }
                $serial.Write($buf, 0, $r)
            }
        } catch { break }
        Start-Sleep -Milliseconds 2
    }
    $client.Close()
    Write-Host "Client disconnected, waiting for reconnect..."
}
```

Then run it:

```
powershell -ExecutionPolicy Bypass -File serial-bridge.ps1 -ComPort COM4 -BaudRate 115200 -ListenPort 9001
```

#### 2 — WSL: expose the TCP stream as a virtual serial device

```
socat -d -d pty,raw,echo=0,link=$HOME/ttyBridge tcp:localhost:9001
```

#### 3 — WSL (separate terminal): run the actual simulation

```
cd ~/dev/chai3d
export HAPTIC_DEVICE_SERIAL_PORT=$HOME/ttyBridge
./bin/lin-x86_64/haptic-device force
```

**Note:** `force` is a required first argument — the haptic mode (`force`,
`position`, or `standby`). Without it, the binary throws "Missing haptic
mode argument" and shows a black window. Every other CLI option (atom
count, structure file, potential, ASE spec, PBC — see the main README's
[OPTIONS](../README.md#options) section) shifts one slot later because of
this, e.g.:

```
./bin/lin-x86_64/haptic-device force 25 morse
```

To sanity-check the device/bridge chain before launching the full sim, use
the diagnostic tool and move the shaft — the on-screen numbers should change
in real time:

```
cd ~/dev/chai3d/haptic-device
./chai3d_device_test $HOME/ttyBridge
```

### Known limitation of the bridge setup

This path adds real latency/jitter on top of the actual serial link: COM
port → PowerShell poll loop → TCP → socat → PTY. It's fine for confirming
the build/protocol/mechanism work end to end, but force feedback can feel
choppy or intermittent rather than smooth — that's expected on this path,
not a bug.

* Check the on-screen Hz counter in the sim if force feel seems off.
* If it's well under ~500–1000Hz, that's the bridge, not the hardware.
* Switch to real usbipd-win USB passthrough once admin access is available,
  for a cleaner/lower-latency connection.

**Note:** `customHapticDevice.cpp`'s force/stiffness specs
(`m_maxLinearForce`, etc.) are explicitly marked as placeholders in the code
— not yet retuned for the real cable/capstan mechanism. Worth revisiting
once the mechanical side is finalized.
