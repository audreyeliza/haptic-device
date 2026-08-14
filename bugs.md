# Haptic Device Debug Notes

Date: 2026-04-02

# Partially Fixed
1. Shutdown no longer hangs just because no haptics thread was started.
2. I did not validate real-device shutdown behavior because no hardware was present, so that case remains only partially covered.
# Still Open
1. No-device mode now runs a different simulation path and materially changes the dynamics.
2. Button 0 behavior is still documented but not implemented.
3. The button-1 anchored-atom scan can still loop forever.
4. The `u` hotkey still contains an assignment inside `assert`.
5. Mouse interactions are still undocumented in the README.

# Open Issue 1: No-Device Simulation Path Changes The Physics

Observed behavior:
- The previous crash is gone, but the keyboard-only fallback is no longer following the original haptics-driven dynamics.
- In no-device mode the simulation is advanced from the graphics loop rather than the haptics loop.
- The fallback path caps timestep, velocity, and per-step displacement, and it zeroes non-finite forces/accelerations before continuing.
- The current atom is also held in place unless a real haptic device is present.

Why this matters:
- This is the strongest current explanation for reports that the atoms now move very differently, fling around unnaturally, or seem inconsistent with the expected LJ response.
- The calculators are still being called, but the post-force integration path is now modified enough that the visual behavior is no longer equivalent to the original runtime.

# Open Issue 2: README/Button 0 Behavior Mismatch

Relevant code:
- README claim in [README.md](/home/darre/chai3d-3.3.0/haptic-device/README.md#L109)
- Button read in [LJ.cpp](/home/darre/chai3d-3.3.0/haptic-device/LJ.cpp#L1302)

Problem:
- Button 0 is still read but not used.
- The README still says button 0 turns off forces while pressed.

Status:
- Still open.

# Open Issue 3: Button 1 Can Still Hang When All Candidates Are Anchored

Problem:
- The button-1 scan still has no termination condition.
- If every candidate atom is anchored, the `while (atoms[simulatedCurrentAtom]->isAnchor())` loop can spin forever.

# Partially Fixed Item 4: Shutdown Hang

Previous claim:
- Quit could hang forever waiting for `simulationFinished`.

Current result:
- `close()` now waits only if the haptics thread actually started, via [LJ.cpp](chai3d-3.3.0/haptic-device/LJ.cpp#L964).
- In this no-device environment, `q` and `ESC` both produced normal shutdown.
