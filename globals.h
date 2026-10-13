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
enum MouseState { MOUSE_IDLE, MOUSE_SELECTION, MOUSE_BOX_SELECTION };
enum LocalPotential { LENNARD_JONES, MORSE, ASE };
enum class HapticMode { Position, Standby, Force };

// map of atom stringnames by atomic number
extern std::unordered_map<int, std::string> atomStringNames;

// map of atom weights by atomic number
extern std::unordered_map<int, double> atomWeights;

// map of atom colors by atomic number
extern std::unordered_map<int, std::tuple<const GLfloat, const GLfloat, const GLfloat>> atomColors;

// Calculator object for force and potential energy calculatorString
extern Calculator* calculatorPtr;

// vector holding hotkey key labels
extern std::vector<chai3d::cLabel *> hotkeyKeys;

// vector holding function key labels (must be separate for formatting)
extern std::vector<chai3d::cLabel *> hotkeyFunctions;

// a camera to render the world in the window display
extern chai3d::cCamera *camera;

// for updating camera position
extern double rho;

// sphere objects
extern std::vector<Atom *> spheres;

// coordinates of central atom
extern double centerCoords[3];

// a pointer to the selected object
extern Atom *selectedAtom;

// mouse state
extern MouseState mouseState;

// position of mouse click.
extern cVector3d selectedPoint;

// current width of window
extern int width;

// current height of window
extern int height;

// offset between the position of the mouse click on the object and the object
// reference frame location.
extern cVector3d selectedAtomOffset;

// screen-space selection box state, stored in framebuffer pixels with origin at
// the bottom-left to match CHAI3D front-layer coordinates
extern double selectionStartX;
extern double selectionStartY;
extern double selectionCurrentX;
extern double selectionCurrentY;
extern cShapeLine *selectionBoxLines[4];

extern GLFWwindow *window;

extern bool fullscreen;

extern int swapInterval;

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
constexpr double MIN_SIMULATION_TIME_STEP = 1.0;
constexpr double MAX_SIMULATION_TIME_STEP = 30.0;

// validates and applies a new simulation time step; returns false (leaving
// the current value untouched) if the value is non-finite or out of
// [MIN_SIMULATION_TIME_STEP, MAX_SIMULATION_TIME_STEP]
bool setLiveTimeStep(double seconds);

// standby/return-to-center haptic tuning parameters, used by standbyModeUpdate
// in LJ.cpp and changeable at runtime via the IPC
// "set settling_err/k_return/k_dampen/return_delay" commands
extern std::atomic<double> settlingError;
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
bool setLiveSettlingError(double value);
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

// validated setter for hapticForceScale, same fail-closed contract as setLiveTimeStep
bool setLiveForceScale(double value);

// advance to the next non-anchored atom / next preset camera angle
void switchCurrentAtom();
void switchCamera();

// nudge the current (controlled) atom one keyboard step along the camera's
// right/up/look axes; each argument is -1, 0, or 1
void moveCurrentAtom(double rightAmount, double upAmount, double forwardAmount);

// swap the live calculator between "lj" and "morse"; returns false (and leaves
// the current calculator untouched) for any other request, since ASE
// calculators need constructor arguments only available at launch time
bool setLivePotential(const std::string &requested);
// debug menu toggle
extern bool showDebug;

// debug labels 
extern std::vector<chai3d::cLabel *> debugLabels;

// atom index labels 
extern std::vector<chai3d::cLabel *> debugAtomLabels;

// initial positions for reset
extern std::vector<chai3d::cVector3d> initialPositions;

// current atom index
extern int currentIndex;

// current measured temperature of the system
extern std::atomic<double> displayedTemperature;

// temperature label
extern chai3d::cLabel *temperatureLabel;

// barrier energy visualization label
