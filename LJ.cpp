//==============================================================================
/*
 Software License Agreement (BSD License)
 Copyright (c) 2003-2016, CHAI3D.
 (www.chai3d.org)
 All rights reserved.
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:
 * Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above
 copyright notice, this list of conditions and the following
 disclaimer in the documentation and/or other materials provided
 with the distribution.
 * Neither the name of CHAI3D nor the names of its contributors may
 be used to endorse or promote products derived from this software
 without specific prior written permission.
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
 \author    <http://www.chai3d.org>
 \author    Francois Conti
 \version   3.2.0 $Rev: 1922 $
 */
//==============================================================================
//------------------------------------------------------------------------------
#include "atom.h"
#include "chai3d.h"
#include "globals.h"
#include "inputHandling.h"
#include "ipcServer.h"
#include "potentials.h"
#include "utility.h"
#include "boundaryConditions.h"
#include "customHapticDevice.h"
//------------------------------------------------------------------------------
#include <GLFW/glfw3.h>
#include <math.h>
#include <unistd.h>
#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <atomic>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <optional>
#include <unordered_map>

#include <stdexcept>

std::mutex umaMutex;
std::vector<std::vector<double>> latestUMA;
std::atomic<bool> umaReady(false);
std::atomic<bool> stopUMA(false);
//------------------------------------------------------------------------------
using namespace chai3d;
using namespace std;

extern int just_unanchored;
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// GENERAL SETTINGS
//------------------------------------------------------------------------------
// Stereo Mode
/*
 C_STEREO_DISABLED:            Stereo is disabled
 C_STEREO_ACTIVE:              Active stereo for OpenGL NVIDIA QUADRO
 cards C_STEREO_PASSIVE_LEFT_RIGHT:  Passive stereo where L/R images are
 rendered next to each other C_STEREO_PASSIVE_TOP_BOTTOM:  Passive stereo
 where L/R images are rendered above each other
 */
const double ASE_UNITS_TO_FS = 10.18;
cStereoMode stereoMode = C_STEREO_DISABLED;

// Fullscreen mode
bool fullscreen = false;

// debug menu toggle
bool showDebug = false;

// debug labels vector
vector<cLabel *> debugLabels;

// atom index labels vector
vector<cLabel *> debugAtomLabels;

// initial positions for reset
vector<cVector3d> initialPositions;

//------------------------------------------------------------------------------
// DECLARED CONSTANTS
//------------------------------------------------------------------------------
// Radius of each sphere
const double SPHERE_RADIUS = 0.008;
// number of cameras
const int NUM_CAM = 2;

// position of walls and ground
const double WALL_GROUND = 0.0 + SPHERE_RADIUS;
const double WALL_CEILING = 0.05; // 0.2;
const double WALL_LEFT = -0.05;   //-0.1;
const double WALL_RIGHT = 0.05;   // 0.2;
const double WALL_FRONT = 0.05;   // 0.08;
const double WALL_BACK = -0.05;   //-0.08;
const double SPHERE_STIFFNESS = 500.0;
const double SPHERE_MASS_SCALE_FACTOR = 0.02;
const double F_DAMPING = 0.25; // Force damping coefficient
const double V_DAMPING = 0.8;  // Velocity damping coefficient
const double A_DAMPING = 0.99; // Acceleration damping coefficient
const double K_MAGNET = 500.0;
const double HAPTIC_STIFFNESS = 1000.0;
const double SIGMA = 1.0;
const double EPSILON = 1.0;
const double MAX_ATOM_SPEED = 1.0;
const double MAX_ATOM_STEP = 0.01;

// Haptic spring-damper constants (to reduce oscillations)
const double K_HAPTIC_SPRING = 100.0;
const double K_HAPTIC_DAMPER = 5.0;    // Damping for force mode
const double K_RETURN_SPRING = 25.0;
const double K_RETURN_DAMPER = 2.0;    // Damping for standby return
const double K_POSITION_ATTRACTION = 25.0;
const double K_POSITION_DAMPER = 2.0;  // Damping for position mode
const double MAX_HAPTIC_ATOM_FORCE = 1.0;

// Hard safety ceiling on the force actually sent to the physical device, in
// the same force units as MAX_FORCE/ATTRACTION_MAX below. Applied AFTER the
// user-configurable hapticForceScale, so a runaway force spike (e.g. two
// atoms overlapping) can't slam the device regardless of the chosen
// feedback intensity - protects older/worn hardware from a hard jolt.
const double MAX_HAPTIC_OUTPUT_FORCE = 10.0;

// Scales the distance betweens atoms
const double DIST_SCALE = .02;

// atom pairs whose centers are closer than this (world units) are considered
// bonded and get a connecting line drawn between them when bond rendering is on
const double BOND_DISTANCE_THRESHOLD = SPHERE_RADIUS * 5.0;

// distance (world units) the current (controlled) atom moves per keyboard
// nudge via the I/J/K/L/O/P movement keys
const double ATOM_MOVE_STEP = SPHERE_RADIUS;

// boundary conditions
const double BOUNDARY_LIMIT = .01;
const double CAMERA_BOUNDARY_SCALE = 0.35;

//------------------------------------------------------------------------------
// DECLARED VARIABLES
//------------------------------------------------------------------------------
// calculator
Calculator *calculatorPtr;

// a world that contains all objects of the virtual environment
cWorld *world;

// a camera to render the world in the window display
cCamera *camera;

// a light source to illuminate the objects in the world
cSpotLight *light;

// a haptic device handler
cHapticDeviceHandler *handler;

// a pointer to the current haptic device
cGenericHapticDevicePtr hapticDevice;

std::atomic<HapticMode> hapticMode(HapticMode::Position);

// simulation time step in seconds; overridable at launch via
// HAPTIC_DEVICE_TIME_STEP and changeable live via the IPC "set timestep" command
std::atomic<double> simulationTimeStep(0.001);

// standby/return-to-center haptic tuning parameters used by standbyModeUpdate,
// changeable live via the IPC "set settling_err/k_return/k_dampen/return_delay"
// commands (see setLiveSettlingError/setLiveKReturn/setLiveKDampen/setLiveReturnDelay)
std::atomic<double> settlingError(0.05);
std::atomic<double> kReturn(25.0);
std::atomic<double> kDampen(0.0); // 0 = no damping, matching original return behavior
std::atomic<double> returnDelaySeconds(2.5);

// overall scale applied to the force sent to the physical haptic device,
// overridable at launch via HAPTIC_DEVICE_FORCE_SCALE and changeable live via
// the IPC "set force_scale" command / launcher UI. Lets owners of older/more
// worn devices turn down feedback strength to reduce wear, without touching
// the underlying simulation's spring/damping constants.
std::atomic<double> hapticForceScale(1.0);

// sphere objects
vector<Atom *> spheres;

// lines drawn between bonded atom pairs, keyed by sorted (sphere index) pairs.
// Lines are created lazily and hidden (not removed) when a pair un-bonds so
// they can be cheaply re-shown if the pair drifts back within range.
map<pair<int, int>, cShapeLine *> bondLines;

// a colored background
cBackground *background;

cLabel *hapticPositionLabel;

// a label to display the rate [Hz] at which the simulation is running
cLabel *labelRates;

// a label to show the potential energy
cLabel *LJ_num;

// label showing the # anchored
cLabel *num_anchored;

// a label to show whether or not the atoms are frozen
cLabel *isFrozen;

// a label to display the camera position
cLabel *camera_pos;

// a label to identify the potential energy surface
cLabel *potentialLabel;
cLabel *temperatureLabel;

// labels for the scope
cLabel *scope_upper;
cLabel *scope_lower;

// a flag that indicates if the haptic simulation is currently running
std::atomic<bool> simulationRunning{false};

// a flag that indicates if the haptic simulation has terminated
bool simulationFinished = true;

// mouse state
MouseState mouseState = MOUSE_IDLE;

// a frequency counter to measure the simulation graphic rate
cFrequencyCounter freqCounterGraphics;

// a frequency counter to measure the simulation haptic rate
cFrequencyCounter freqCounterHaptics;

// haptic thread
cThread *hapticsThread;

// a handle to window display context
GLFWwindow *window = NULL;

// a handle to slider control window
GLFWwindow *sliderWindow = NULL;

// current framebuffer (render) size in pixels.
// NOTE: on HiDPI / Retina displays this is LARGER than the window size in points.
int width = 0;
int height = 0;

// radius of the camera
double rho = .35; 

// a scope to monitor the potential energy
cScope *scope;

// global minimum for the given cluster size
double global_minimum;

// a pointer to the selected object
Atom *selectedAtom = NULL;

// offset between the position of the mmouse click on the object and the object
// reference frame location.
cVector3d selectedAtomOffset;

// position of mouse click.
cVector3d selectedPoint;

double selectionStartX = 0.0;
double selectionStartY = 0.0;
double selectionCurrentX = 0.0;
double selectionCurrentY = 0.0;
cShapeLine *selectionBoxLines[4] = {NULL, NULL, NULL, NULL};

// swap interval for the display context (vertical synchronization)

int swapInterval = 1;

std::atomic<bool> freezeAtoms(false); // determine if atoms should be frozen

std::atomic<bool> renderAtoms(true); // determine if atom spheres should be drawn
std::atomic<bool> renderForceVectors(true); // determine if force vector lines should be drawn
std::atomic<bool> renderBonds(true); // determine if bond lines should be drawn
double centerCoords[3] = {50.0, 50.0, 50.0}; // save coordinates of central atom

std::atomic<int> screenshotCounter(-2); // keep track of how long screenshot label has been displayed
std::atomic<int> writeConCounter(-2);  // keep track of how long write to con label has been displayed

LocalPotential energySurface = LENNARD_JONES; // default potential is Lennard Jones
bool global_min_known = true; // check if able to read in the global min
cPanel *helpPanel; // panel that displays hotkeys
cLabel *helpHeader; // help panel header

std::atomic<double> displayedPotentialEnergy(0.0);

std::atomic<double> displayedTemperature(0.0);
double lastPotentialEnergy = 0.0;
double potentialEnergyDerivative = 0.0;
std::atomic<int> displayedAnchoredCount(0);
std::recursive_mutex sceneMutex;
std::atomic<bool> hapticsThreadStarted(false);
int currentIndex = 0;
vector<cLabel *> hotkeyKeys; // vector holding hotkey key labels
vector<cLabel *> hotkeyFunctions; // vector holding function key labels (must be separate for formatting)
// Energy Barrier Explorer tracking
// screenshot notification label
cLabel *screenshotLabel;

// write to con notification label
cLabel *writeConLabel;

cVector3d hapticPosition;
static std::vector<cVector3d> prevPositions;

void printIntro();

void initializeGLFW();
void setOpenGLVersion();
void initializeGLEW();

void initializeWorld();
void initializeCamera();
void initializeLight();
void initializeHapticDevice();
void initializeAtomLabels();
void addDebugLabel(std::string text);


void placeAtoms(std::array<double, 9>& aseCell, std::array<int, 3>& asePbc, int argc, char *argv[]);
Atom* initializeAtom(cTexture2dPtr texture, int atomicNumber, double radius);
vector<cVector3d> generateShellPositions(int k, double radiusAngstroms);
vector<cVector3d> PolyhedronCords(int k, double radius);
vector<cVector3d> ThomsonCords(int k, double radius);
vector<cVector3d> FibonacciCords(int k, double radius);

void initializeAtomPosition(Atom *new_atom);
void initializeCalculator(int argc, char *argv[], std::array<double, 9> aseCell,
    std::array<int, 3> asePbc);
void initializeLabels();
void initializeHotkeyLabels();
void initializePotentialLabel();
void initializePotentialEnergyPlot();
void initializeHelpPanel();
void initializeHapticThread();
void initializeSliderUI();
void runGraphicsLoop();
void renderSliderWindow();
double getSimulationTimeStep();
double getCurrentTemp();
void updateSliderWindowTitle();
void sliderWindowSizeCallback(GLFWwindow *a_window, int a_width, int a_height);
void sliderWindowCursorPosCallback(GLFWwindow *a_window, double a_posX, double a_posY);
void sliderWindowMouseButtonCallback(GLFWwindow *a_window, int a_button, int a_action, int a_mods);
void sliderWindowKeyCallback(GLFWwindow *a_window, int a_key, int a_scancode, int a_action, int a_mods);

// callback when the framebuffer is resized (size in pixels, not window points)
void framebufferSizeCallback(GLFWwindow *a_window, int a_width, int a_height);

// callback when an error GLFW occurs
void errorCallback(int error, const char *a_description);

// this function updates the draw positions
void updateGraphics(void);

// recomputes which atom pairs are within BOND_DISTANCE_THRESHOLD of each
// other and shows/hides/creates the line connecting each bonded pair
void updateBonds(void);

