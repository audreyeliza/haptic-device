#include "atom.h"
#include "chai3d.h"
#include "globals.h"
#include "inputHandling.h"
#include "ipcServer.h"
#include "potentials.h"
#include "utility.h"
#include "customHapticDevice.h"
#include "slider.h"


#include <GLFW/glfw3.h>
#include <math.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
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

const int REPEAT_Y = 3;
const int REPEAT_Z = 1;

bool showDebug = false; // Toggles the extra debug overlay information when true.
chai3d::cVector3d hapticForce; // The force applied to the haptic device
std::vector<cLabel *> debugAtomLabels; // Stores the labels that annotate atoms with their indices.

// Declare variables needed for calculator constructor (cell, pbc), atoms object
// (mass, atomic number), and placing of initial atoms (positions)
std::array<double, 9> aseCell = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
std::array<int, 3> asePbc = {0, 0, 0};

// Stores the initial atom positions so the structure can be reset.
std::vector<cVector3d> initialPositions;

// Radius of an atom in world units. 
// Based on the covalent radius of hydrogen, .37 Å
const double SPHERE_RADIUS = 0.37 * DIST_SCALE;

// Haptic spring-damper constants used to reduce unwanted oscillations.
const double K_HAPTIC_SPRING = 100.0;
const double K_HAPTIC_DAMPER = 5.0;    // Damping for force modendLines

//------------------------------------------------------------------------------
// DECLARED VARIABLES
//------------------------------------------------------------------------------

Calculator *calculatorPtr; // The calculator used to calculate atom forces

chai3d::cCamera *camera; // a camera to render the world in the window display
chai3d::cGenericHapticDevicePtr hapticDevice; // a pointer to the current haptic device

// The haptic mode the simulation is in.

std::atomic<HapticMode> hapticMode(HapticMode::Position);


// simulation time step in seconds; overridable at launch via
// HAPTIC_DEVICE_TIME_STEP and changeable live via the IPC "set timestep" command
std::atomic<double> simulationTimeStep(0.001);

// standby/return-to-center haptic tuning parameters used by standbyModeUpdate,
// changeable live via the IPC "set settling_err/k_return/k_dampen/return_delay"
// commands (see setLiveKReturn/setLiveKDampen/setLiveReturnDelay)
std::atomic<double> kReturn(25.0);
std::atomic<double> kDampen(0.0); // 0 = no damping, matching original return behavior
std::atomic<double> returnDelaySeconds(2.5);

// overall scale applied to the force sent to the physical haptic device,
// overridable at launch via HAPTIC_DEVICE_FORCE_SCALE and changeable live via
// the IPC "set force_scale" command / launcher UI. Lets owners of older/more
// worn devices turn down feedback strength to reduce wear, without touchingctedPoint
// the underlying simulation's spring/damping constants.
std::atomic<double> hapticForceScale(1.0);
std::atomic<double> maxForceOutput(10.0);

std::atomic<int> repeatX(1);
std::atomic<int> repeatY(1);
std::atomic<int> repeatZ(1);

bool repeatChanged = false;

// atom objects
std::vector<Atom *> atoms;

// lines drawn between bonded atom pairs, keyed by sorted (atom index) pairs.
// Lines are created lazily and hidden (not removed) when a pair un-bonds so
// they can be cheaply re-shown if the pair drifts back within range.
std::map<pair<int, int>, chai3d::cShapeLine *> bondLines;

std::vector<cLabel *> debugLabels; // Stores the labels that show debug values in the scene.

chai3d::cLabel *hapticPositionLabel;
chai3d::cLabel *labelRates; // a label to display the rate [Hz] at which the simulation is running
chai3d::cLabel *LJ_num; // a label to show the potential energy
chai3d::cLabel *num_anchored; // label showing the # anchored
chai3d::cLabel *isFrozen; // a label to show whether or not the atoms are frozen
chai3d::cLabel *camera_pos; // a label to display the camera position
chai3d::cLabel *potentialLabel; // a label to identify the potential energy surface
chai3d::cLabel *temperatureLabel;

// labels for the scope
chai3d::cLabel *scope_upper;
chai3d::cLabel *scope_lower;

// a flag that indicates if the haptic simulation is currently running
std::atomic<bool> simulationRunning{false};

bool simulationFinished; // a flag that indicates if the haptic simulation has terminated

// a frequency counter to measure the simulation graphic rate
chai3d::cFrequencyCounter freqCounterGraphics; 

chai3d::cFrequencyCounter freqCounterHaptics; // a frequency counter to measure the simulation haptic rate


GLFWwindow *sliderWindow; // a handle to slider control window

// current framebuffer (render) size in pixels.
// NOTE: on HiDPI / Retina displays this is LARGER than the window size in points.
int width;
int height;

double CAMERA_RADIUS = .35; 

chai3d::cScope *scope; // a scope to monitor the potential energy

double global_minimum; // global minimum for the given cluster size

std::atomic<bool> freezeAtoms(false); // determine if atoms should be frozen

std::atomic<bool> renderAtoms(true); // determine if atom atoms should be drawn
std::atomic<bool> renderForceVectors(true); // determine if force vector lines should be drawn
std::atomic<bool> renderBonds(true); // determine if bond lines should be drawn
double centerCoords[3] = {50.0, 50.0, 50.0}; // save coordinates of central atom

std::atomic<int> screenshotCounter(-2); // keep track of how long screenshot label has been displayed
std::atomic<int> writeConCounter(-2);  // keep track of how long write to con label has been displayed

LocalPotential energySurface = LENNARD_JONES; // default potential is Lennard Jones
bool global_min_known = true; // check if able to read in the global min
chai3d::cPanel *helpPanel; // panel that displays hotkeys
chai3d::cLabel *helpHeader; // help panel header

std::atomic<double> displayedPotentialEnergy(0.0);

std::atomic<double> displayedTemperature(0.0);
double lastPotentialEnergy = 0.0;
std::atomic<int> displayedAnchoredCount(0);
std::recursive_mutex sceneMutex;
std::atomic<bool> hapticsThreadStarted(false);
std::atomic<bool> physicsThreadStarted(false);
int currentIndex = 0;
std::vector<cLabel *> hotkeyKeys; // vector holding hotkey key labels
std::vector<cLabel *> hotkeyFunctions; // vector holding function key labels (must be separate for formatting)

// screenshot notification label
chai3d::cLabel *screenshotLabel;

// write to con notification label
chai3d::cLabel *writeConLabel;

chai3d::cVector3d hapticPosition;

chai3d::cVector3d extraForces; // Miscellaneous forces; reset when applied

/**
 * @brief Prints the startup banner.
 */
void printIntro() {
  cout << endl;
  cout << "-----------------------------------" << endl;
  cout << "CHAI3D" << endl;
  cout << "Press CTRL for help" << endl;
  cout << "-----------------------------------" << endl
       << endl
       << endl;
}

/**
 * @brief Callback triggered when GLFW reports an error.
 * @param errorCode the error code of the error
 * @param description A UTF-8 encoded string describing the error
 */
