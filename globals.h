#include "chai3d.h"
#include "atom.h"
#include <atomic>
#include <string>
#include <vector>
#include <GLFW/glfw3.h>
#include "potentials.h"
#include <unordered_map>
#include <tuple>
#include <mutex>

//------------------------------------------------------------------------------
// STATES
//------------------------------------------------------------------------------

enum LocalPotential { LENNARD_JONES, MORSE, ASE };
enum class HapticMode { Position, Standby, Force };

// Calculator object for force and potential energy calculatorString
extern Calculator* calculatorPtr;

// vector holding hotkey key labels
extern std::vector<chai3d::cLabel *> hotkeyKeys;

// vector holding function key labels (must be separate for formatting)
extern std::vector<chai3d::cLabel *> hotkeyFunctions;

// a camera to render the world in the window display
extern chai3d::cCamera *camera;


extern double CAMERA_RADIUS; 

extern const double DIST_SCALE;
// atom objects
extern std::vector<Atom *> atoms;

// coordinates of central atom
extern double centerCoords[3];

const int SWAP_INTERVAL = 1;

// current width of window
extern int width;

// current height of window
extern int height;

extern cScope *scope;

extern std::atomic<bool> freezeAtoms;

// debug rendering toggles: control whether atoms, force vectors, and bonds
// are drawn each frame. Changeable at runtime via hotkeys 1/2/3 or the IPC
// "set render_atoms/render_forces/render_bonds" commands.
extern std::atomic<bool> renderAtoms;
extern std::atomic<bool> renderForceVectors;
extern std::atomic<bool> renderBonds;

extern cLabel *camera_pos;

extern cLabel *helpHeader;

extern cPanel *helpPanel;

extern std::atomic<int> screenshotCounter;

extern std::atomic<int> writeConCounter;

extern std::recursive_mutex sceneMutex;

// currently selected haptic control scheme, changeable at runtime by the IPC server
extern std::atomic<HapticMode> hapticMode;

// currently active potential energy surface
extern LocalPotential energySurface;

// most recently computed potential energy, published for status queries
extern std::atomic<double> displayedPotentialEnergy;

// simulation time step in seconds, used by both the haptics-thread loop and
// the no-device keyboard fallback loop; changeable at runtime
extern std::atomic<double> simulationTimeStep;

// smallest and largest time step (seconds) accepted from launch/IPC input.
// The minimum is intentionally very small so the Time Step slider can crawl the
// simulation for close inspection; a tiny timestep is more accurate, just slow.
constexpr double MIN_SIMULATION_TIME_STEP = 0.0;
constexpr double MAX_SIMULATION_TIME_STEP = 2.0;

/**
 * @brief Sets the timestep
 * @param seconds How many femtoseconds the simulation runs at
 * @return true if the timestep is valid and setting the timestep succeeds, false otherwise.
 *         The timestep is valid if "seconds" is within the interval
 *         [MIN_SIMULATION_TIME_STEP, MAX_SIMULATION_TIME_STEP]
 */
extern bool setLiveTimeStep(double seconds);

// standby/return-to-center haptic tuning parameters, used by standbyModeUpdate
// in LJ.cpp and changeable at runtime via the IPC
// "set k_return/k_dampen/return_delay" commands
extern std::atomic<double> kReturn;
extern std::atomic<double> kDampen;
extern std::atomic<double> returnDelaySeconds;

// bounds accepted for the above, enforced by their setLiveXxx validators
constexpr double MIN_SETTLING_ERROR = 0.001;
constexpr double MAX_SETTLING_ERROR = 1.0;
constexpr double MIN_K_RETURN = 0.0;
constexpr double MAX_K_RETURN = 500.0;
constexpr double MIN_K_DAMPEN = 0.0;
constexpr double MAX_K_DAMPEN = 50.0;
constexpr double MIN_RETURN_DELAY_SECONDS = 0.0;
constexpr double MAX_RETURN_DELAY_SECONDS = 30.0;

// validated setters for the standby/return tuning parameters, same
// fail-closed contract as setLiveTimeStep
bool setLiveKReturn(double value);
bool setLiveKDampen(double value);
bool setLiveReturnDelay(double value);

// overall scale (0 = no feedback, 1 = full strength) applied to the force
// sent to the physical haptic device, used by updateHaptics in LJ.cpp.
// Overridable at launch via HAPTIC_DEVICE_FORCE_SCALE and changeable at
// runtime via the IPC "set force_scale" command / launcher UI - lets owners
// of older/more worn devices turn feedback down to reduce wear.
extern std::atomic<double> hapticForceScale;

constexpr double MIN_FORCE_SCALE = 0.0;
constexpr double MAX_FORCE_SCALE = 1.0;

constexpr double MIN_MAX_FORCE_OUTPUT = 0.0;
constexpr double MAX_MAX_FORCE_OUTPUT = 10.0;
// validated setter for hapticForceScale, same fail-closed contract as setLiveTimeStep
bool setLiveForceScale(double value);
bool setLiveMaxOutput(double value);

// advance to the next non-anchored atom / next preset camera angle
void switchCurrentAtom();
void switchCamera();

// nudge the current (controlled) atom one keyboard step along the camera's
// right/up/look axes; each argument is -1, 0, or 1
void relCamApplyForceToCurrent(chai3d::cVector3d direction);

/**
 * @brief Sets the potential from the live controls
 * @param requested The requested potential to use. Only lj and morse work, for ASE needs
 *                  constructor arguments that are only available at launch time
 * @return Whether setting the potential succeeded or not
 */
bool setLivePotential(const std::string &requested);

/**
 * @brief Sets how much to repeat in the x-axis from the live controls
 * @param value How many times to repeat in the x-axis
 * @return true if the value is valid
 */
bool setLiveRepeatX(int value);
/**
 * @brief Sets how much to repeat in the y-axis from the live controls
 * @param value How many times to repeat in the y-axis
 */
bool setLiveRepeatY(int value);
/**
 * @brief Sets how much to repeat in the z-axis from the live controls
 * @param value How many times to repeat in the z-axis
 */
bool setLiveRepeatZ(int value);

extern bool showDebug; // debug menu toggle
extern std::vector<chai3d::cLabel *> debugLabels; // debug labels 
extern std::vector<chai3d::cLabel *> debugAtomLabels; // atom index labels 
extern std::vector<chai3d::cVector3d> initialPositions; // initial positions for reset
extern int currentIndex; // current atom index
extern std::atomic<double> displayedTemperature; // current measured temperature of the system
extern chai3d::cLabel *temperatureLabel; // temperature label