// this function contains the main haptics simulation loop
void updateHaptics(void);

// this function advances the atom simulation for one time step
cVector3d stepSimulation(const cVector3d &position,
                         const double timeInterval,
                         const bool hasHapticDevice);

// nudge the current (controlled) atom one keyboard step along the camera's
// right/up/look axes; each argument is -1, 0, or 1
void moveCurrentAtom(double rightAmount, double upAmount, double forwardAmount);

// this function closes the application
void close(void);

// add a label to the world with default black text
void addLabel(cLabel *&label);

// Update camera text
void updateCameraLabel(cLabel *&camera_pos, cCamera *&camera);

// save configuration in .con file
void writeToCon(string fileName);

//------------------------------------------------------------------------------
// RESOURCE HELPERS
//------------------------------------------------------------------------------
static string getExecutableDir() {
  char buffer[4096];
  ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (length <= 0) {
    return "";
  }
  buffer[length] = '\0';
  string executablePath(buffer);
  size_t separator = executablePath.find_last_of('/');
  if (separator == string::npos) {
    return "";
  }
  return executablePath.substr(0, separator);
}

// RETURNS THE PATHS TO RESOURCES FILES
static vector<string> getResourceSearchRoots() {
  vector<string> roots;
  // root resource path
  string resourceRoot;
  if (!resourceRoot.empty()) {
    roots.push_back(resourceRoot);
  }
  string executableDir = getExecutableDir();
  if (!executableDir.empty()) {
    roots.push_back(executableDir + "/");
    roots.push_back(executableDir + "/../");
  }

  roots.push_back("./");
  roots.push_back("../");
  roots.push_back("./bin/");
  roots.push_back("../bin/");

  return roots;
}

template <typename Loader>
static bool loadChaiResource(Loader loader, const string &relativePath) {
  for (const string &root : getResourceSearchRoots()) {
    const string candidate = root + relativePath;
    if (loader(candidate.c_str())) {
      return true;
    }
  }
  return false;
}

static bool isFiniteVector(const cVector3d &value) {
  return std::isfinite(value.x()) && std::isfinite(value.y()) && std::isfinite(value.z());
}

// THIS CALCULATES THE MAGNTIUDES OF THE VECTORS  
static cVector3d clampVectorMagnitude(const cVector3d &value, const double maxMagnitude) {
  if (!isFiniteVector(value)) {
    return cVector3d(0.0, 0.0, 0.0);
  }
  const double length = value.length();
  if (length > maxMagnitude && length > 0.0) {
    return maxMagnitude * cNormalize(value);
  }
  return value;
}

//==============================================================================
/*
 LJ.cpp
 This program simulates LJ clusters of varying sizes using modified
 sphere primitives (atom.cpp). All dynamics and collisions are computed in the
 haptics thread.
 */
//==============================================================================
// current camera
int curr_camera = 1;

// on Windows, double-clicking the .exe directly (rather than launching it
// through launcher/main.py, which supplies the required arguments) used to
// crash instantly: the console window this project builds as opens, an
// unhandled exception fires (e.g. missing haptic mode argument) and
// std::terminate closes the window again before anyone can read why. main()
// below catches that and keeps the window open with the error instead.
int runApplication(int argc, char *argv[]) {
  printIntro();
  srand(time(NULL)); // initialize random seed
  
  // OPEN GL - WINDOW DISPLAY
  initializeGLFW();
  initializeGLEW();

  // WORLD - CAMERA - LIGHTING
  initializeWorld();
  initializeCamera();
  initializeLight();
  
  // HAPTIC DEVICE
  initializeHapticDevice();
  
  if (argc < 2) {
    throw std::runtime_error("Missing haptic mode argument");
  }
  string hapticModeStr = argv[1];  
  if (hapticModeStr == "force" || hapticModeStr == "f") {
    hapticMode = HapticMode::Force;
  } else if (hapticModeStr == "position" || hapticModeStr == "p") {
    hapticMode = HapticMode::Position;
  } else if (hapticModeStr == "standby" || hapticModeStr == "s") {
    hapticMode = HapticMode::Standby;
  } else {
    throw std::runtime_error("First argument must be a haptic mode: \"force\", \"position\", \"standby\"");
  }

  // Declare variables needed for calculator constructor (cell, pbc), atoms object
  // (mass, atomic number), and placing of initial atoms (positions)
  std::array<double, 9> aseCell = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  std::array<int, 3> asePbc = {0, 0, 0};

  // PBC argument (argv[5]): "on" forces periodic boundaries on in all three
  // directions, "off" forces them off, and "keep" (or omitting the argument)
  // leaves whatever the loaded structure file specified untouched.
  string pbcMode = "keep";
  if (argc > 5) {
    pbcMode = argv[5];
    for (char &c : pbcMode) {
      c = tolower(c);
    }
  }

  // PLACE ATOMS
  placeAtoms(aseCell, asePbc, argc, argv);
  initializeAtomLabels();
  for (int i = 0; i < spheres.size(); i++) {
    initialPositions.push_back(spheres[i]->getLocalPos());
  }

  if (pbcMode == "on" || pbcMode == "true" || pbcMode == "1" || pbcMode == "yes") {
    asePbc = {1, 1, 1};
  } else if (pbcMode == "off" || pbcMode == "false" || pbcMode == "0" || pbcMode == "no") {
    asePbc = {0, 0, 0};
  }

  // determine potential if specified
  if (argc > 3) {
    initializeCalculator(argc, argv, aseCell, asePbc);
  } else {
    cerr << "No potential specified. Defaulting to Lennard-Jones." << endl;
    calculatorPtr = new ljCalculator();
  }

  // WIDGETS
  // helpPanel must be added to the front layer before the hotkey labels
  // (added inside initializeLabels) so the labels draw on top of the panel
  // background instead of being occluded by it.
  initializeHelpPanel();
  initializeLabels();
  initializePotentialEnergyPlot();

  // initial time step override, e.g. from the desktop launcher UI
  if (const char *timeStepEnv = std::getenv("HAPTIC_DEVICE_TIME_STEP")) {
    setLiveTimeStep(atof(timeStepEnv));
  }

  // initial haptic feedback intensity override, e.g. from the desktop
  // launcher UI; lets owners of older/more worn devices start already turned
  // down instead of having to dial it back after every launch
  if (const char *forceScaleEnv = std::getenv("HAPTIC_DEVICE_FORCE_SCALE")) {
    setLiveForceScale(atof(forceScaleEnv));
  }

  // IPC SERVER - lets the desktop launcher UI query status and change
  // parameters (freeze, haptic mode, potential, anchors, time step) while running
  int ipcPort = 8765;
  if (const char *portEnv = std::getenv("HAPTIC_DEVICE_CMD_PORT")) {
    ipcPort = atoi(portEnv);
    if (ipcPort <= 0) {
      ipcPort = 8765;
    }
  }
  startIpcServer(ipcPort);

  initializeSliderUI();
  

  // START SIMULATION
  initializeHapticThread();
  
  // MAIN GRAPHIC LOOP
  runGraphicsLoop();
  close();

  // close window
  if (sliderWindow != NULL) {
    glfwDestroyWindow(sliderWindow);
    sliderWindow = NULL;
  }
  glfwDestroyWindow(window);
  window = NULL;

  glfwTerminate(); // terminate GLFW library
  return 0; // exit
}

int main(int argc, char *argv[]) {
  try {
    return runApplication(argc, argv);
  } catch (const std::exception &e) {
    cerr << endl << "Fatal error: " << e.what() << endl;
    cerr << "(run this binary through launcher/main.py, or pass the haptic "
            "mode argument yourself - see README.md)" << endl;
    cerr << "Press Enter to close this window..." << endl;
    cin.get();
    return 1;
  }
}

void framebufferSizeCallback(GLFWwindow *a_window, int a_width, int a_height) {
  // update framebuffer (pixel) size used for rendering
  width = a_width;
  height = a_height;
}

void errorCallback(int a_error, const char *a_description) {
  cout << "Error: " << a_description << endl;
}

void printIntro() {
  cout << endl;
  cout << "-----------------------------------" << endl;
  cout << "CHAI3D" << endl;
  cout << "Press CTRL for help" << endl;
  cout << "-----------------------------------" << endl
       << endl
       << endl;
  cout << endl
       << endl;
}

void initializeGLFW() {
  if (!glfwInit()) {
    cSleepMs(1000);
    throw std::runtime_error("Failed to initialize GLFW library!");
  }

  glfwSetErrorCallback(errorCallback); // set error callback
  setOpenGLVersion();
  // set active stereo mode
  stereoMode == C_STEREO_ACTIVE ? glfwWindowHint(GLFW_STEREO, GL_TRUE)
                                : glfwWindowHint(GLFW_STEREO, GL_FALSE);

  // compute desired size of window
  const GLFWvidmode *mode = glfwGetVideoMode(glfwGetPrimaryMonitor());

  //    const w *mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
  int windowWidth = 0.8 * mode->height;
  int windowHeight = 0.5 * mode->height;
  int windowX = 0.5 * (mode->width - windowWidth);
  int windowY = 0.5 * (mode->height - windowHeight);

  // create display context
  window = glfwCreateWindow(windowWidth, windowHeight, "CHAI3D", NULL, NULL);
  if (!window) {
    cSleepMs(1000);
    glfwTerminate();
    throw std::runtime_error("Failed to create window!");
  }

  sliderWindow = glfwCreateWindow(340, 170, "Controls", NULL, window);
  if (!sliderWindow) {
    cSleepMs(1000);
    glfwTerminate();
    throw std::runtime_error("Failed to create slider window!");
  }

  glfwGetFramebufferSize(window, &width, &height); // framebuffer size in pixels (HiDPI-aware)
  glfwSetWindowPos(window, windowX, windowY); // set position of window
  glfwSetWindowPos(sliderWindow, windowX + windowWidth + 20, windowY);
  glfwSetKeyCallback(window, keyCallback); // set key callback
  glfwSetCursorPosCallback(window, mouseMotionCallback); // set mouse position callback
  glfwSetMouseButtonCallback(window, mouseButtonCallback); // set mouse button callback
  glfwSetFramebufferSizeCallback(window, framebufferSizeCallback); // track render size on resize
  glfwSetCursorPosCallback(sliderWindow, sliderWindowCursorPosCallback);
  glfwSetMouseButtonCallback(sliderWindow, sliderWindowMouseButtonCallback);
  glfwSetWindowSizeCallback(sliderWindow, sliderWindowSizeCallback);
  glfwSetKeyCallback(sliderWindow, sliderWindowKeyCallback);
  glfwMakeContextCurrent(window); // set current display context
  glfwSwapInterval(swapInterval); // sets the swap interval for the current display context
}

void setOpenGLVersion() {
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
}

void initializeGLEW() {
  #ifdef GLEW_VERSION
  if (glewInit() != GLEW_OK) {
    glfwTerminate();
    throw std::runtime_error("Failed to initialize GLEW library!");
  }
  #endif
}
void initializeWorld() {
  world = new cWorld();
  world->m_backgroundColor.setWhite();
  world->setShadowIntensity(0.3); // set shadow factor
}

void initializeCamera() {
  camera = new cCamera(world);
  world->addChild(camera);

  // creates the radius, origin reference, along with the zenith and azimuth direction vectors
  cVector3d origin(0.0, 0.0, 0.0);
  cVector3d zenith(0.0, 0.0, 1.0);
  cVector3d azimuth(1.0, 0.0, 0.0);

  // sets the camera's references of the origin, zenith, and azimuth
  camera->setSphericalReferences(origin, zenith, azimuth);

  // sets the camera's position to have a radius of .1, located at 0 radians (vertically and horizontally)
  camera->setSphericalRad(rho, 0, 0);
  // set the near and far clipping planes of the camera anything in front or behind these clipping
  // planes will not be rendered
  camera->setClippingPlanes(0.01, 10.0);

  camera->setStereoMode(stereoMode);  // set stereo mode

  // set stereo eye separation and focal length (applies only if stereo is enabled)
  camera->setStereoEyeSeparation(0.03);
  camera->setStereoFocalLength(1.8);
  camera->setMirrorVertical(false); // set vertical mirrored display mode

    // create a background
  background = new cBackground();
  camera->m_backLayer->addChild(background);

  // set aspect ration of background image a constant
  background->setFixedAspectRatio(true);

  // load background image
  bool fileload = loadChaiResource(
      [&](const char *path)
      { return background->loadFromFile(path); },
      "resources/images/background.png");
  if (!fileload) {
    close();
    throw std::runtime_error("Failed to load background image!");
  }
}