void errorCallback(int a_error, const char *a_description) {
  cout << "Error: " << a_description << endl;
}

/**
 * @brief Configures the OpenGL context version used by the scene.
 * @param majorVer the major version of GLFW to use
 * @param minorVer the minor version of GLFW to use
 */
void setOpenGLVersion(int majorVer, int minorVer) {
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, majorVer);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minorVer);
}

/**
 * @brief Configures the GLFW library. Sets GLFW error callback, the OpenGL version to 2.1, and
 * the stereo mode.
 * @param STEREO_MODE Whether stereo mode should be active.
 */
void configureGLFW(const chai3d::cStereoMode STEREO_MODE) {
  if (!glfwInit()) {
    throw std::runtime_error("Configuration failed! GLFW not initialized!");
  }
  glfwSetErrorCallback(errorCallback); // set error callback
  setOpenGLVersion(2, 1);
  // set active stereo mode
  STEREO_MODE == C_STEREO_ACTIVE 
      ? glfwWindowHint(GLFW_STEREO, GL_TRUE) 
      : glfwWindowHint(GLFW_STEREO, GL_FALSE);
}

/**
 * @brief Initializes the main window using GLFW, setting all necessary callbacks. Also populates info about the window, such as the
 *        framebuffer size.
 * @return the main window of the simulation
 */
GLFWwindow* initializeMainWindow() {
  // compute desired size of window
  const GLFWvidmode *VIDEO_MODE = glfwGetVideoMode(glfwGetPrimaryMonitor());
  
  // How wide the main window should be relative to the monitor size.
  const double MAIN_WINDOW_WIDTH_SCALE = 0.8;
  // How tall the main window should be relative to the monitor size.
  const double MAIN_WINDOW_HEIGHT_SCALE = 0.5; 

  int windowWidth = MAIN_WINDOW_WIDTH_SCALE * VIDEO_MODE->width;
  int windowHeight = MAIN_WINDOW_HEIGHT_SCALE * VIDEO_MODE->height;

  // Handle to window display context
  GLFWwindow *window = glfwCreateWindow(windowWidth, windowHeight, "haptic-device", nullptr, 
      nullptr); 

  if (!window) {
    glfwTerminate();
    throw std::runtime_error("Failed to create window!");
  }

  glfwGetFramebufferSize(window, &width, &height); // framebuffer size in pixels (HiDPI-aware)     

  // Horizontally, where the main window should be relative to the monitor size.
  const double MAIN_WINDOW_INIT_POS_X = 0.5;
  // Vertically, where the main window should be relative to the monitor size.
  const double MAIN_WINDOW_INIT_POS_Y = 0.5;

  // set position of window
  glfwSetWindowPos(
    window,
    MAIN_WINDOW_INIT_POS_X * (VIDEO_MODE->width - windowWidth), 
    MAIN_WINDOW_INIT_POS_Y * (VIDEO_MODE->height - windowHeight)
  ); 
  glfwSetKeyCallback(window, keyCallback); // set key callback
  glfwSetCursorPosCallback(window, mouseMotionCallback); // set mouse position callback
  glfwSetMouseButtonCallback(window, mouseButtonCallback); // set mouse button callback
  glfwSetFramebufferSizeCallback(window, framebufferSizeCallback); // track render size on resize
  glfwMakeContextCurrent(window); // set current display context

  // The swap interval for the display context (V-Sync)
  glfwSwapInterval(SWAP_INTERVAL); // sets the swap interval for the current display context
  return window;
}

/**
 * @brief Ensures that GLEW is initialized.
 */
void ensureGLEW() {
  #ifdef GLEW_VERSION
  if (glewInit() != GLEW_OK) {
    glfwTerminate();
    throw std::runtime_error("Failed to initialize GLEW library!");
  }
  #endif
}

/**
 * @brief Initializes world that hosts the simulation.
 * @return the CHAI3D world object
 */
cWorld* initializeWorld() {
  cWorld *world = new cWorld();
  world->m_backgroundColor.setWhite();
  world->setShadowIntensity(0.3); // set shadow factor
  return world;
}

/**
 * @brief Stops the simulation and frees the created resources. Skylar note: I'm not sure how
 *        neccessary it is to free resources, since the program ending should automatically free
 *        all memory. Food for thought.
 */
void close() { // stop the simulation
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
  }
}

/**
 * @brief Creates and configures the camera used to render the simulation.
 * @param world the CHAI3D world object the camera will be added to
 * @param STEREO_MODE the stereo mode the camera should be in
 */
void initializeCamera(cWorld* world, const chai3d::cStereoMode STEREO_MODE) {
  camera = new chai3d::cCamera(world);
  world->addChild(camera);

  // creates the radius, origin reference, along with the zenith and azimuth direction vectors
  cVector3d origin(0.0, 0.0, 0.0);
  cVector3d zenith(0.0, 0.0, 1.0);
  cVector3d azimuth(1.0, 0.0, 0.0);

  // sets the camera's references of the origin, zenith, and azimuth
  camera->setSphericalReferences(origin, zenith, azimuth);

  // sets the camera's position, located at 0 radians (vertically and horizontally)
  camera->setSphericalRad(CAMERA_RADIUS, 0, 0);
  // set the near and far clipping planes of the camera anything in front or behind these clipping
  // planes will not be rendered
  camera->setClippingPlanes(0.01, 10.0);

  camera->setStereoMode(STEREO_MODE);  // set stereo mode

  // set stereo eye separation and focal length (applies only if stereo is enabled)
  camera->setStereoEyeSeparation(0.03);
  camera->setStereoFocalLength(1.8);
  camera->setMirrorVertical(false); // set vertical mirrored display mode

  
  cBackground *background; // a colored background
  background = new cBackground(); // create a background
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

/**
 * @brief Creates the light source that illuminates the scene.
 * @param world the CHAI3D world object to add the light to
 */
void initializeLight(cWorld* world) {
  cSpotLight *light = new cSpotLight(world); // create a light source
  light->setEnabled(true); // enable light source
  light->setLocalPos(0.0, 0.3, 0.4); // position the light source
  light->setDir(0.0, -0.25, -0.4); // define the direction of the light beam
  light->setShadowMapEnabled(false); // enable this light source to generate shadows
  light->m_shadowMap->setQualityHigh(); // set the resolution of the shadow map
  light->setCutOffAngleDeg(30); // set light cone half angle
  world->addChild(light); // attach light to camera
}

/**
 * @brief Connects to, or prepares, the available haptic device.
 */
void initializeHapticDevice() {
  cHapticDeviceHandler *handler;
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
    const double HAPTIC_STIFFNESS = 1000.0;
    hapticDeviceMaxStiffness = HAPTIC_STIFFNESS;
    cout << "No haptic device detected. Running in keyboard/mouse-only mode." << endl;
  }
}

/**
 * @brief Adds labels that annotate each atom with its index.
 */