void initializeLight() {
  light = new cSpotLight(world); // create a light source
  world->addChild(light); // attach light to camera
  light->setEnabled(true); // enable light source
  light->setLocalPos(0.0, 0.3, 0.4); // position the light source
  light->setDir(0.0, -0.25, -0.4); // define the direction of the light beam
  light->setShadowMapEnabled(false); // enable this light source to generate shadows
  light->m_shadowMap->setQualityHigh(); // set the resolution of the shadow map
  light->setCutOffAngleDeg(30); // set light cone half angle
}

void initializeHapticDevice() {
  handler = new cHapticDeviceHandler(); // create a haptic device handler
  // get access to the first available haptic device
  double hapticDeviceMaxStiffness;   // highest stiffness the current haptic device can render
  if (handler->getNumDevices() > 0) {
    handler->getDevice(hapticDevice, 0);
  }
#ifndef _WIN32
  if (!hapticDevice) {
    // No CHAI3D-recognized device (e.g. Falcon) found - fall back to the
    // custom 1-DOF capstan device (see customHapticDevice.h and
    // firmware/haptic_motor_controller) if the user opted in via env var.
    const char *serialPort = getenv("HAPTIC_DEVICE_SERIAL_PORT");
    if (serialPort != nullptr) {
      hapticDevice = std::make_shared<CustomHapticDevice>(serialPort);
    }
  }
#endif
  if (hapticDevice) {
    // retrieve the highest stiffness this device can render
    hapticDeviceMaxStiffness = hapticDevice->getSpecifications().m_maxLinearStiffness;

    // if the haptic devices carries a gripper, enable it to behave like a user switch
    hapticDevice->setEnableGripperUserSwitch(true);
  } else {
    hapticDeviceMaxStiffness = HAPTIC_STIFFNESS;
    cout << "No haptic device detected. Running in keyboard/mouse-only mode." << endl;
  }
}


void placeAtomsAse(std::array<double, 9>& aseCell, std::array<int, 3>& asePbc, cTexture2dPtr texture, int argc, char *argv[]) {
  AseStructureData structure;
  // Optional repeat factors: argv[6]=x, argv[7]=y, argv[8]=z. Each defaults to
  // 1 if not given, and values < 1 are ignored (they would zero out the cell).
  std::array<int, 3> repeat = {1, 1, 1};
  for (int i = 0; i < 3; i++) {
    if (argc > 6 + i) {
      int value = atoi(argv[6 + i]);
      if (value > 0) {
        repeat[i] = value;
      }
    }
  }
  try {
    structure = loadAseStructure(argv[2], repeat);
  } catch (const std::exception &ex) {
    close();
    throw std::runtime_error(ex.what());
  }
  const std::vector<std::array<double, 3>> &positions = structure.positions;
  const std::vector<int> &startingAtomicNrs = structure.atomicNumbers;
  const std::vector<double> &startingRadii = structure.radii;
  // comment out below for no pbc
  aseCell = structure.cell;
  asePbc = structure.pbc;
  const int nAtoms = static_cast<int>(positions.size());

  for (int i = 0; i < nAtoms; i++) {
    Atom *newAtom = initializeAtom(texture, startingAtomicNrs[i], startingRadii[i] * 0.02); // Create atom pointer`
    // Set the positions of all atoms
    if (i == 0) {
      // make very first atom the current atom
      newAtom->setCurrent(true);
      // get coordinates from pPositionTriplet
      for (int j = 0; j < 3; j++) {
        centerCoords[j] = positions[0][static_cast<size_t>(j)];
      }
      newAtom->setLocalPos(0.0, 0.0, 0.0); // set first atom at center of view
    } else {
      // newAtom->setAnchor(true); // Anchor by default
        // scale coordinates and insert
        if (hapticMode == HapticMode::Standby) {
          newAtom->setLocalPos(
              0.02 * (positions[i][0] - centerCoords[0]  + 0.1), // position offset -- should probably disappear once we get boxes working
              0.02 * (positions[i][1] - centerCoords[1] + 0.1),
              0.02 * (positions[i][2] - centerCoords[2] + 0.1) );
        } else {
          newAtom->setLocalPos(
              0.02 * (positions[i][0] - centerCoords[0]), // position offset -- should probably disappear once we get boxes working
              0.02 * (positions[i][1] - centerCoords[1]),
              0.02 * (positions[i][2] - centerCoords[2]));
        }
    }
  }
}

void placeAtoms(std::array<double, 9>& aseCell, std::array<int, 3>& asePbc, int argc, char *argv[]) {
  cTexture2dPtr texture = cTexture2d::create(); // create texture
  // load texture file
  bool fileload = loadChaiResource([&](const char *path)
      { return texture->loadFromFile(path); },
      "resources/images/grayball.jpg");
  if (!fileload){
    close();
    throw std::runtime_error("Failed to load texture!");
  }

  // either no additional arguments were given or second argument was an integer
  if (argc == 2 || isNumber(argv[2])) {
    // k is the number of atoms surrounding the current center atom.
    int k = argc > 2 ? atoi(argv[2]) : 5;
    if (k < 0) {
      k = 0;
    }
    int numSpheres = k + 1;
    // argv[4]/argv[5] are always the ASE spec and PBC mode (see main()), never
    // a radius, so there is no CLI slot to override this default.
    const double shellRadiusAngstroms = 5.0;
    vector<cVector3d> positions = generateShellPositions(k, shellRadiusAngstroms);
    for (int i = 0; i < numSpheres; i++) {
      // initialize atom with texture and atomic number of 1 (hydrogen)
      Atom *new_atom = initializeAtom(texture, 1, SPHERE_RADIUS); 
      if (i == 0) {
        new_atom->setCurrent(true); // set the first sphere to the current
      } else {
        new_atom->setLocalPos(positions[i - 1]);
      }
    }
  } else // read in specified file
    placeAtomsAse(aseCell, asePbc, texture, argc, argv);

  // Done reading any sort of info.
  for (int i = 0; i < spheres.size(); i++) {
    spheres[i]->setVelocity(0);
  }
}

Atom* initializeAtom(cTexture2dPtr texture, int atomicNumber, double radius = SPHERE_RADIUS) {
  Atom *new_atom = new Atom(radius, atomicNumber); // create a atom and define its radius
  spheres.push_back(new_atom); // store pointer to atom
  world->addChild(new_atom); // add atom to world
  world->addChild(new_atom->getVelVector()); // add line to world

  // set graphic properties of sphere
  new_atom->setTexture(texture);
  new_atom->m_texture->setSphericalMappingEnabled(true);
  new_atom->setUseTexture(true);
  return new_atom;
}

vector<cVector3d> generateShellPositions(int k, double radiusAngstroms) {
  const double radius = radiusAngstroms * DIST_SCALE;

  if (k <= 0) {
    return vector<cVector3d>();
  }

  if ((k == 4) || (k == 6) || (k == 8) || (k == 12) || (k == 20)) {
    return PolyhedronCords(k, radius);
    //
  } else if (k <= 100) {
    return ThomsonCords(k, radius);
  }

  return FibonacciCords(k, radius);
}

static cVector3d scaledToRadius(const cVector3d &position, double radius) {
  double length = position.length();
  if (length <= 1e-12) {
    return cVector3d(0.0, 0.0, radius);
  }
  return position *(radius / length);
}

static void addScaledVertex(vector<cVector3d> &positions, double x, double y, double z, double radius) {
  positions.push_back(scaledToRadius(cVector3d(x, y, z), radius));
}

vector<cVector3d> PolyhedronCords(int k, double radius) {
  vector<cVector3d> positions;
  positions.reserve(k);
  const double phi = (1.0 + sqrt(5.0)) / 2.0;
  const double invPhi = 1.0 / phi;

  if (k == 4) {
    addScaledVertex(positions,  1.0,  1.0,  1.0, radius);
    addScaledVertex(positions,  1.0, -1.0, -1.0, radius);
    addScaledVertex(positions, -1.0,  1.0, -1.0, radius);
    addScaledVertex(positions, -1.0, -1.0,  1.0, radius);
  } else if (k == 6) {
    addScaledVertex(positions,  1.0,  0.0,  0.0, radius);
    addScaledVertex(positions, -1.0,  0.0,  0.0, radius);
    addScaledVertex(positions,  0.0,  1.0,  0.0, radius);
    addScaledVertex(positions,  0.0, -1.0,  0.0, radius);
    addScaledVertex(positions,  0.0,  0.0,  1.0, radius);
    addScaledVertex(positions,  0.0,  0.0, -1.0, radius);
  } else if (k == 8) {
    //test, might have to hardcode postiitons if this doesnt work
    for (int x = -1; x <= 1; x += 2) {
      for (int y = -1; y <= 1; y += 2) {
        for (int z = -1; z <= 1; z += 2) {
          addScaledVertex(positions, x, y, z, radius);
        }
      }
    }
  } else if (k == 12) {
    for (int y = -1; y <= 1; y += 2) {
      for (int z = -1; z <= 1; z += 2) {
        addScaledVertex(positions, 0.0, y, z * phi, radius);
      }
    }
    for (int x = -1; x <= 1; x += 2) {
      for (int y = -1; y <= 1; y += 2) {
        addScaledVertex(positions, x, y * phi, 0.0, radius);
      }
    }
    for (int x = -1; x <= 1; x += 2) {
      for (int z = -1; z <= 1; z += 2) {
        addScaledVertex(positions, x * phi, 0.0, z, radius);
      }
    }
  } else if (k == 20) {
    for (int x = -1; x <= 1; x += 2) {
      for (int y = -1; y <= 1; y += 2) {
        for (int z = -1; z <= 1; z += 2) {
          addScaledVertex(positions, x, y, z, radius);
        }
      }
    }
    for (int y = -1; y <= 1; y += 2) {
      for (int z = -1; z <= 1; z += 2) {
        addScaledVertex(positions, 0.0, y * invPhi, z * phi, radius);
      }
    }
    for (int x = -1; x <= 1; x += 2) {
      for (int y = -1; y <= 1; y += 2) {
        addScaledVertex(positions, x * invPhi, y * phi, 0.0, radius);
      }
    }
    for (int x = -1; x <= 1; x += 2) {
      for (int z = -1; z <= 1; z += 2) {
        addScaledVertex(positions, x * phi, 0.0, z * invPhi, radius);
      }
    }
  }

  return positions;
}

vector<cVector3d> ThomsonCords(int k, double radius) {
  vector<cVector3d> positions = FibonacciCords(k, radius);
  if (k <= 1) {
    return positions;
  }

  const int iterations = 600;
  const double baseStep = radius * 0.04;

  for (int iter = 0; iter < iterations; iter++) {
    vector<cVector3d> forces(k, cVector3d(0.0, 0.0, 0.0));

    for (int i = 0; i < k; i++) {
      for (int j = i + 1; j < k; j++) {
        cVector3d diff = positions[i] - positions[j];
        double dist = diff.length();
        if (dist <= 1e-9) {
          continue;
        }

        cVector3d force = diff * (1.0 / (dist * dist * dist));
        forces[i] += force;
        forces[j] -= force;
      }
    }

    double step = baseStep * (1.0 - (0.75 * iter / iterations));
    for (int i = 0; i < k; i++) {
      cVector3d normal = scaledToRadius(positions[i], 1.0);
      double radialForce = forces[i].x() * normal.x()
                         + forces[i].y() * normal.y()
                         + forces[i].z() * normal.z();
      cVector3d tangentForce = forces[i] - normal * radialForce;
      positions[i] = scaledToRadius(positions[i] + tangentForce * step, radius);
    }
  }

  return positions;
}

vector<cVector3d> FibonacciCords(int k, double radius) {
  vector<cVector3d> positions;
  positions.reserve(k);

  if (k <= 0) {
    return positions;
  }
  if (k == 1) {
    positions.push_back(cVector3d(0.0, 0.0, radius));
    return positions;
  }

  const double goldenAngle = M_PI * (3.0 - sqrt(5.0));

  for (int i = 0; i<k; i++) {
    double y = 1.0 - (2.0 * i) / (k - 1);
    double r = sqrt(1.0 - y * y);
    double theta = goldenAngle * i;

    positions.push_back(cVector3d(
      radius*cos(theta)*r,
      radius*y,
      radius*sin(theta)*r
    ));
  }

  return positions;
}

void initializeAtomPosition(Atom *new_atom) {
  bool inside_atom = true;
  auto iter{0};
  while (inside_atom) {
    if (iter <= 1000) {
      // Place atom at a random position
      new_atom->setInitialPosition();
    } else {
      // If there are too many failed attempts at placing the atom increase the radius in
      // which it can spawn
      new_atom->setInitialPosition(.115);
    }
    // Check that it doesn't collide with any others
    bool collision_detected = false;
    for (auto i{0}; i < spheres.size(); i++) {
      if (new_atom != spheres[i]) {
        auto dist_between = cDistance(new_atom->getLocalPos(), spheres[i]->getLocalPos());
        if (dist_between < SPHERE_RADIUS * 2) {
          // The number dist between is being compared to is the threshold for collision
          collision_detected = true;
          iter++;
          break;
        }
      }
    }
    if (!collision_detected){
      inside_atom = false;
    }
    
  }
  // cout << "Placing atom at " << new_atom->getLocalPos() << endl;
}

void initializeCalculator(int argc, char *argv[], std::array<double, 9> aseCell,
    std::array<int, 3> asePbc) {
    if (argc < 4) {
      energySurface = LENNARD_JONES;
      calculatorPtr = new ljCalculator();
      return;
    }
    string potential = argv[3];
    for (char &c : potential) {
      c = tolower(c);
    }
    if (potential == "morse" || potential == "m") {
      energySurface = MORSE;
      calculatorPtr = new morseCalculator();
    } else if (potential == "ase" || potential == "a") {
      energySurface = ASE;
      calculatorPtr = new aseCalculator((argc > 4) ? argv[4] : "", aseCell, asePbc);
    } else if (potential == "lennard-jones" || potential == "lj") {
      calculatorPtr = new ljCalculator();
    } else {
      cerr << "Warning: unknown potential '" << potential
           << "'. Defaulting to Lennard-Jones." << endl;
      energySurface = LENNARD_JONES;
      calculatorPtr = new ljCalculator();
    }
}

bool setLivePotential(const std::string &requested) {
  string potential = requested;
  for (char &c : potential) {
    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  }

  Calculator *newCalculator = nullptr;
  LocalPotential newSurface;
  if (potential == "lj" || potential == "lennard-jones") {
    newCalculator = new ljCalculator();
    newSurface = LENNARD_JONES;
  } else if (potential == "morse") {
    newCalculator = new morseCalculator();
    newSurface = MORSE;
  } else {
    // live switching to ASE is not supported since it needs constructor
    // arguments (structure file, calculator spec) only available at launch
    return false;
  }

  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  delete calculatorPtr;
  calculatorPtr = newCalculator;
  energySurface = newSurface;
  initializePotentialLabel();
  return true;
}

bool setLiveTimeStep(double seconds) {
  if (!std::isfinite(seconds) ||
      seconds < MIN_SIMULATION_TIME_STEP ||
      seconds > MAX_SIMULATION_TIME_STEP) {
    return false;
  }
  simulationTimeStep.store(seconds);
  return true;
}

bool setLiveSettlingError(double value) {
  if (!std::isfinite(value) || value < MIN_SETTLING_ERROR || value > MAX_SETTLING_ERROR) {
    return false;
  }
  settlingError.store(value);
  return true;
}

bool setLiveKReturn(double value) {
  if (!std::isfinite(value) || value < MIN_K_RETURN || value > MAX_K_RETURN) {
    return false;
  }
  kReturn.store(value);
  return true;
}

bool setLiveKDampen(double value) {
  if (!std::isfinite(value) || value < MIN_K_DAMPEN || value > MAX_K_DAMPEN) {
    return false;
  }
  kDampen.store(value);
  return true;
}

bool setLiveReturnDelay(double value) {
  if (!std::isfinite(value) || value < MIN_RETURN_DELAY_SECONDS || value > MAX_RETURN_DELAY_SECONDS) {
    return false;
  }
  returnDelaySeconds.store(value);
  return true;
}

bool setLiveForceScale(double value) {
  if (!std::isfinite(value) || value < MIN_FORCE_SCALE || value > MAX_FORCE_SCALE) {
    return false;
  }
  hapticForceScale.store(value);
  return true;
}

void initializeLabels() {
  addLabel(hapticPositionLabel); // label to read haptic device
  addLabel(labelRates); // create a label to display the haptic and graphic rate of the simulation
  addLabel(LJ_num); // potential energy label
  addLabel(num_anchored); // number anchored label
  
  cLabel *total_energy; // a label to display the total energy of the system
  addLabel(total_energy); // total energy label
  addLabel(isFrozen); // frozen state label
  addLabel(camera_pos); // camera position label
  addLabel(potentialLabel); // energy surface label
  addLabel(temperatureLabel);
  addDebugLabel("Force magnitude: ");
  addDebugLabel("Atom pos: ");
  addDebugLabel("Nearest neighbor: ");
  addDebugLabel("Max force: ");
  
  addLabel(scope_upper); // Add labels to the graph
  addLabel(scope_lower);

  hapticPositionLabel->setLocalPos(0, 50);

  cFontPtr notificationFont = NEW_CFONT_CALIBRI_20();
  writeConLabel = new cLabel(notificationFont);
  writeConLabel->m_fontColor.setBlack();
  screenshotLabel = new cLabel(notificationFont);
  screenshotLabel->m_fontColor.setBlack();
  camera->m_frontLayer->addChild(writeConLabel);
  camera->m_frontLayer->addChild(screenshotLabel);
  writeConLabel->setShowEnabled(false);
  screenshotLabel->setShowEnabled(false);

  screenshotLabel->setText("Screenshot taken");
  writeConLabel->setText("Con file written");

  initializePotentialLabel();

  temperatureLabel->setLocalPos(0, 90, 0);
  temperatureLabel->setText("Temperature: 0.00000 kT");

  camera_pos->setLocalPos(0, 30, 0);
  updateCameraLabel(camera_pos, camera);
}
void initializeAtomLabels() {
  cFontPtr atomLabelFont = NEW_CFONT_CALIBRI_20();
  for (int i = 0; i < spheres.size(); i++) {
    cLabel *label = new cLabel(atomLabelFont);
    label->m_fontColor.setBlack();
    label->setText(to_string(i));
    label->setShowEnabled(false);
    camera->m_frontLayer->addChild(label);
    debugAtomLabels.push_back(label);
  }
}

void initializeHotkeyLabels() {
  addHotkeyLabel("f", "toggle fullscreen");
  addHotkeyLabel("q, ESC", "quit program");
  addHotkeyLabel("a", "anchor all atoms");
  addHotkeyLabel("u", "unanchor all atoms");
  addHotkeyLabel("ARROW KEYS", "rotate camera");
  addHotkeyLabel("[", "zoom in");
  addHotkeyLabel("]", "zoom out");
  addHotkeyLabel("r", "reset camera");
  addHotkeyLabel("s", "screenshot atoms");
  addHotkeyLabel("c", "save configuration to .con");
  addHotkeyLabel("SPACE", "freeze atoms");
  addHotkeyLabel("1", "toggle atom rendering");
  addHotkeyLabel("2", "toggle force vector rendering");
  addHotkeyLabel("3", "toggle bond rendering");
  addHotkeyLabel("I, K", "move current atom up/down");
  addHotkeyLabel("J, L", "move current atom left/right");
  addHotkeyLabel("O, P", "move current atom forward/back");
  addHotkeyLabel("d", "toggle debug info");
  addHotkeyLabel("t", "reset atom structure");
  addHotkeyLabel("CTRL", "toggle help panel");
}

void initializePotentialLabel() {
  // set energy surface label
  potentialLabel->setLocalPos(0, 0);
  string potentialName;
  if (energySurface == LENNARD_JONES) {
    potentialName = "Lennard Jones Potential";
  } else if (energySurface == MORSE) {
    potentialName = "Morse Potential";
  } else if (energySurface == ASE) {
    potentialName = "ASE Potential";
  }
  potentialLabel->setText("Potential energy surface: " + potentialName);
}

void initializePotentialEnergyPlot() {
  // create a scope to plot potential energy
  scope = new cScope();
  scope->setLocalPos(0, 60);
  camera->m_frontLayer->addChild(scope);
  scope->setSignalEnabled(true, true, false, false);
  scope->setTransparencyLevel(.7);
  scope->setShowEnabled(false);
  global_minimum = getGlobalMinima(spheres.size());
  double lower_bound, upper_bound;
  if (global_minimum != 0 && (energySurface == LENNARD_JONES)) {
    if (global_minimum > -50) {
      upper_bound = 0;
      lower_bound = global_minimum - .5;
    } else {
      upper_bound = 0 + (global_minimum * .2);
      lower_bound = global_minimum - 3;
    }
    global_min_known = true;
  } else {
    upper_bound = 0;
    lower_bound = static_cast<int>(spheres.size()) * -3;
    global_minimum = 0;
    global_min_known = false;
  }
  scope->setRange(lower_bound, upper_bound);
  scope_upper->setText(cStr(upper_bound));
  scope_lower->setText(cStr(lower_bound));

  // Height was guessed and added manually - there's probably a better way
  // To do this but the scope height is protected
  scope_upper->setLocalPos(cAdd(scope->getLocalPos(), cVector3d(0, 180, 0)));
  scope_lower->setLocalPos(scope->getLocalPos());
  // TODO - make more legible
  // scope_upper->m_fontColor.setRed();
  // scope_lower->m_fontColor.setRed();
}

void initializeHelpPanel() {
  cColorf panelColor = cColorf();
  panelColor.setBlueCadet();

  helpPanel = new cPanel();
  helpPanel->setColor(panelColor);
  helpPanel->setSize(520, 600);
  camera->m_frontLayer->addChild(helpPanel);
  helpPanel->setShowPanel(false);

  initializeHotkeyLabels();

  cFontPtr headerFont = NEW_CFONT_CALIBRI_40();
  helpHeader = new cLabel(headerFont);
  helpHeader->m_fontColor.setBlack();
  helpHeader->setText("HOTKEYS AND INSTRUCTIONS");
  helpHeader->setShowPanel(false);
  helpHeader->setShowEnabled(false);
  camera->m_frontLayer->addChild(helpHeader);
}

void initializeHapticThread() {
  hapticsThread = nullptr; // create a thread which starts the main haptics rendering loop
  if (hapticDevice) {
    hapticsThread = new cThread();
    hapticsThread->start(updateHaptics, CTHREAD_PRIORITY_HAPTICS);
  }
}

void initializeprevPositions() {
  prevPositions.resize(spheres.size());
  for (int i = 0; i < spheres.size(); i++) {
    prevPositions[i] = spheres[i]->getLocalPos();
  }
}

void runGraphicsLoop() {
  framebufferSizeCallback(window, width, height); // initialize framebuffer size
  // main graphic loop

  initializeprevPositions();
  while (!glfwWindowShouldClose(window)) {
    glfwGetFramebufferSize(window, &width, &height); // framebuffer size in pixels (HiDPI-aware)
    if (!hapticDevice) {
      // Advance the sim by the slider-controlled fixed timestep. This used to be
      // min()'d with the real inter-frame time, which capped the timestep at the
      // frame duration on fast machines - so most of the Time Step slider's range
      // produced no visible change. Using the fixed value directly makes the whole
      // slider range (including the new slower minimum) actually take effect.
      double timeInterval = simulationTimeStep.load();
      freqCounterHaptics.signal(1);
      
      stepSimulation(cVector3d(0.0, 0.0, 0.0), timeInterval / ASE_UNITS_TO_FS, false);
    }
    updateGraphics(); // render graphics
    glfwSwapBuffers(window); // swap buffers
    renderSliderWindow();
    glfwPollEvents(); // process events
    freqCounterGraphics.signal(1); // signal frequency counter
  }
}



void close(void) { // stop the simulation
  static bool closed = false;
  if (!closed) {
    closed = true;
    stopIpcServer();
    simulationRunning = false;
    if (hapticsThreadStarted.load()) {
      // wait for graphics and haptics loops to terminate
      while (!simulationFinished) {
        cSleepMs(100);
      }
    }
    if (calculatorPtr != nullptr) {
      delete calculatorPtr;
      calculatorPtr = nullptr;
    }
    // delete resources
    delete hapticsThread;
    hapticsThread = nullptr;
    delete world;
    world = nullptr;
    delete handler;
    handler = nullptr;
  }
}

void updateCounters(cLabel *label, std::atomic<int> &counter) {
  int value = counter.load();
  if (value == 5000) {
    label->setShowEnabled(true);
  } else if (value == 0) {
    label->setShowEnabled(false);
  }
  counter--;
}