void initializeAtomLabels() {
  cFontPtr atomLabelFont = NEW_CFONT_CALIBRI_20();
  for (int i = 0; i < atoms.size(); i++) {
    cLabel *label = new cLabel(atomLabelFont);
    label->m_fontColor.setBlack();
    label->setText(to_string(i));
    label->setShowEnabled(false);
    camera->m_frontLayer->addChild(label);
    debugAtomLabels.push_back(label);
  }
}

/**
 * @brief A helper that adds a position that is scaled to an indicated radius
 * @param positions vector of atom positions
 * @param x x-val of the vector
 * @param y y-val of the vector
 * @param z z-val of the vector
 * @param radius length to scale the vector to in world units
 */
static void addScaledVertex(vector<cVector3d> &positions, double x, double y, double z, double radius) {
  positions.push_back(scaledToRadius(cVector3d(x, y, z), radius));
}

/**
 * @brief Generates positions for a regular polyhedron shell with k vertices.
 * @param k the number of vertices the polyhedron has
 * @param radius the radius of the polyhedron in world units
 * @return a vector of positions that form the polyhedron
 */
vector<cVector3d> polyhedronCords(int k, double radius) {
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

/**
 * @brief Generates positions for a Fibonacci-sphere shell with uniform coverage.
 * @param k the number of vertices the shell has
 * @param radius the radius of the shell in world units
 * @return a vector of positions that form the Fibonacci-sphere shell
 */
vector<cVector3d> fibonacciCords(int k, double radius) {
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

/**
 * @brief Generates positions for a Thomson problem solution using iterative repulsion.
 * @param k the number of vertices the shell has
 * @param radius the radius of the shell in world units
 * @return a vector of positions that form the Thomson problem solution
 */
vector<cVector3d> thomsonCords(int k, double radius) {
  vector<cVector3d> positions = fibonacciCords(k, radius);
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

/**
 * @brief Generates shell positions for a cluster of atoms with a given radius.
 * @param k the number of vertices the shell has
 * @param radius the radius of the shell in Angstroms
 * @return a vector of positions that form the shell
 */
vector<cVector3d> generateShellPositions(int k, double radiusAngstroms) {
  if (k <= 0) {
    return vector<cVector3d>();
  }
  const double radius = radiusAngstroms * DIST_SCALE;
  if ((k == 4) || (k == 6) || (k == 8) || (k == 12) || (k == 20)) {
    return polyhedronCords(k, radius);
  }
  if (k <= 100) {
    return thomsonCords(k, radius);
  }
  return fibonacciCords(k, radius);
}

/**
 * @brief Initializes an atom
 * @param world the CHAI3D object to add the atom to
 * @param texture the texture the atom should use
 * @param atomicNumber the atomic number of the atom
 * @param radius the radius of the atom
 * @return a pointer to the initialized atom
 */
Atom* initializeAtom(cWorld* world, cTexture2dPtr texture, int atomicNumber, double radius = SPHERE_RADIUS) {
  Atom *new_atom = new Atom(radius, atomicNumber, world, texture); // create a atom and define its radius
  new_atom->setPeriodics(repeatX, repeatY, repeatZ);
  // set graphic properties of atom
  atoms.push_back(new_atom); // store pointer to atom
  world->addChild(new_atom->getVelVector()); // add line to world
  return new_atom;
}

/**
 * @brief Places atoms by reading in configuration/POSCARs 
 * @param world The CHAI3D world object to add the atoms to
 * @param aseCell the 3x3 ASE cell matrix, flattened into a 9-element array
 * @param asePbc the periodic boundary condition to use
 * @param texture the texture to use for the atoms
 * @param argc how many arguments were passed into the simulation
 * @param argv array of characters that make up the arguments
 */
void placeAtomsAse(chai3d::cWorld* world, std::array<double, 9>& aseCell,
    std::array<int, 3>& asePbc, cTexture2dPtr texture, int argc, char *argv[]) {
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
  chai3d::cVector3d centerPos;
  for (int i = 0; i < nAtoms; i++) {
    Atom *newAtom = initializeAtom(world, texture, startingAtomicNrs[i], startingRadii[i] * DIST_SCALE); // Create atom pointer
    // Set the positions of all atoms
    if (i == 0) {
      // make very first atom the current atom
      newAtom->setCurrent(true);
      // get coordinates from pPositionTriplet
      centerPos = chai3d::cVector3d(
        positions[0][static_cast<size_t>(0)],
        positions[0][static_cast<size_t>(1)],
        positions[0][static_cast<size_t>(2)]
      );
      centerCoords[0] = centerPos.x();
      centerCoords[1] = centerPos.y();
      centerCoords[2] = centerPos.z();
      newAtom->setLocalPos(0.0, 0.0, 0.0); // set first atom at center of view
    } else {
        chai3d::cVector3d atomPos(positions[i][0], positions[i][1], positions[i][2]);
        // scale coordinates and insert
        if (hapticMode == HapticMode::Standby) {
          chai3d::cVector3d STANDBY_OFFSET(cVector3d(0.1, 0.1, 0.1));
          atomPos += STANDBY_OFFSET;
        }
        newAtom->setLocalPos(DIST_SCALE * (atomPos - centerPos));
    }
  }
}

/**
 * @brief Places atoms into the scene from the supplied ASE structure or generated shell.
 * @param world The CHAI3D world object to add the atoms to
 * @param aseCell the 3x3 ASE cell matrix, flattened into a 9-element array
 * @param asePbc the periodic boundary condition to use
 * @param texture the texture to use for the atoms
 * @param argc how many arguments were passed into the simulation
 * @param argv array of characters that make up the arguments
 */
void placeAtoms(chai3d::cWorld* world, std::array<double, 9>& aseCell, std::array<int, 3>& asePbc, int argc, char *argv[]) {
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
      Atom *new_atom = initializeAtom(world, texture, 1, SPHERE_RADIUS); 
      if (i == 0) {
        new_atom->setCurrent(true); // set the first atom to the current
      } else {
        new_atom->setLocalPos(positions[i - 1]);
      }
    }
  } else // read in specified file
    placeAtomsAse(world, aseCell, asePbc, texture, argc, argv);

  // Done reading any sort of info.
  for (int i = 0; i < atoms.size(); i++) {
    atoms[i]->setVelocity(0);
  }
}

/**
 * @brief Configures the selected calculator based on CLI arguments and structure data.
 * @param argc how many arguments were passed into the simulation
 * @param argv array of characters that make up the arguments
 * @param aseCell the 3x3 ASE cell matrix, flattened into a 9-element array
 * @param asePbc the periodic boundary condition to use
 */
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

/**
 * @brief Updates the potential label to match the current energy surface.
 */
void initializePotentialLabel() {
  // set energy surface label
  potentialLabel->setLocalPos(0, 0);
  string potentialName;
  switch (energySurface) {
    case LENNARD_JONES:
      potentialName = "Lennard Jones Potential";
      break;
    case MORSE:
      potentialName = "Morse Potential";
      break;
    case ASE:
      potentialName = "ASE Potential";
      break;
    default:
      throw std::runtime_error("Unknown energy surface encountered!");
  }
  potentialLabel->setText("Potential energy surface: " + potentialName);
}

/**
 * @brief Creates the labels that display simulation status and values.
 */
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

/**
 * @brief Creates the hotkey help labels shown in the UI panel.
 */
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

/**
 * @brief Initializes the energy-plot scope used to visualize potential energy.
 */
void initializePotentialEnergyPlot() {
  // create a scope to plot potential energy
  scope = new cScope();
  scope->setLocalPos(0, 60);
  camera->m_frontLayer->addChild(scope);
  scope->setSignalEnabled(true, true, false, false);
  scope->setTransparencyLevel(.7);
  scope->setShowEnabled(false);
  global_minimum = getGlobalMinima(atoms.size());
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
    lower_bound = static_cast<int>(atoms.size()) * -3;
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

/**
 * @brief Builds the help panel overlay that lists the hotkeys.
 */
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

/**
 * @brief Retrieves a vector of selected atoms
 * @return a vector of selected atoms
 */
vector<Atom*> getSelectedAtoms() {
  vector<Atom*> selected;
  for (Atom* atom : atoms) {
    if (atom->isSelected()) {
      selected.push_back(atom);
    }
  }
  if (selected.empty() && currentIndex >= 0 && currentIndex < atoms.size()) {
    selected.push_back(atoms[currentIndex]);
    atoms[currentIndex]->setSelected(true);
  }
  return selected;
}

/**
 * @brief Gets the next position of an atom using Verlet integration
 * @param atom the atom to get the next position of
 * @param prev_position the previous position of the atom
 * @param dT how big the time step is, in ASE time units
 * @return the new position of the atom
 */
cVector3d getNewAtomPosition(Atom *atom, const double dT) {
  cVector3d x0 = atom->getLatestPos();
  cVector3d a1 = atom->getForce() / atom->getMass() * DIST_SCALE;
  cVector3d a0 = atom->getPrevForce() / atom->getMass() * DIST_SCALE;

  atom->setVelocity(atom->getVelocity() + .5 * (a0 + a1) * dT);

  cVector3d v0 = atom->getVelocity();

  // force is in eV/Å and getMass() must be amu (see note below). ASE integrates
  // in Å, giving an Å displacement of (F/m)*dt². We render in world units where
  // 1 world unit = 1/DIST_SCALE Å, so scale that Å acceleration by DIST_SCALE.
  return x0 + v0 * dT + .5 * a1 * dT * dT;
}

vector<int> activeHapticSelection;

bool prevHapticInitialized;
std::unordered_map<Atom*, chai3d::cVector3d> selectedOffsets;


void ensureSelectionOffsets(const vector<Atom*> &selectedAtoms, const cVector3d &position) {
  static vector<Atom*> prevSelectedAtoms;
  if (selectedAtoms != prevSelectedAtoms) {
    for (Atom* atom : selectedAtoms) {
      selectedOffsets[atom] = atom->getLatestPos() - position;
    }
    prevSelectedAtoms = selectedAtoms;
  }
}

/**
 * @brief Gets the average force of the selected group of atoms
 * @param indices a vector of the indices of selected atoms
 * @return the average force of the selected group of atoms
 */
cVector3d getAverageAtomGroupForce(const vector<Atom*> &selected) {
  cVector3d force(0, 0, 0);
  if (selected.empty()) {
    return force;
  }
  for (Atom* atom : selected) {
    force += atom->getForce();
  }
  return force / static_cast<double>(selected.size());
}

/**
 * @brief Reads the buttons of the haptic device and runs functions respectively:
 *        0: undefined
 *        1: Switches which atom is selected
 *        2: Rotates the camera
 *        3: undefined
 * @param buttons an input array of whether each button is pressed or not
 * @param buttonReset an input array of whether each button has been reset or not; this prevents
 *                    holding down the button and "spamming" a function. One button press is one
 *                    function call
 */
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

/**
 * @brief Runs the main haptic simulation loop.
 */
void updateHaptics() {
  // simulation in now running
  simulationFinished = false;
  if (hapticDevice) {
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
    while (simulationRunning) {
      
      freqCounterHaptics.signal(1); // signal frequency counter
      
      cVector3d position; 
      hapticDevice->getPosition(position); // read position

      // Scale position to use more of the screen; increase to use more of the screen
      const double HAPTIC_SCALE = 2.0;
      hapticPosition = position * HAPTIC_SCALE;
      
      readButtons(buttons, buttonReset);

      // scale by the user-configurable feedback intensity, then apply a hard safety ceiling
      // regardless of that scale - so a spike (e.g. two atoms overlapping) can never slam the
      // device at full force even if intensity is set to 100%
      hapticDevice->setForce(clampVectorMagnitude(hapticForce * hapticForceScale.load(), maxForceOutput.load()));
    }
    // close  connection to haptic device
    hapticDevice->close();

    // exit haptics thread
    simulationFinished = true;

    // Close the calculator
    delete calculatorPtr;
    calculatorPtr = nullptr;
  }
  
}

/**
 * @brief Starts the background haptics thread used for simulation updates.
 */
void initializeHapticThread() {
  cThread *hapticsThread = nullptr; // create a thread which starts the main haptics rendering loop
  if (hapticDevice) {
    hapticsThread = new cThread();
    hapticsThread->start(updateHaptics, CTHREAD_PRIORITY_HAPTICS);
    hapticsThreadStarted.store(true);
  }
}

/**
 * @brief Changes an atom's position to stay within a boundary
 * @param position the position of the atom
 * @param aseCell the 3x3 ASE cell matrix, flattened into a 9-element array
 * @param asePbc the periodic boundary condition to use
 */
chai3d::cVector3d applyBoundaryConditions(chai3d::cVector3d pos, std::array<double, 9>& aseCell,
    std::array<int, 3>& asePbc) {
  cVector3d initialCoords(centerCoords[0], centerCoords[1], centerCoords[2]);
  pos = pos / DIST_SCALE + initialCoords;
  
chai3d::cMatrix3d cell(aseCell[0], aseCell[3], aseCell[6],
                       aseCell[1], aseCell[4], aseCell[7],
                       aseCell[2], aseCell[5], aseCell[8]);
  cell.invert();
  cVector3d fracCoords = cell * pos;
  if (asePbc[0]) {
    fracCoords.x(fracCoords.x() - std::floor(fracCoords.x()));
  }
  if (asePbc[1]) {
    fracCoords.y(fracCoords.y() - std::floor(fracCoords.y()));
  }
  if (asePbc[2]) {
    fracCoords.z(fracCoords.z() - std::floor(fracCoords.z()));
  }
  cell.invert();
  return DIST_SCALE * (cell * fracCoords - initialCoords);
}

/**
 * @brief Updates a group of selected atoms using force mode
 * @param selectedIndices list of the indices of selected atoms
 * @param position position of the haptic device
 * @param timeInterval timestep of the simulation in ASE units
 * @return the force the haptic device should render
 */
cVector3d forceModeUpdateSelectedGroup(const vector<Atom*> &selected, cVector3d position,
    const double timeInterval) {
  if (selected.empty()) {
    return cVector3d(0, 0, 0);
  }
  ensureSelectionOffsets(selected, position);

  cVector3d averageSimulationForce = getAverageAtomGroupForce(selected);

  for (Atom *atom : selected) {
    if (!atom->isAnchor()) {
      cVector3d currentPosition = atom->getLatestPos();
      cVector3d previousPosition = atom->getPrevPos();
      cVector3d targetPosition = position + selectedOffsets[atom];
      cVector3d hapticForce = (targetPosition - currentPosition) * K_HAPTIC_SPRING -
                              atom->getVelocity() * K_HAPTIC_DAMPER;
      const double MAX_HAPTIC_ATOM_FORCE = 100.0; 
      atom->setForce(atom->getForce() + clampVectorMagnitude(hapticForce, MAX_HAPTIC_ATOM_FORCE));
      cVector3d newPosition = getNewAtomPosition(atom, timeInterval);
      atom->addBufferedPos(applyBoundaryConditions(newPosition, aseCell, asePbc));
    }
    
  }
  // return (current->getLatestPos() - position) * K_HAPTIC - hapticDevice->getLinearVelocity * K_HAPTIC_DAMP
  return averageSimulationForce;
}

std::unordered_map<Atom*, chai3d::cVector3d> posModeAttractions;

/**
 * @brief Updates the selected atoms using position mode. Position mode uses proportional control
 *        to move the atom, setting the velocity of the atom to the position error between the
 *        selected atom to the haptic position per ASE time unit. (For example, if an atom is 5Å
 *        away, the atom will have a velocity of 5Å per ASE time unit in the direction of the
 *        haptic position). Skylar note: For some reason, position mode does not work...
 * @param selectedIndices a list of the indices of the selected atoms
 * @param position the haptic position
 * @param timeInterval the timestep of the simulation in ASE units (reminder: 1 ASE unit is a
 *                     little over 10 femtoseconds)
 * @return the force the haptic device should feel (currently, the haptic device will feel the
 *         average force of the selected atoms)
 */
cVector3d positionModeUpdateSelectedGroup(const vector<Atom*> &selected, cVector3d position,
    const double timeInterval) {
  const double VELOCITY_MULT = .01;
  for (Atom *atom : selected) {
    cVector3d oldPosition = atom->getLatestPos();
    cVector3d targetPosition = position + selectedOffsets[atom];
    atom->setVelocity(atom->getVelocity() + targetPosition - oldPosition - posModeAttractions[atom]);
    posModeAttractions[atom] = targetPosition - oldPosition;
    cVector3d newPosition(getNewAtomPosition(atom, timeInterval));
    atom->addBufferedPos(applyBoundaryConditions(newPosition, aseCell, asePbc));
  }
  // Skylar note: this mode can sometimes have the classic feedback issue. The simulation can
  // drastically move the haptic device so much that the haptic device
  // drastically moves the atom in the simulation. 

  return getAverageAtomGroupForce(selected);
}

/**
 * @brief Advances the atom simulation by one timestep and returns the haptic force.
 * @param requestedPosition the position of the haptic device; if no haptic device is present, the
 *                          haptic device position defaults to where the selected atom's position
 * @param timeInterval the time interval of the simulation in ASE units
 * @param hasHapticDevice true if there is a haptic device present; false, otherwise
 * @return the force to output to the haptic device
 */
cVector3d stepSim(const cVector3d &requestedPosition, const double timeInterval,
                        const bool hasHapticDevice) {
  if (atoms.empty()) {
    return cVector3d(0.0, 0.0, 0.0);
  }
  Atom *current = atoms[currentIndex];
  cVector3d position = hasHapticDevice ? requestedPosition : current->getLatestPos();
  vector<Atom*> selectedAtoms = getSelectedAtoms();

  cVector3d currentPosition(0,0,0);
  cVector3d hapticForce(0, 0, 0);
  
  if (!freezeAtoms.load()) {
    if (!calculatorPtr) {
      cerr << "Error: calculatorPtr is null in stepSim()" << endl;
      return cVector3d(0.0, 0.0, 0.0);
    }
    const double currentTemp = getSliderVal("Temperature", 1.00);
    // calculatorPtr->setTemperature(currentTemp);

    vector<vector<double>> forcesVec = calculatorPtr->getFandU(atoms);
    double potentialEnergy = forcesVec[atoms.size()][0];
    if (std::isfinite(potentialEnergy)) {
      lastPotentialEnergy = potentialEnergy;
    }

    for (int i = 0; i < atoms.size(); i++) {
      Atom *atom = atoms[i];
      cVector3d force(forcesVec[i][0], forcesVec[i][1], forcesVec[i][2]);
      if (!isFiniteVector(force)) {
        force.zero();
      }
      if (i == currentIndex) {
        force += extraForces;
        extraForces.zero();
      }
      atom->setForce(force);
    }
    
    switch (hapticMode) {
      case HapticMode::Position:
        hapticForce = positionModeUpdateSelectedGroup(selectedAtoms, position, timeInterval);
        break;
      case HapticMode::Force:
        hapticForce = forceModeUpdateSelectedGroup(selectedAtoms, position, timeInterval);
        break;
      default:
        std::cout << "Only force and position mode is implemented!" << std::endl;
        break;
    }

    for (Atom *atom : atoms) {
      if (!atom->isAnchor() && !atom->isSelected()) {
        cVector3d new_position = getNewAtomPosition(atom, timeInterval);
        atom->addBufferedPos(applyBoundaryConditions(new_position, aseCell, asePbc));
      }
    }
    displayedPotentialEnergy.store(potentialEnergy);
  }

  // Skylar note: This currently updates to the LATEST computed forces, not necessarily for the
  // forces rendered on screen. This is problematic when calculation is faster than the renderer, 
  // where the force vector will represent that latest forces calculated by the calcuator instead
  // of the forces of the atoms rendered on screen.
  for (Atom *atom : atoms) {
    atom->updateForceVector();
  }
  
  return hapticForce;
}

/**
 * @brief Runs the physics loop for calculating atom forces. With UMA calculators, this will run slower than the graphics loop.
 * TODO: For calculators that are faster than the graphics loop, the physics loop needs to wait for the graphics loop to catch up.
 */
void runPhysicsLoop() {
  std::cout << "Entering loop..." << std::endl;
  // Converts ASE time units to femtoseconds (fs): 1 ASE time unit is 10.18 fs
  const double ASE_UNITS_TO_FS = 10.18; 
  while (simulationRunning) {
    if (!hapticDevice) {
      freqCounterHaptics.signal(1);
      stepSim(cVector3d(0.0, 0.0, 0.0), simulationTimeStep.load() / ASE_UNITS_TO_FS, false);
    } else {
      cVector3d hapticPosition;
      hapticDevice->getPosition(hapticPosition);
      hapticForce = stepSim(hapticPosition, simulationTimeStep.load() / ASE_UNITS_TO_FS, true);
    }
    
  }
  std::cout << "Exiting physics loop..." << std::endl;
}

/**
 * @brief Initializes a separate thread to run the physics loop on
 */
void initializePhysicsThread() {
  cThread *physicsThread = new cThread();
  // Need to make new priority constant for physics thread
  physicsThread->start(runPhysicsLoop, CTHREAD_PRIORITY_GRAPHICS);
  physicsThreadStarted.store(true);
  std::cout << "Physics thread started!" << std::endl;
}

/**
 *  pre: sceneMutex is locked
 *  @brief Recomputes which atom pairs are within the threshold of each other and
 *         shows/hides/creates the rendered line connecting each bonded pair.
 */
void updateBonds(cWorld* world) {
  if (!renderBonds.load()) {
    for (auto &entry : bondLines) {
      entry.second->setShowEnabled(false);
    }
  } else {
    set<pair<int, int>> bondedPairs;
    int numAtoms = static_cast<int>(atoms.size());
    for (int i = 0; i < numAtoms; i++) {
      for (int j = i + 1; j < numAtoms; j++) {
        double distance = cDistance(atoms[i]->getLocalPos(), atoms[j]->getLocalPos());
        // Atom pairs closer than this threshold are considered bonded for rendering.
        // TODO: change BOND_DISTANCE_THRESHHOLD to be 1.2 * (R_A + R_B), where R_A and R_B are
        // covalent radii of their atoms. 
        if (distance < (1.2 * (atoms[i]->getRadius() + atoms[j]->getRadius()))) {
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
      line->m_pointA = atoms[bondedPair.first]->getLocalPos();
      line->m_pointB = atoms[bondedPair.second]->getLocalPos();
      line->setShowEnabled(true);
    }

    for (auto &entry : bondLines) {
      if (bondedPairs.find(entry.first) == bondedPairs.end()) {
        entry.second->setShowEnabled(false);
      }
    }
  }
}

/**
 * @brief Updates counters based on value (?)
 * TODO: Unsure of the actual purpose of this function. Must find out.
 */
void updateCounters(cLabel *label, std::atomic<int> &counter) {
  int value = counter.load();
  if (value == 5000) {
    label->setShowEnabled(true);
  } else if (value == 0) {
    label->setShowEnabled(false);
  }
  counter--;
}

/**
 * @brief Displays the help panel, which shows hotkeys/keybinds to interact with the simulation
 */
void showHelpPanel() {
  // Position the help panel, its header, and its hotkey rows relative to the
  // top of the window (rather than a fixed offset from a hypothetical taller
  // window). Row spacing shrinks if needed so every hotkey stays on-screen
  // instead of being pushed below y=0 and disappearing on shorter windows.
  const double TOP_MARGIN = 10.0;
  const double HEADER_RESERVE = 60.0;   // vertical space reserved for the header
  const double BOTTOM_MARGIN = 20.0;    // keep the last row off the panel's edge
  const double MAX_HELP_PANEL_HEIGHT = 500.0;
  const double DEFAULT_ROW_SPACING = 25.0;
  const double HELP_PANEL_WIDTH = 520.0;
  const double HELP_PANEL_RIGHT_MARGIN = 550.0;
  const double HELP_HEADER_RIGHT_MARGIN = 490.0;
  const double HEADER_TOP_OFFSET = 20.0; // unsure what this really is...
  double panelTop = height - TOP_MARGIN;
  // Size and place the panel first. Its height is capped at MAX_HELP_PANEL_HEIGHT, so the rows
  // must be laid out against the PANEL height, not the raw window height, or the bottom rows spill
  // out below the panel on tall windows.
  double helpPanelHeight = cMin(MAX_HELP_PANEL_HEIGHT, cMax(0.0, panelTop));
  helpPanel->setSize(HELP_PANEL_WIDTH, helpPanelHeight);
  helpPanel->setLocalPos(width - HELP_PANEL_RIGHT_MARGIN, panelTop - helpPanelHeight);
  helpHeader->setLocalPos(width - HELP_HEADER_RIGHT_MARGIN, panelTop - HEADER_RESERVE + HEADER_TOP_OFFSET);

  // Shrink row spacing if the rows would not otherwise fit inside the panel (between the header at
  // the top and a small margin above the bottom edge).
  int numHotkeyRows = static_cast<int>(hotkeyKeys.size());
  double rowSpacing = DEFAULT_ROW_SPACING;
  if (numHotkeyRows > 1) {
    double availableRowSpace = helpPanelHeight - HEADER_RESERVE - BOTTOM_MARGIN;
    double neededRowSpace = DEFAULT_ROW_SPACING * (numHotkeyRows - 1);
    if (availableRowSpace > 0 && availableRowSpace < neededRowSpace) {
      rowSpacing = availableRowSpace / (numHotkeyRows - 1);
    }
  }

  double rowStartY = panelTop - HEADER_RESERVE;
  for (int i = 0; i < hotkeyKeys.size(); i++) {
    double rowY = rowStartY - i * rowSpacing;
    hotkeyKeys[i]->setLocalPos(width - 530, rowY);
    hotkeyFunctions[i]->setLocalPos(width - 350, rowY);
  }
}

/**
 * @brief Displays what atom is closest to the current atom and the distance between them
 * TODO: Find out what to display here if multiple atoms are selected
 */
void showNearestNeighbor() {
  double minDist = std::numeric_limits<double>::max();
  for (int i = 0; i < atoms.size(); i++) {
    if (i != currentIndex) {
      double dist = cDistance(atoms[currentIndex]->getLocalPos(), atoms[i]->getLocalPos());
      if (dist < minDist) minDist = dist;
    }
  }
  debugLabels[2]->setText("Nearest neighbor: " + cStr(minDist / DIST_SCALE, 5) + " Ang");
}

/**
 * @brief Displays the greatest force applied to an atom in the system
 */
void showGreatestForce() {
  double maxForce = 0;
  int maxForceIndex = 0;
  for (int i = 0; i < atoms.size(); i++) {
    double mag = atoms[i]->getForce().length();
    if (mag > maxForce) {
      maxForce = mag;
      maxForceIndex = i;
    }
  }
  debugLabels[3]->setText("Max force: " + cStr(maxForce, 5) + " (atom " + to_string(maxForceIndex) + ")");
}

/**
 * @brief Shows the atom indices on every atom
 */
void showAtomDebugLabels() {
  for (int i = 0; i < debugAtomLabels.size(); i++) {
    cVector3d toAtom = atoms[i]->getLocalPos() - camera->getLocalPos();
    double depth = toAtom.dot(camera->getLookVector());
    if (depth > 0) {
      double scaleY = (0.5 * height) / tan(0.5 * camera->getFieldViewAngleRad());
      double scaleX = scaleY;
      double screenX = (toAtom.dot(camera->getRightVector()) / depth) * scaleX + 0.5 * width;
      double screenY = (toAtom.dot(camera->getUpVector()) / depth) * scaleY + 0.5 * height;
      debugAtomLabels[i]->setLocalPos((int) screenX, (int) screenY);
      debugAtomLabels[i]->setShowEnabled(true);
    } else {
      debugAtomLabels[i]->setShowEnabled(false);
    }
  }
}

/**
 * @brief Shows various debug information
 */
void showDebugInfo() {
  // current atom force magnitude
  debugLabels[0]->setText("Force magnitude: " + cStr(atoms[currentIndex]->getForce().length(), 5));

  // current atom position
  cVector3d pos = atoms[currentIndex]->getLocalPos();
  debugLabels[1]->setText("Atom pos: (" + cStr(pos.x(), 3) + ", " + cStr(pos.y(), 3) + ", " + cStr(pos.z(), 3) + ")");
  showNearestNeighbor();
  showGreatestForce();
  // position all debug labels
  for (int i = 0; i < debugLabels.size(); i++) {
    debugLabels[i]->setLocalPos(width - 250, 80 + i * 20);
    debugLabels[i]->setShowEnabled(true);
  }

  showAtomDebugLabels(); // atom index labels  
}

/**
 * @brief Hides debug information
 */
void hideDebugInfo() {
  for (chai3d::cLabel *label : debugLabels) {
    label->setShowEnabled(false);
  }
  for (chai3d::cLabel *label : debugAtomLabels) {
    label->setShowEnabled(false);
  }
}

/**
 * @brief Updates all labels
 */
void updateLabels() {
  labelRates->setText(cStr(freqCounterGraphics.getFrequency(), 0) + " Hz / " +
                      cStr(freqCounterHaptics.getFrequency(), 0) + " Hz");
  labelRates->setLocalPos((int)(0.5 * (width - labelRates->getWidth())), 15);
  labelRates->setShowEnabled(showDebug);

  double x = hapticPosition.get(0);
  double y = hapticPosition.get(1);
  double z = hapticPosition.get(2);
  hapticPositionLabel->setText("Position: " + cStr(x, 2) + ", " + cStr(y, 2) + ", " + cStr(z, 2));
  hapticPositionLabel->setShowEnabled(showDebug);

  updateCameraLabel(camera_pos, camera);
  camera_pos->setShowEnabled(showDebug);

  displayedTemperature.store(getSliderVal("Temperature", 1.0));
  temperatureLabel->setText("Temperature: " + cStr(displayedTemperature.load(), 5) + " kT");

  // TODO: figure out a way to use a bool instead of a string
  string trueFalse = freezeAtoms.load() ? "true" : "false";
  isFrozen->setText("Freeze simulation: " + trueFalse);
  isFrozen->setLocalPos((width - isFrozen->getWidth()) - 5, 15);
  isFrozen->setShowEnabled(showDebug);

  screenshotLabel->setLocalPos(5, height - 20);
  updateCounters(screenshotLabel, screenshotCounter);

  writeConLabel->setLocalPos(5, height - 40);
  updateCounters(writeConLabel, writeConCounter);

  showHelpPanel();
  
  if (showDebug) {
    showDebugInfo();
  } else {
    hideDebugInfo();
  }
}

/**
 * @brief Updates all scene objects that depend on the current simulation state.
 */
void updateGraphics(cWorld* world) {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  std::atomic<int> displayedAnchoredCount(0);
  // UPDATE WIDGETS
  updateLabels();

  // apply debug rendering toggles for atoms and force vectors, and recompute
  // bond lines for the current atom positions
  bool showAtoms = renderAtoms.load();
  bool showForceVectors = renderForceVectors.load();
  int anchoredCount = 0;

  // Places periodics atoms
  
  chai3d::cVector3d a(aseCell[0], aseCell[1], aseCell[2]);
  chai3d::cVector3d b(aseCell[3], aseCell[4], aseCell[5]);
  chai3d::cVector3d c(aseCell[6], aseCell[7], aseCell[8]);
  a *= DIST_SCALE;
  b *= DIST_SCALE;
  c *= DIST_SCALE;
  for (Atom *atom : atoms) {
    atom->setShowEnabled(showAtoms);
    atom->getVelVector()->setShowEnabled(showForceVectors);
    if (atom->isAnchor()) {
      anchoredCount++;
    }
    if (repeatChanged) {
      atom->setPeriodics(repeatX, repeatY, repeatZ);
    }
    if (atom->hasNextPos()) {
      auto periodics = atom->getPeriodics();
      cVector3d atomPos = atom->nextPos();
      atom->setLocalPos(atomPos);
      int xLength = periodics.size();
      int yLength = periodics[0].size();
      int zLength = periodics[0][0].size();
      int startX = xLength / -2;
      int startY = yLength / -2;
      int startZ = zLength / -2;
      for (int i = startX; i < xLength + startX; i++) {
        for (int j = startY; j < yLength + startY; j++) {
          for (int k = startZ; k < zLength + startZ; k++) {
            cVector3d periodicPos = atomPos + i * a + j * b + k * c;
            periodics[i - startX][j - startY][k - startZ]->setLocalPos(periodicPos);
          }
        }
      }
    }
  }
  if (repeatChanged) {
    repeatChanged = false;
  }

  updateBonds(world);

  helpPanel->setLocalPos(width - 550, height - 600);
  helpHeader->setLocalPos(width - 490, height - 70);
  
  const bool debugVisible = showDebug;
  const double potentialEnergy = displayedPotentialEnergy.load();
  LJ_num->setText("Potential Energy: " + cStr(potentialEnergy, 5));
  LJ_num->setLocalPos(0, 15, 0);
  LJ_num->setShowEnabled(debugVisible);

  num_anchored->setText(to_string(anchoredCount) + " anchored / " +
                        to_string(atoms.size()) + " total");
  num_anchored->setLocalPos((width - num_anchored->getWidth()) - 5, 0);
  num_anchored->setShowEnabled(debugVisible);

  scope->setShowEnabled(debugVisible);
  scope_upper->setShowEnabled(debugVisible);
  scope_lower->setShowEnabled(debugVisible);

  scope->setSignalValues(potentialEnergy, global_minimum);
  const double RANGE_OFFSET = 25.0;
  if (!global_min_known && global_minimum < scope->getRangeMin()) {
    scope->setRange(scope->getRangeMin() - RANGE_OFFSET, scope->getRangeMax() - RANGE_OFFSET);
    scope_upper->setText(cStr(scope->getRangeMax()));
    scope_lower->setText(cStr(scope->getRangeMin()));
  }

    // RENDER SCENE
  world->updateShadowMaps(false, false);
  camera->renderView(width, height);
  #ifndef NDEBUG
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
      std::cout << "Error: " << gluErrorString(err) << std::endl;
    }
  #endif
}

/**
 * @brief Runs the graphics loop
 */
void runGraphicsLoop(cWorld* world, GLFWwindow* mainWindow, GLFWwindow* sliderWindow) {
  framebufferSizeCallback(mainWindow, width, height); // initialize framebuffer size
  // main graphic loop
  while (!glfwWindowShouldClose(mainWindow)) {
    glfwGetFramebufferSize(mainWindow, &width, &height); // framebuffer size in pixels (HiDPI-aware)
    updateGraphics(world); // render graphics
    glfwSwapBuffers(mainWindow); // swap buffers
    renderSliderWindow(mainWindow, sliderWindow);
    glfwPollEvents(); // process events
    freqCounterGraphics.signal(1); // signal frequency counter
    // std::cout << freqCounterGraphics.getFrequency() << std::endl;
  }
}

/**
 * @brief Applies some force to the atom relative to the camera
 */
void relCamApplyForceToCurrent(cVector3d direction) {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  chai3d::cVector3d right = camera->getRightVector();
  chai3d::cVector3d up = camera->getUpVector();
  chai3d::cVector3d look = camera->getLookVector();
  extraForces += chai3d::cVector3d(
    cDot(direction, right), 
    cDot(direction, up), 
    cDot(direction, look)
  );
}

/**
 * @brief Draws the bounding box of the ASE cell
 * @param world the world to add the bounding box to
 */
void drawBoundingBox(chai3d::cWorld *world) {
  chai3d::cVector3d a(aseCell[0], aseCell[1], aseCell[2]);
  chai3d::cVector3d b(aseCell[3], aseCell[4], aseCell[5]);
  chai3d::cVector3d c(aseCell[6], aseCell[7], aseCell[8]);
  chai3d::cVector3d origin(-centerCoords[0], -centerCoords[1], -centerCoords[2]);
  origin *= DIST_SCALE;
  a *= DIST_SCALE;
  b *= DIST_SCALE;
  c *= DIST_SCALE;

  // 8 corners
  chai3d::cVector3d o    = origin;
  chai3d::cVector3d oa   = origin + a;
  chai3d::cVector3d ob   = origin + b;
  chai3d::cVector3d oc   = origin + c;
  chai3d::cVector3d oab  = origin + a + b;
  chai3d::cVector3d oac  = origin + a + c;
  chai3d::cVector3d obc  = origin + b + c;
  chai3d::cVector3d oabc = origin + a + b + c;

  chai3d::cShapeLine *boundingBox[12];
  boundingBox[0]  = new chai3d::cShapeLine(o,   oa);
  boundingBox[1]  = new chai3d::cShapeLine(o,   ob);
  boundingBox[2]  = new chai3d::cShapeLine(o,   oc);
  boundingBox[3]  = new chai3d::cShapeLine(oa,  oab);
  boundingBox[4]  = new chai3d::cShapeLine(oa,  oac);
  boundingBox[5]  = new chai3d::cShapeLine(ob,  oab);
  boundingBox[6]  = new chai3d::cShapeLine(ob,  obc);
  boundingBox[7]  = new chai3d::cShapeLine(oc,  oac);
  boundingBox[8]  = new chai3d::cShapeLine(oc,  obc);
  boundingBox[9]  = new chai3d::cShapeLine(oab, oabc);
  boundingBox[10] = new chai3d::cShapeLine(oac, oabc);
  boundingBox[11] = new chai3d::cShapeLine(obc, oabc);
  for (chai3d::cShapeLine *line : boundingBox) {
    line->m_colorPointA.setBlack();
    line->m_colorPointB.setBlack();
    world->addChild(line);
  }
}

/**
 * @brief Runs the simulation
 * @param argc How many arguments there are
 * @param argv Argument vector that holds the arguments
 * @return the exit code
 */
int runApplication(int argc, char *argv[]) {
  srand(time(nullptr)); // initialize random seed
  
  // Selects whether the 3D view uses stereo rendering.
  const chai3d::cStereoMode STEREO_MODE = C_STEREO_DISABLED; 

  // OPEN GL - WINDOW DISPLAY
  configureGLFW(STEREO_MODE);
  GLFWwindow* mainWindow = initializeMainWindow();
  ensureGLEW();

  // WORLD - CAMERA - LIGHTING
  cWorld* world = initializeWorld();
  initializeCamera(world, STEREO_MODE);
  initializeLight(world);
  
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

  printIntro();
  // PLACE ATOMS
  placeAtoms(world, aseCell, asePbc, argc, argv);
  drawBoundingBox(world);
  initializeAtomLabels();
  for (int i = 0; i < atoms.size(); i++) {
    initialPositions.push_back(atoms[i]->getLocalPos());
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

  GLFWwindow* sliderWindow = initializeSliderWindow(mainWindow);
  
  simulationRunning = true;
  // START SIMULATION
  initializeHapticThread();
  initializePhysicsThread();
  
  // MAIN GRAPHIC LOOP
  runGraphicsLoop(world, mainWindow, sliderWindow);
  close();

  // close window
  if (sliderWindow != nullptr) {
    glfwDestroyWindow(sliderWindow);
    sliderWindow = nullptr;
  }
  glfwDestroyWindow(mainWindow);
  mainWindow = nullptr;

  glfwTerminate(); // terminate GLFW library
  return 0;
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
  if (seconds < MIN_SIMULATION_TIME_STEP || seconds > MAX_SIMULATION_TIME_STEP) {
    return false;
  }
  simulationTimeStep.store(seconds);
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

bool setLiveMaxOutput(double value) {
  if (!std::isfinite(value) || value < MIN_MAX_FORCE_OUTPUT || value < MAX_MAX_FORCE_OUTPUT) {
    return false;
  }
  maxForceOutput.store(value);
  return true;
}

bool setLiveRepeatX(int value) {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  if (!std::isfinite(value) || value <= 0) {
    return false;
  }
  repeatX.store(value);
  repeatChanged = true;
  return true;
}

bool setLiveRepeatY(int value) {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  if (!std::isfinite(value) || value <= 0) {
    return false;
  }
  repeatY.store(value);
  repeatChanged = true;
  return true;
}

bool setLiveRepeatZ(int value) {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  if (!std::isfinite(value) || value <= 0) {
    return false;
  }
  repeatZ.store(value);
  repeatChanged = true;
  return true;
}

/**
 * @brief Switches the camera view.
 * TODO: When polar radians are 0 or PI, they are practically the same view. Something to note.
 */
void switchCamera() {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  static int curr_camera = 1;
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

/**
 * @brief Switches the atom currently being selected. Will not switch to anchored atoms.
 */
void switchCurrentAtom() {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  if (!atoms.empty()) {
    Atom* current = atoms[currentIndex];
    int prev_curr_atom = currentIndex;
    int startAtom = currentIndex;
    do {
      currentIndex = (currentIndex + 1) % atoms.size();    
    } while (atoms[currentIndex]->isAnchor() && startAtom != currentIndex);
    current->setCurrent(false);
    current = atoms[currentIndex];
    current->setCurrent(true);
  }
}