void updateLabels() {
  const bool debugVisible = showDebug;

  labelRates->setText(cStr(freqCounterGraphics.getFrequency(), 0) + " Hz / " +
                      cStr(freqCounterHaptics.getFrequency(), 0) + " Hz");
  labelRates->setLocalPos((int)(0.5 * (width - labelRates->getWidth())), 15);
  labelRates->setShowEnabled(debugVisible);

  double x = hapticPosition.get(0);
  double y = hapticPosition.get(1);
  double z = hapticPosition.get(2);
  hapticPositionLabel->setText("Position: " + cStr(x, 2) + ", " + cStr(y, 2) + ", " + cStr(z, 2));
  hapticPositionLabel->setShowEnabled(debugVisible);

  updateCameraLabel(camera_pos, camera);
  camera_pos->setShowEnabled(debugVisible);

  displayedTemperature.store(getCurrentTemp());
  temperatureLabel->setText("Temperature: " + cStr(displayedTemperature.load(), 5) + " kT");

  string trueFalse = freezeAtoms.load() ? "true" : "false";
  isFrozen->setText("Freeze simulation: " + trueFalse);
  isFrozen->setLocalPos((width - isFrozen->getWidth()) - 5, 15);
  isFrozen->setShowEnabled(debugVisible);

  screenshotLabel->setLocalPos(5, height - 20);
  updateCounters(screenshotLabel, screenshotCounter);

  writeConLabel->setLocalPos(5, height - 40);
  updateCounters(writeConLabel, writeConCounter);

  // Position the help panel, its header, and its hotkey rows relative to the
  // top of the window (rather than a fixed offset from a hypothetical taller
  // window). Row spacing shrinks if needed so every hotkey stays on-screen
  // instead of being pushed below y=0 and disappearing on shorter windows.
  const double topMargin = 10.0;
  const double headerReserve = 60.0;   // vertical space reserved for the header
  const double bottomMargin = 20.0;    // keep the last row off the panel's edge
  const double maxHelpPanelHeight = 500.0;
  const double defaultRowSpacing = 25.0;

  // Size and place the panel first. Its height is capped at maxHelpPanelHeight,
  // so the rows must be laid out against the PANEL height, not the raw window
  // height, or the bottom rows spill out below the panel on tall windows.
  double helpPanelHeight = cMin(maxHelpPanelHeight, cMax(0.0, (double)height - topMargin));
  helpPanel->setSize(520, helpPanelHeight);
  double panelTop = height - topMargin;
  helpPanel->setLocalPos(width - 550, panelTop - helpPanelHeight);
  helpHeader->setLocalPos(width - 490, panelTop - headerReserve + 20);

  // Shrink row spacing if the rows would not otherwise fit inside the panel
  // (between the header at the top and a small margin above the bottom edge).
  int numHotkeyRows = static_cast<int>(hotkeyKeys.size());
  double rowSpacing = defaultRowSpacing;
  if (numHotkeyRows > 1) {
    double availableRowSpace = helpPanelHeight - headerReserve - bottomMargin;
    double neededRowSpace = defaultRowSpacing * (numHotkeyRows - 1);
    if (availableRowSpace > 0 && availableRowSpace < neededRowSpace) {
      rowSpacing = availableRowSpace / (numHotkeyRows - 1);
    }
  }

  double rowStartY = panelTop - headerReserve;
  for (int i = 0; i < hotkeyKeys.size(); i++) {
    cLabel *tempKeyLabel = hotkeyKeys[i];
    cLabel *tempFuncLabel = hotkeyFunctions[i];
    double rowY = rowStartY - i * rowSpacing;
    tempKeyLabel->setLocalPos(width - 530, rowY);
    tempFuncLabel->setLocalPos(width - 350, rowY);
  }

  if (showDebug) {
    // current atom force magnitude
    cVector3d force = spheres[currentIndex]->getForce();
    debugLabels[0]->setText("Force magnitude: " + cStr(force.length(), 5));

    // current atom position
    cVector3d pos = spheres[currentIndex]->getLocalPos();
    debugLabels[1]->setText("Atom pos: (" + cStr(pos.x(), 3) + ", " + cStr(pos.y(), 3) + ", " + cStr(pos.z(), 3) + ")");

    // nearest neighbor distance
    double minDist = std::numeric_limits<double>::max();
    for (int i = 0; i < spheres.size(); i++) {
      if (i != currentIndex) {
        double dist = cDistance(spheres[currentIndex]->getLocalPos(), spheres[i]->getLocalPos());
        if (dist < minDist) minDist = dist;
      }
    }
    debugLabels[2]->setText("Nearest neighbor: " + cStr(minDist / 0.02, 5) + " Ang");

    // max force across all atoms
    double maxForce = 0;
    int maxForceIndex = 0;
    for (int i = 0; i < spheres.size(); i++) {
      double mag = spheres[i]->getForce().length();
      if (mag > maxForce) {
        maxForce = mag;
        maxForceIndex = i;
      }
    }
    debugLabels[3]->setText("Max force: " + cStr(maxForce, 5) + " (atom " + to_string(maxForceIndex) + ")");

    // position all debug labels
    for (int i = 0; i < debugLabels.size(); i++) {
      debugLabels[i]->setLocalPos(width - 250, 80 + i * 20);
      debugLabels[i]->setShowEnabled(true);
    }

    // atom index labels  
    for (int i = 0; i < debugAtomLabels.size(); i++) {
      cVector3d atomPos = spheres[i]->getLocalPos();
      cVector3d camPos = camera->getLocalPos();
      cVector3d camLook = camera->getLookVector();
      cVector3d camUp = camera->getUpVector();
      cVector3d camRight = camera->getRightVector();
      cVector3d toAtom = atomPos - camPos;
      double depth = toAtom.dot(camLook);
      if (depth > 0) {
        double fov = camera->getFieldViewAngleRad();
        double scaleY = (0.5 * height) / tan(0.5 * fov);
        double scaleX = scaleY;
        double screenX = (toAtom.dot(camRight) / depth) * scaleX + 0.5 * width;
        double screenY = (toAtom.dot(camUp) / depth) * scaleY + 0.5 * height;
        debugAtomLabels[i]->setLocalPos((int)screenX, (int)screenY);
        debugAtomLabels[i]->setShowEnabled(true);
      } else {
        debugAtomLabels[i]->setShowEnabled(false);
      }
    }

  } else {
    for (int i = 0; i < debugLabels.size(); i++) {
      debugLabels[i]->setShowEnabled(false);
    }
    for (int i = 0; i < debugAtomLabels.size(); i++) {
      debugAtomLabels[i]->setShowEnabled(false);
    }
  }
}

// Recomputes which atom pairs are within BOND_DISTANCE_THRESHOLD of each
// other and shows/hides/creates the cShapeLine connecting each bonded pair.
// Must be called with sceneMutex held.
void updateBonds(void) {
  if (!renderBonds.load()) {
    for (auto &entry : bondLines) {
      entry.second->setShowEnabled(false);
    }
    return;
  }

  set<pair<int, int>> bondedPairs;
  int numAtoms = static_cast<int>(spheres.size());
  for (int i = 0; i < numAtoms; i++) {
    for (int j = i + 1; j < numAtoms; j++) {
      double distance = cDistance(spheres[i]->getLocalPos(), spheres[j]->getLocalPos());
      if (distance < BOND_DISTANCE_THRESHOLD) {
        bondedPairs.insert(make_pair(i, j));
      }
    }
  }

  for (const pair<int, int> &bondedPair : bondedPairs) {
    cShapeLine *&line = bondLines[bondedPair];
    if (!line) {
      line = new cShapeLine(cVector3d(0, 0, 0), cVector3d(0, 0, 0));
      line->setLineWidth(3);
      line->m_colorPointA.setGrayDim();
      line->m_colorPointB.setGrayDim();
      world->addChild(line);
    }
    line->m_pointA = spheres[bondedPair.first]->getLocalPos();
    line->m_pointB = spheres[bondedPair.second]->getLocalPos();
    line->setShowEnabled(true);
  }

  for (auto &entry : bondLines) {
    if (bondedPairs.find(entry.first) == bondedPairs.end()) {
      entry.second->setShowEnabled(false);
    }
  }
}

void updateGraphics(void) {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  std::atomic<int> displayedAnchoredCount(0);
  // UPDATE WIDGETS
  updateLabels();

  // apply debug rendering toggles for atoms and force vectors, and recompute
  // bond lines for the current atom positions
  bool showAtoms = renderAtoms.load();
  bool showForceVectors = renderForceVectors.load();
  for (int i = 0; i < spheres.size(); i++) {
    spheres[i]->setShowEnabled(showAtoms);
    spheres[i]->getVelVector()->setShowEnabled(showForceVectors);
  }
  updateBonds();

  helpPanel->setLocalPos(width - 550, height - 600);
  helpHeader->setLocalPos(width - 490, height - 70);
  
  const bool debugVisible = showDebug;
  const double potentialEnergy = displayedPotentialEnergy.load();
  LJ_num->setText("Potential Energy: " + cStr(potentialEnergy, 5));
  LJ_num->setLocalPos(0, 15, 0);
  LJ_num->setShowEnabled(debugVisible);

  int anchoredCount = 0;
  for (int i = 0; i < spheres.size(); i++) {
    if (spheres[i]->isAnchor()) anchoredCount++;
  }
  num_anchored->setText(to_string(anchoredCount) + " anchored / " +
                        to_string(spheres.size()) + " total");
  num_anchored->setLocalPos((width - num_anchored->getWidth()) - 5, 0);
  num_anchored->setShowEnabled(debugVisible);

  scope->setShowEnabled(debugVisible);
  scope_upper->setShowEnabled(debugVisible);
  scope_lower->setShowEnabled(debugVisible);

  scope->setSignalValues(potentialEnergy, global_minimum);
  if (!global_min_known && global_minimum < scope->getRangeMin()) {
    auto new_lower = scope->getRangeMin() - 25;
    auto new_upper = scope->getRangeMax() - 25;
    scope->setRange(new_lower, new_upper);
    scope_upper->setText(cStr(scope->getRangeMax()));
    scope_lower->setText(cStr(scope->getRangeMin()));
  }

  // RENDER SCENE
  world->updateShadowMaps(false, false); // update shadow maps (if any)
  camera->renderView(width, height); // render world (width/height are framebuffer pixels)
  glFinish(); // wait until all GL commands are completed
  GLenum err = glGetError(); // check for any OpenGL errors
  if (err != GL_NO_ERROR)
    cout << "Error: " << gluErrorString(err) << endl;
}



void switchCamera() {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  switch (curr_camera) {
    case 1:
      camera->setSphericalPolarRad(0);
      camera->setSphericalAzimuthRad(0);
      break;
    case 2:
      camera->setSphericalPolarRad(0);
      camera->setSphericalAzimuthRad(M_PI);
      break;
    case 3:
      camera->setSphericalPolarRad(M_PI);
      camera->setSphericalAzimuthRad(M_PI);
      break;
    case 4:
      curr_camera = 0;
      camera->setSphericalPolarRad(M_PI);
      camera->setSphericalAzimuthRad(0);
      break;
  }
  curr_camera++;
}

void switchCurrentAtom() {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  if (spheres.empty()) {
    return;
  }
  currentIndex %= spheres.size();
  Atom* current = spheres[currentIndex];
  int prev_curr_atom = currentIndex;
  currentIndex = (currentIndex + 1) % spheres.size();  if (currentIndex < 0)
    currentIndex += spheres.size();
  int startAtom = currentIndex;
  while (spheres[currentIndex]->isAnchor()) {
    currentIndex = (currentIndex + 1) % spheres.size();    if (currentIndex < 0)
      currentIndex += spheres.size();
    if (currentIndex == startAtom)
      break;
  }

  current = spheres[currentIndex];
  current->setCurrent(true);

  Atom *prev = spheres[prev_curr_atom];
  prev->setCurrent(false);
}
void readButtons(bool buttons[4], bool buttonReset[4]) {
  for (int i = 0; i < 4; i++) {
    hapticDevice->getUserSwitch(i, buttons[i]);
    if (buttons[i]) {
      if (buttonReset[i]) {
        switch (i) {
          
          case 1:
            switchCurrentAtom();
            break;
          case 2:
            switchCamera();
            break;
          default:
            cout << "Button " << i << " has not yet been defined!" << endl;
            break;
        }
        buttonReset[i] = false;
      } 
    } else {
      buttonReset[i] = true;
    }
  }
}



double getDynamicBoundaryLimit() {
  if (!camera || width <= 0 || height <= 0) {
    return BOUNDARY_LIMIT;
  }

  const double aspect = static_cast<double>(width) / static_cast<double>(height);
  const double zoomDistance = camera->getSphericalRadius();
  const double safeDistance = (zoomDistance > 1e-6) ? zoomDistance : 0.1;
  const double halfHeight = safeDistance * tan(camera->getFieldViewAngleRad() * 0.5) * CAMERA_BOUNDARY_SCALE;
  const double halfWidth = halfHeight * aspect;
  return (halfWidth > halfHeight) ? halfWidth : halfHeight;
}

void getCameraAlignedBoundaryPlanes(cVector3d &northPlanePos,
                                   cVector3d &northPlaneNorm,
                                   cVector3d &southPlanePos,
                                   cVector3d &southPlaneNorm,
                                   cVector3d &eastPlanePos,
                                   cVector3d &eastPlaneNorm,
                                   cVector3d &westPlanePos,
                                   cVector3d &westPlaneNorm,
                                   cVector3d &forwardPlanePos,
                                   cVector3d &forwardPlaneNorm,
                                   cVector3d &backPlanePos,
                                   cVector3d &backPlaneNorm,
                                   double &boundaryLimit) {
  boundaryLimit = getDynamicBoundaryLimit();

  const cVector3d focusPoint(0.0, 0.0, 0.0);
  if (!camera) {
    northPlanePos = cVector3d(0, boundaryLimit, 0);
    northPlaneNorm = cVector3d(0, 1, 0);
    southPlanePos = cVector3d(0, -boundaryLimit, 0);
    southPlaneNorm = cVector3d(0, -1, 0);
    eastPlanePos = cVector3d(boundaryLimit, 0, 0);
    eastPlaneNorm = cVector3d(1, 0, 0);
    westPlanePos = cVector3d(-boundaryLimit, 0, 0);
    westPlaneNorm = cVector3d(-1, 0, 0);
    forwardPlanePos = cVector3d(0, 0, boundaryLimit);
    forwardPlaneNorm = cVector3d(0, 0, 1);
    backPlanePos = cVector3d(0, 0, -boundaryLimit);
    backPlaneNorm = cVector3d(0, 0, -1);
    return;
  }

  const cVector3d camRight = camera->getRightVector();
  const cVector3d camUp = camera->getUpVector();
  const cVector3d camLook = camera->getLookVector();

  northPlanePos = focusPoint + camUp * boundaryLimit;
  northPlaneNorm = camUp;
  southPlanePos = focusPoint - camUp * boundaryLimit;
  southPlaneNorm = -camUp;
  eastPlanePos = focusPoint + camRight * boundaryLimit;
  eastPlaneNorm = camRight;
  westPlanePos = focusPoint - camRight * boundaryLimit;
  westPlaneNorm = -camRight;
  forwardPlanePos = focusPoint + camLook * boundaryLimit;
  forwardPlaneNorm = camLook;
  backPlanePos = focusPoint - camLook * boundaryLimit;
  backPlaneNorm = -camLook;
}

void applyBoundaryConditions(cVector3d &oldPosition, cVector3d &newPosition) {
  cVector3d northPlanePos;
  cVector3d northPlaneNorm;
  cVector3d southPlanePos;
  cVector3d southPlaneNorm;
  cVector3d eastPlanePos;
  cVector3d eastPlaneNorm;
  cVector3d westPlanePos;
  cVector3d westPlaneNorm;
  cVector3d forwardPlanePos;
  cVector3d forwardPlaneNorm;
  cVector3d backPlanePos;
  cVector3d backPlaneNorm;
  double boundaryLimit = 0.0;

  getCameraAlignedBoundaryPlanes(
      northPlanePos, northPlaneNorm,
      southPlanePos, southPlaneNorm,
      eastPlanePos, eastPlaneNorm,
      westPlanePos, westPlaneNorm,
      forwardPlanePos, forwardPlaneNorm,
      backPlanePos, backPlaneNorm,
      boundaryLimit);

  applySeanBoundaryConditions(
      oldPosition, newPosition, newPosition,
      northPlanePos, northPlaneNorm,
      southPlanePos, southPlaneNorm,
      eastPlanePos, eastPlaneNorm,
      westPlanePos, westPlaneNorm,
      forwardPlanePos, forwardPlaneNorm,
      backPlanePos, backPlaneNorm,
      boundaryLimit);
}

cVector3d getNewAtomPosition(Atom *atom, cVector3d &prev_position, const double timeInterval) {
  cVector3d x_curr = atom->getLocalPos();
  cVector3d force = atom->getForce();

  cVector3d acc;
  if (energySurface == ASE) {
    // force is in eV/Å and getMass() must be amu (see note below). ASE integrates
    // in Å, giving an Å displacement of (F/m)*dt². We render in world units where
    // 1 world unit = 1/DIST_SCALE Å, so scale that Å acceleration by DIST_SCALE.
    acc = force / atom->getMass() * DIST_SCALE;
    if (atom == spheres[currentIndex]) {
      cout << acc.length() << endl;
    }
  } else {
    // reduced-unit toy potentials (LJ/Morse): keep the original tuned scaling,
    // which depends on the current (unphysical) numbers.
    acc = force / (atom->getMass() * SPHERE_MASS_SCALE_FACTOR);
  }
  if (!isFiniteVector(acc)) {
    acc.zero();
  }
  return x_curr + (x_curr - prev_position) + acc * timeInterval * timeInterval;
}

// cVector3d forceModeUpdate(Atom *current, cVector3d position, const double timeInterval) {
//   const double K_CURRENT       = K_HAPTIC_SPRING;
//   const double K_CURRENT_DAMP  = K_HAPTIC_DAMPER;
//   const double K_HAPTIC        = K_HAPTIC_SPRING;
//   const double K_HAPTIC_DAMP   = K_HAPTIC_DAMPER;

//   cVector3d currentPrevPos = prevPositions[currentIndex];
//   cVector3d x_curr = current->getLocalPos();

//   // Physical (UMA) force: integrate in world units with the ASE scaling.
//   cVector3d physForce = current->getForce();
//   cVector3d physAcc;
//   if (energySurface == ASE) {
//     physAcc = physForce / current->getMass() * DIST_SCALE;      // ×0.02
//   } else {
//     physAcc = physForce / (current->getMass() * SPHERE_MASS_SCALE_FACTOR);
//   }
//   if (!isFiniteVector(physAcc)) physAcc.zero();

//   // Haptic spring: defined directly in world/display units. Keep it on the
//   // ORIGINAL scaling so its feel is unchanged by the physics-unit fix.
//   cVector3d positionErr = position - x_curr;
//   cVector3d velocity    = (x_curr - currentPrevPos) / timeInterval;
//   cVector3d hapticAcc   = (positionErr * K_CURRENT - velocity * K_CURRENT_DAMP)
//                           / (current->getMass() * SPHERE_MASS_SCALE_FACTOR);
                          
//   if (!isFiniteVector(hapticAcc)) hapticAcc.zero();

//   // Verlet, summing the two accelerations only at integration time.
//   cVector3d newPos = x_curr + (x_curr - currentPrevPos)
//                    + (physAcc + hapticAcc) * timeInterval * timeInterval;
//   current->setLocalPos(newPos);
//   prevPositions[currentIndex] = newPos;

//   cVector3d forceErr        = current->getLocalPos() - position;
//   cVector3d hapticVelocity  = (current->getLocalPos() - currentPrevPos) / timeInterval;
//   return forceErr * K_HAPTIC - hapticVelocity * K_HAPTIC_DAMP;
// }

bool prevHapticInitialized;
cVector3d prevHapticPosition(0,0,0);
// cPrecisionClock positionClock;
// bool standby;
// bool resetting;
// bool confirming;
// bool simulating;
// double finalErr;

// const int MAX_FORCE_HISTORY = 10000;
// cVector3d prevForces[MAX_FORCE_HISTORY];
// int prevForcesIndex = 0;

// double getStrongestScalarProj(cVector3d v) {
//   const double length = v.length();
//   if (length <= 1e-9) {
//     return 0.0;
//   }

//   double strongest = 0;
//   for (int i = 0; i < MAX_FORCE_HISTORY; i++) {
//     double scalarProj = v.dot(prevForces[i]) / length;
//     if (strongest < scalarProj) {
//       strongest = scalarProj;
//     }
//   }
//   return strongest;
// }

// void recordForceHistory(Atom *current) {
//   constexpr double REST_ERR = 0.001;

//   if (!simulating) return;
//   if (current->getForce().length() < REST_ERR) return;

//   prevForces[prevForcesIndex] = current->getForce();
//   prevForcesIndex = (prevForcesIndex + 1) % MAX_FORCE_HISTORY;
// }

// std::optional<cVector3d> updateStandbyModeSimulating(Atom *current, cVector3d& position, double timeInterval) {
//   constexpr double HAPTIC_RADIUS = .02;
//   constexpr double K_HAPTIC = 1; // spring constant for applying vector projection

//   if (position.length() < HAPTIC_RADIUS) {
//     cVector3d temp = current->getLocalPos();
//     current->setLocalPos(getNewAtomPosition(current, prevPositions[currentIndex], timeInterval));
//     prevPositions[currentIndex] = temp;

//     double distFromCenter = position.length() - finalErr;
//     if (position.length() <= 1e-9) {
//       return cVector3d(0, 0, 0);
//     }

//     cVector3d direction = cNormalize(position);
//     return getStrongestScalarProj(-direction) * -direction * (distFromCenter / HAPTIC_RADIUS) * K_HAPTIC;
//   }
//   simulating = false;
//   return std::nullopt;
// }

// std::optional<cVector3d> updateStandbyState(Atom *current, const cVector3d& position,
//                                           const cVector3d& dPHaptic, double timeInterval) {
//   // position err acceptable for return mechanism to return to center,
//   // live-tunable via the IPC "set settling_err" command / launcher UI
//   double SETTLING_ERR = settlingError.load();
//   // spring constant for return haptic controller to center, live-tunable via
//   // the IPC "set k_return" command / launcher UI
//   double K_RETURN = kReturn.load();
//   constexpr double K_DAMPING = 8.0;
//   constexpr double RETURN_DELAY_SECONDS = 5.0;

//   if (position.length() >= SETTLING_ERR) {
//     if (resetting && confirming) {
//       cout << "Not yet settled!" << endl;
//     }
//     confirming = false;
//     // delay (seconds) after entering standby before the return mechanism
//     // kicks in, live-tunable via the IPC "set return_delay" command / launcher UI
//     if (positionClock.getCurrentTimeSeconds() >= returnDelaySeconds.load() || resetting) {
//       positionClock.stop();
//       positionClock.reset();
//       if (!resetting) {
//         cout << "Resetting to center..." << endl;
//       }
//       resetting = true;

//       const double MAX_FORCE = 1.6; // maximum force the return mechanism should output

//       double distanceFromCenter = position.length();
//       double springScale = 0.5 + (distanceFromCenter / (SETTLING_ERR * 2.0));
//       if (springScale > 1.0) {
//         springScale = 1.0;
//       }

//       // velocity-based damping on the return spring, live-tunable via the
//       // IPC "set k_dampen" command / launcher UI; defaults to 0 (no
//       // damping) so existing return behavior is unchanged until set
//       cVector3d hapticVelocity = dPHaptic / timeInterval;
//       cVector3d returnVector = -position * K_RETURN - hapticVelocity * kDampen.load();
//       return clampVectorMagnitude(returnVector, MAX_FORCE);
//     }
//   } else {
//     if (!confirming) {
//       cout << "Starting confirmation..." << endl;
//       positionClock.start();
//       confirming = true;
//     }
    
//     if (positionClock.getCurrentTimeSeconds() >= .5) {
//       cout << "Done!" << endl;
//       standby = false;
//       resetting = false;
//       confirming = false;
//       prevHapticInitialized = false;
//       simulating = true;
//       positionClock.stop();
//       positionClock.reset();

//       prevPositions[currentIndex] = current->getLocalPos();
//       finalErr = position.length();
//     }
//     return cVector3d(0,0,0);
//   }
//   return std::nullopt;
// }

// cVector3d standbyModeUpdate(Atom *current, cVector3d position, const double timeInterval) {
//   constexpr double STANDBY_ERR = .1; // movement err acceptable for standby mode to activate

//   if (!prevHapticInitialized) {
//     prevHapticInitialized = true;
//     prevHapticPosition = position;
//   }
//   recordForceHistory(current);

//   cVector3d dPHaptic = position - prevHapticPosition;
//   prevHapticPosition = position;

//   if (simulating) {
//     auto pos = updateStandbyModeSimulating(current, position, timeInterval);
//     if (pos) return *pos;
//   } else {
//     if (dPHaptic.length() < STANDBY_ERR && !standby && position.length() >= .01) {
//       positionClock.start();
//       standby = true;
//       cout << "Entering standby mode..." << endl;
//     }
//     if (standby) {
//       auto pos = updateStandbyState(current, position, dPHaptic, timeInterval);
//       if (pos) return *pos;
//     }

//     if (dPHaptic.length() >= 1e-6 && standby && !resetting) { 
//       standby = false;
//       positionClock.stop();
//       positionClock.reset();
//       cout << "Standby cancelled!" << endl;
//     }
//   }

//   current->setLocalPos(current->getLocalPos() + dPHaptic);
//   return current->getForce();
// }

cVector3d positionModeUpdate(Atom *current, cVector3d position, const double timeInterval) {
  const double VELOCITY_MULT = 25;
  const double ATTRACTION_MAX = 1.5;
  cVector3d currentPos = current->getLocalPos();
  cVector3d attraction = (position - currentPos) * timeInterval * VELOCITY_MULT;
  current->setLocalPos(currentPos + clampVectorMagnitude(attraction, ATTRACTION_MAX * timeInterval));
  prevPositions[currentIndex] = currentPos;
  return cVector3d(0,0,0);
}

vector<int> getSelectedAtomIndices() {
  vector<int> selected;
  for (int i = 0; i < spheres.size(); i++) {
    if (spheres[i]->isSelected()) {
      selected.push_back(i);
    }
  }
  return selected;
}

vector<int> getHapticInfluencedAtomIndices() {
  vector<int> influenced = getSelectedAtomIndices();
  if (influenced.empty() && currentIndex >= 0 && currentIndex < spheres.size()) {
    influenced.push_back(currentIndex);
  }
  return influenced;
}

map<int, cVector3d> hapticInfluenceOffsets;
vector<int> activeHapticInfluence;

void ensureHapticInfluenceOffsets(const vector<int> &indices,
                                  const cVector3d &position) {
  if (indices != activeHapticInfluence) {
    activeHapticInfluence = indices;
    hapticInfluenceOffsets.clear();
    for (int index : indices) {
      if (spheres[index]->isSelected()) {
        hapticInfluenceOffsets[index] = spheres[index]->getLocalPos() - position;
      } else {
        hapticInfluenceOffsets[index] = cVector3d(0, 0, 0);
      }
    }
  }

  
}

cVector3d getAverageAtomGroupForce(const vector<int> &indices) {
  cVector3d force(0, 0, 0);
  if (indices.empty()) {
    return force;
  }
  for (int index : indices) {
    force += spheres[index]->getForce();
  }
  return force / static_cast<double>(indices.size());
}

cVector3d addHapticForceToAtoms(const vector<int> &indices,
                                const cVector3d &position,
                                const double timeInterval) {
  if (indices.empty()) {
    return cVector3d(0, 0, 0);
  }

  ensureHapticInfluenceOffsets(indices, position);
  cVector3d averageForceBeforeHaptic = getAverageAtomGroupForce(indices);
  for (int index : indices) {
    Atom *atom = spheres[index];
    if (!atom->isAnchor()) {
      cVector3d targetPosition = position + hapticInfluenceOffsets[index];
      cVector3d currentPosition = atom->getLocalPos();
      cVector3d previousPosition = prevPositions[index];
      cVector3d velocity = (currentPosition - previousPosition) / timeInterval;
      cVector3d externalForce =
          (targetPosition - currentPosition) * K_HAPTIC_SPRING -
          velocity * K_HAPTIC_DAMPER;
      atom->setForce(atom->getForce() +
                     clampVectorMagnitude(externalForce, MAX_HAPTIC_ATOM_FORCE));
    }
  }

  return averageForceBeforeHaptic;
}

vector<int> activeHapticSelection;
map<int, cVector3d> activeHapticSelectionOffsets;

void ensureSelectedAtomOffsets(const vector<int> &selectedIndices,
                               const cVector3d &position) {
  if (selectedIndices == activeHapticSelection) {
    return;
  }

  activeHapticSelection = selectedIndices;
  activeHapticSelectionOffsets.clear();
  for (int index : selectedIndices) {
    activeHapticSelectionOffsets[index] = spheres[index]->getLocalPos() - position;
  }
  prevHapticInitialized = false;
}

cVector3d getSelectedAtomTarget(int index, const cVector3d &position) {
  return position + activeHapticSelectionOffsets[index];
}

cVector3d forceModeUpdateSelectedGroup(const vector<int> &selectedIndices,
                                      cVector3d position,
                                      const double timeInterval) {
  const double K_CURRENT = K_HAPTIC_SPRING;
  const double K_CURRENT_DAMP = K_HAPTIC_DAMPER;
  ensureSelectedAtomOffsets(selectedIndices, position);

  cVector3d averageSimulationForce = getAverageAtomGroupForce(selectedIndices);

  for (int index : selectedIndices) {
    Atom *atom = spheres[index];
    cVector3d currentPosition = atom->getLocalPos();
    cVector3d previousPosition = prevPositions[index];
    cVector3d targetPosition = getSelectedAtomTarget(index, position);
    cVector3d velocity = (currentPosition - previousPosition) / timeInterval;
    cVector3d hapticForce = (targetPosition - currentPosition) * K_CURRENT -
                            velocity * K_CURRENT_DAMP;

    atom->setForce(atom->getForce() + hapticForce);
    cVector3d newPosition = getNewAtomPosition(atom, previousPosition, timeInterval);
    applyBoundaryConditions(currentPosition, newPosition);
    atom->setLocalPos(newPosition);
    prevPositions[index] = currentPosition;
  }

  return averageSimulationForce;
}

cVector3d positionModeUpdateSelectedGroup(const vector<int> &selectedIndices,
                                         cVector3d position,
                                         const double timeInterval) {
  const double VELOCITY_MULT = 25;
  const double ATTRACTION_MAX = 1.5;
  ensureSelectedAtomOffsets(selectedIndices, position);

  for (int index : selectedIndices) {
    Atom *atom = spheres[index];
    cVector3d oldPosition = atom->getLocalPos();
    cVector3d targetPosition = getSelectedAtomTarget(index, position);
    cVector3d attraction = (targetPosition - oldPosition) * timeInterval * VELOCITY_MULT;
    cVector3d newPosition = oldPosition +
        clampVectorMagnitude(attraction, ATTRACTION_MAX * timeInterval);
    applyBoundaryConditions(oldPosition, newPosition);
    atom->setLocalPos(newPosition);
    prevPositions[index] = oldPosition;
  }
  return getAverageAtomGroupForce(selectedIndices);
}

cVector3d standbyModeUpdateSelectedGroup(const vector<int> &selectedIndices,
                                        cVector3d position,
                                        const double timeInterval) {
  ensureSelectedAtomOffsets(selectedIndices, position);
  if (!prevHapticInitialized) {
    prevHapticInitialized = true;
    prevHapticPosition = position;
  }

  cVector3d dPHaptic = position - prevHapticPosition;
  prevHapticPosition = position;
  for (int index : selectedIndices) {
    Atom *atom = spheres[index];
    cVector3d oldPosition = atom->getLocalPos();
    cVector3d newPosition = oldPosition + dPHaptic;
    applyBoundaryConditions(oldPosition, newPosition);
    atom->setLocalPos(newPosition);
    prevPositions[index] = oldPosition;
  }
  return getAverageAtomGroupForce(selectedIndices);
}

void moveCurrentAtom(double rightAmount, double upAmount, double forwardAmount) {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  if (spheres.empty()) {
    return;
  }
  Atom *current = spheres[currentIndex];
  cVector3d delta = ATOM_MOVE_STEP * (rightAmount * camera->getRightVector() +
                                      upAmount * camera->getUpVector() +
                                      forwardAmount * camera->getLookVector());
  current->setLocalPos(current->getLocalPos() + delta);
  if (currentIndex < prevPositions.size()) {
    prevPositions[currentIndex] = current->getLocalPos();
  }
}


cVector3d stepSimulation(const cVector3d &requestedPosition, const double timeInterval,
                        const bool hasHapticDevice) {
  if (prevPositions.size() != spheres.size()) {
    prevPositions.resize(spheres.size());
  }
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  if (spheres.empty()) {
    return cVector3d(0.0, 0.0, 0.0);
  }

  Atom *current = spheres[currentIndex];
  cVector3d position = hasHapticDevice ? requestedPosition : current->getLocalPos();
  vector<int> hapticInfluencedIndices =
      hasHapticDevice ? getHapticInfluencedAtomIndices() : vector<int>();
  bool useHapticInfluence = !hapticInfluencedIndices.empty();
  if (!useHapticInfluence && !activeHapticSelection.empty()) {
    activeHapticSelection.clear();
    activeHapticSelectionOffsets.clear();
    prevHapticInitialized = false;
  }

  cVector3d currentPosition(0,0,0);
  cVector3d hapticForce(0, 0, 0);
  
  if (!freezeAtoms.load()) {
    if (!calculatorPtr) {
      cerr << "Error: calculatorPtr is null in stepSimulation()" << endl;
      return cVector3d(0.0, 0.0, 0.0);
    }
    const double currentTemp = getCurrentTemp();
    // calculatorPtr->setTemperature(currentTemp);
    vector<vector<double>> forcesVec = calculatorPtr->getFandU(spheres);
    double potentialEnergy = forcesVec[spheres.size()][0];
    if (std::isfinite(potentialEnergy)) {
      potentialEnergyDerivative = (potentialEnergy - lastPotentialEnergy) / std::max(1e-6, timeInterval);
      lastPotentialEnergy = potentialEnergy;
    }

    for (int i = 0; i < spheres.size(); i++) {
      Atom *atom = spheres[i];
      cVector3d force(forcesVec[i][0], forcesVec[i][1], forcesVec[i][2]);
      if (!isFiniteVector(force)) {
        force.zero();
      }
      atom->setForce(force);
    }
    if (hasHapticDevice && useHapticInfluence) {
      hapticForce = addHapticForceToAtoms(hapticInfluencedIndices, position, timeInterval);
    }
    for (int i = 0; i < spheres.size(); i++) {
      Atom *atom = spheres[i];
      if (!atom->isAnchor()) {
        cVector3d old_position = atom->getLocalPos();
        cVector3d new_position = getNewAtomPosition(atom, prevPositions[i], timeInterval);
        prevPositions[i] = old_position;
        applyBoundaryConditions(old_position, new_position);
        atom->setLocalPos(new_position);
        cVector3d v = (new_position - old_position) / timeInterval;
        atom->setVelocity(v);
      }
    }
    displayedPotentialEnergy.store(potentialEnergy);
  }

  for (int i = 0; i < spheres.size(); i++) {
    spheres[i]->updateVelVector();
  }

  return hapticForce;
}

void updateHaptics(void) {
  hapticsThreadStarted.store(true);

  // simulation in now running
  simulationRunning = true;
  simulationFinished = false;
  if (!hapticDevice) {
    return;
  }
  // open a connection to haptic device
  hapticDevice->open();

  // calibrate device (if necessary)
  hapticDevice->calibrate();
  // Track which atom is currently being moved
  int anchor_atom = 1;
  int anchor_atom_hold = 1;

  // main haptic simulation loop
  bool button3_changed = false;
  bool is_anchor = true;
  bool buttons[4];
  bool buttonReset[4];
  readButtons(buttons, buttonReset);
  initializeprevPositions();
  while (simulationRunning) {
    /////////////////////////////////////////////////////////////////////
    // SIMULATION TIME
    /////////////////////////////////////////////////////////////////////

    // signal frequency counter
    freqCounterHaptics.signal(1);
    /////////////////////////////////////////////////////////////////////////
    // READ HAPTIC DEVICE
    /////////////////////////////////////////////////////////////////////////
    // read position
    cVector3d position;
    hapticDevice->getPosition(position);

    // Scale position to use more of the screen
    // increase to use more of the screen
    position *= 2.0;
    hapticPosition = position;

    /////////////////////////////////////////////////////////////////////////
    // UPDATE SIMULATION
    /////////////////////////////////////////////////////////////////////////
    // Update current atom based on if the user pressed the far left button
    // The point of button2_changed is to make it so that it only switches one
    // atom if the button is touched Otherwise it flips out
    
    readButtons(buttons, buttonReset);

    // time step the simulation runs at in seconds - shorter timesteps are more
    // accurate but advance the sim more slowly. Driven by the Time Step slider
    // (and HAPTIC_DEVICE_TIME_STEP / the IPC "set timestep" command) so the
    // cout << simulationTimeStep.load() << endl;
    // slider takes effect in haptic mode too, instead of a hardcoded value.
    cVector3d force = stepSimulation(position, simulationTimeStep.load() / ASE_UNITS_TO_FS, true);

    /////////////////////////////////////////////////////////////////////////
    // APPLY FORCES
    /////////////////////////////////////////////////////////////////////////

    // scale by the user-configurable feedback intensity, then apply a hard
    // safety ceiling regardless of that scale - so a spike (e.g. two atoms
    // overlapping) can never slam the device at full force even if
    // intensity is set to 100%
    force = clampVectorMagnitude(force * hapticForceScale.load(), MAX_HAPTIC_OUTPUT_FORCE);
    hapticDevice->setForce(force);
  }
  // close  connection to haptic device
  hapticDevice->close();

  // exit haptics thread
  simulationFinished = true;

  // Close the calculator
  delete calculatorPtr;
  calculatorPtr = nullptr;
}

//------------------------------------------------------------------------------
// SLIDER CONTROL WINDOW
//------------------------------------------------------------------------------
struct SliderConfig {
  string name;
  double minValue;
  double maxValue;
  double defaultValue;
  string units;
  double displayScale;
  int displayDigits;
};

struct SliderUI {
  string id;
  string name;
  string units;
  double minValue;
  double maxValue;
  double value;
  double displayScale;
  int displayDigits;
  bool dragging;

  double normalizedValue() const {
    if (maxValue <= minValue) {
      return 0.0;
    }
    return (value - minValue) / (maxValue - minValue);
  }

  void setNormalizedValue(double normalizedValue) {
    double clampedValue = normalizedValue;
    if (clampedValue < 0.0) {
      clampedValue = 0.0;
    }
    if (clampedValue > 1.0) {
      clampedValue = 1.0;
    }
    value = minValue + clampedValue * (maxValue - minValue);
  }

  string displayText() const {
    return name + ": " + cStr(value * displayScale, displayDigits) + " " + units;
  }
};

const int SLIDER_WINDOW_WIDTH = 340;
const int SLIDER_WINDOW_HEIGHT = 170;
const int SLIDER_WIDTH = 240;
const int SLIDER_LEFT = 50;
const int SLIDER_TOP = 45;
const int SLIDER_ROW_SPACING = 52;

int sliderWindowWidth = SLIDER_WINDOW_WIDTH;
int sliderWindowHeight = SLIDER_WINDOW_HEIGHT;
cFontPtr sliderFont;
vector<SliderUI> sliders;
unordered_map<string, int> sliderIndexById;

// SLIDER UI STEP 1A: Add each new slider ID to this list to control display order.
vector<string> sliderOrder = {"time_step", "temperature"};

// SLIDER UI STEP 1B: Add each new slider's configuration here.
// sliderConfigs[id] = {display name, min, max, default, units, display scale, display digits}
unordered_map<string, SliderConfig> sliderConfigs = {
  {"time_step", {"Time Step", 0.00001, 0.0020, 0.0010, "ms", 1000.0, 3}},
  {"temperature", {"Temperature", 0.000, 15.000, 0.000, "kT", 1.0, 5}}
};

// SLIDER UI STEP 4: Wire the slider back to whatever live state it controls,
// both so dragging it takes effect immediately and so the handle reflects
// changes made through another channel (e.g. the launcher's IPC command).
void applySliderValue(const string &id, double value) {
  if (id == "time_step") {
    setLiveTimeStep(value);
  }
}

double getLiveSliderValue(const string &id, double fallback) {
  if (id == "time_step") {
    return simulationTimeStep.load();
  }
  return fallback;
}

void generateSliderUI() {
  sliders.clear();
  sliderIndexById.clear();

  for (const string &id : sliderOrder) {
    const SliderConfig &config = sliderConfigs[id];

    SliderUI slider;
    slider.id = id;
    slider.name = config.name;
    slider.units = config.units;
    slider.minValue = config.minValue;
    slider.maxValue = config.maxValue;
    slider.value = getLiveSliderValue(id, config.defaultValue);
    slider.displayScale = config.displayScale;
    slider.displayDigits = config.displayDigits;
    slider.dragging = false;

    sliderIndexById[id] = sliders.size();
    sliders.push_back(slider);
  }
}

// keep sliders that aren't currently being dragged in sync with live state
// changed through another channel (e.g. the launcher's IPC "set timestep")
void syncSlidersFromLiveState() {
  for (SliderUI &slider : sliders) {
    if (slider.dragging) {
      continue;
    }
    slider.value = getLiveSliderValue(slider.id, slider.value);
  }
}

double getSliderValue(const string &id, double fallback) {
  auto it = sliderIndexById.find(id);
  if (it == sliderIndexById.end()) {
    return fallback;
  }
  return sliders[it->second].value;
}

// SLIDER UI STEP 2: Add a getter for each slider value
// The fallback should match that sliders default value in sliderConfigs.
double getSimulationTimeStep() {
  return getSliderValue("time_step", 0.0010);
}

double getCurrentTemp() {
  return getSliderValue("temperature", 1.00);
}

// SLIDER UI STEP 3: Replace direct variable usage with the getter where needed.
// Example: use getSimulationTimeStep() instead of hardcoding the timestep.
void initializeSliderUI() {
  sliderFont = NEW_CFONT_CALIBRI_20();
  generateSliderUI();
  updateSliderWindowTitle();
}

void sliderWindowSizeCallback(GLFWwindow *a_window, int a_width, int a_height) {
  sliderWindowWidth = a_width;
  sliderWindowHeight = a_height;
}

void getSliderLayout(int sliderIndex, double &trackX, double &trackY) {
  trackX = SLIDER_LEFT;
  trackY = SLIDER_TOP + sliderIndex * SLIDER_ROW_SPACING;
}

void getSliderMousePosition(GLFWwindow *a_window, double inputX, double inputY,
                            double &mouseX, double &mouseY) {
  int windowWidth;
  int windowHeight;
  glfwGetWindowSize(a_window, &windowWidth, &windowHeight);

  if (windowWidth <= 0 || windowHeight <= 0) {
    mouseX = inputX;
    mouseY = inputY;
    return;
  }

  mouseX = inputX * sliderWindowWidth / windowWidth;
  mouseY = inputY * sliderWindowHeight / windowHeight;
}

double getSliderNormalizedValueFromMouseX(int sliderIndex, double mouseX) {
  double trackX;
  double trackY;
  getSliderLayout(sliderIndex, trackX, trackY);

  double normalizedValue = (mouseX - trackX) / SLIDER_WIDTH;
  if (normalizedValue < 0.0) {
    return 0.0;
  }
  if (normalizedValue > 1.0) {
    return 1.0;
  }
  return normalizedValue;
}

bool isMouseOverSlider(int sliderIndex, double mouseX, double mouseY) {
  double trackX;
  double trackY;
  getSliderLayout(sliderIndex, trackX, trackY);

  return (mouseX >= trackX - 12 && mouseX <= trackX + SLIDER_WIDTH + 12 &&
          mouseY >= trackY - 18 && mouseY <= trackY + 18);
}

void updateSliderWindowTitle() {
  if (sliderWindow == NULL) {
    return;
  }
  glfwSetWindowTitle(sliderWindow, "Controls");
}

void drawRect(double x, double y, double w, double h, float r, float g, float b) {
  glColor3f(r, g, b);
  glBegin(GL_QUADS);
  glVertex2d(x, y);
  glVertex2d(x + w, y);
  glVertex2d(x + w, y + h);
  glVertex2d(x, y + h);
  glEnd();
}

void drawCircle(double cx, double cy, double radius, float r, float g, float b) {
  const int segments = 24;
  glColor3f(r, g, b);
  glBegin(GL_TRIANGLE_FAN);
  glVertex2d(cx, cy);
  for (int i = 0; i <= segments; i++) {
    const double angle = 2.0 * M_PI * i / segments;
    glVertex2d(cx + radius * cos(angle), cy + radius * sin(angle));
  }
  glEnd();
}

// draws a horizontal "pill" (stadium shape): a rectangle capped with a
// semicircle at each end, used for the simplified/rounded slider track
void drawPill(double xStart, double xEnd, double y, double halfHeight, float r, float g, float b) {
  if (xEnd < xStart) {
    xEnd = xStart;
  }
  drawRect(xStart, y - halfHeight, xEnd - xStart, halfHeight * 2.0, r, g, b);
  drawCircle(xStart, y, halfHeight, r, g, b);
  drawCircle(xEnd, y, halfHeight, r, g, b);
}

void drawSliderText(const string &text, double x, double y) {
  if (!sliderFont) {
    return;
  }

  cRenderOptions options;
  options.m_camera = nullptr;
  options.m_single_pass_only = true;
  options.m_render_opaque_objects_only = true;
  options.m_render_transparent_front_faces_only = false;
  options.m_render_transparent_back_faces_only = false;
  options.m_enable_lighting = false;
  options.m_render_materials = false;
  options.m_render_textures = true;
  options.m_creating_shadow_map = false;
  options.m_rendering_shadow = false;
  options.m_shadow_light_level = 0.0;
  options.m_storeObjectPositions = false;
  options.m_markForUpdate = false;

  glDisable(GL_LIGHTING);
  glPushMatrix();
  glTranslated(x, y, 0.0);
  glScaled(1.0, -1.0, 1.0);
  sliderFont->renderText(text, cColorf(0.05f, 0.05f, 0.05f), 1.0, 1.0, 1.0, options);
  glPopMatrix();
}

void renderSliderWindow() {
  if (sliderWindow == NULL) {
    return;
  }
  if (glfwWindowShouldClose(sliderWindow)) {
    glfwDestroyWindow(sliderWindow);
    sliderWindow = NULL;
    glfwMakeContextCurrent(window);
    return;
  }

  syncSlidersFromLiveState();

  glfwMakeContextCurrent(sliderWindow);
  int framebufferWidth;
  int framebufferHeight;
  glfwGetWindowSize(sliderWindow, &sliderWindowWidth, &sliderWindowHeight);
  glfwGetFramebufferSize(sliderWindow, &framebufferWidth, &framebufferHeight);
  glViewport(0, 0, framebufferWidth, framebufferHeight);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, sliderWindowWidth, sliderWindowHeight, 0, -1, 1);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glDisable(GL_DEPTH_TEST);
  glClearColor(0.94f, 0.94f, 0.94f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  const double TRACK_HALF_HEIGHT = 3.0;
  const double HANDLE_RADIUS = 9.0;

  for (int i = 0; i < sliders.size(); i++) {
    const SliderUI &slider = sliders[i];
    double trackX;
    double trackY;
    getSliderLayout(i, trackX, trackY);
    const double trackXEnd = trackX + SLIDER_WIDTH;
    const double handleX = trackX + slider.normalizedValue() * SLIDER_WIDTH;

    drawSliderText(slider.displayText(), trackX, trackY - 28);
    // simple rounded pill track with a filled portion, plus a plain circular
    // handle knob - avoids the blocky rectangle look of the old slider
    drawPill(trackX, trackXEnd, trackY, TRACK_HALF_HEIGHT, 0.82f, 0.82f, 0.84f);
    drawPill(trackX, handleX, trackY, TRACK_HALF_HEIGHT, 0.20f, 0.55f, 0.95f);
    drawCircle(handleX, trackY, HANDLE_RADIUS, 1.0f, 1.0f, 1.0f);
    drawCircle(handleX, trackY, HANDLE_RADIUS - 2.5, 0.15f, 0.45f, 0.85f);
  }

  updateSliderWindowTitle();
  glfwSwapBuffers(sliderWindow);
  glfwMakeContextCurrent(window);
}

bool handleSliderMousePress(double mouseX, double mouseY) {
  for (int i = 0; i < sliders.size(); i++) {
    if (!isMouseOverSlider(i, mouseX, mouseY)) {
      continue;
    }

    sliders[i].dragging = true;
    sliders[i].setNormalizedValue(getSliderNormalizedValueFromMouseX(i, mouseX));
    applySliderValue(sliders[i].id, sliders[i].value);
    updateSliderWindowTitle();
    return true;
  }
  return false;
}

bool handleSliderMouseMotion(double mouseX, double mouseY) {
  for (int i = 0; i < sliders.size(); i++) {
    if (!sliders[i].dragging) {
      continue;
    }

    sliders[i].setNormalizedValue(getSliderNormalizedValueFromMouseX(i, mouseX));
    applySliderValue(sliders[i].id, sliders[i].value);
    updateSliderWindowTitle();
    return true;
  }
  return false;
}

bool handleSliderMouseRelease() {
  for (int i = 0; i < sliders.size(); i++) {
    if (!sliders[i].dragging) {
      continue;
    }

    sliders[i].dragging = false;
    return true;
  }
  return false;
}

void sliderWindowCursorPosCallback(GLFWwindow *a_window, double a_posX, double a_posY) {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  double mouseX;
  double mouseY;
  getSliderMousePosition(a_window, a_posX, a_posY, mouseX, mouseY);
  handleSliderMouseMotion(mouseX, mouseY);
}

void sliderWindowMouseButtonCallback(GLFWwindow *a_window, int a_button, int a_action, int a_mods) {
  if (a_button != GLFW_MOUSE_BUTTON_LEFT) {
    return;
  }

  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  double x;
  double y;
  glfwGetCursorPos(a_window, &x, &y);
  getSliderMousePosition(a_window, x, y, x, y);

  if (a_action == GLFW_PRESS) {
    handleSliderMousePress(x, y);
  } else if (a_action == GLFW_RELEASE) {
    handleSliderMouseRelease();
  }
}

// the Controls window has its own GLFW key callback, so ESC/Q only reached
// the main window's keyCallback when the 3D view had focus; quitting should
// work no matter which of the two windows the user last clicked into
void sliderWindowKeyCallback(GLFWwindow *a_window, int a_key, int a_scancode, int a_action, int a_mods) {
  if ((a_action != GLFW_PRESS) && (a_action != GLFW_REPEAT)) {
    return;
  }
  if ((a_key == GLFW_KEY_ESCAPE) || (a_key == GLFW_KEY_Q)) {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
  }
}
