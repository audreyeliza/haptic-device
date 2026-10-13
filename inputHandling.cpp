#include "globals.h"
#include "inputHandling.h"
#include "utility.h"
#include <GLFW/glfw3.h>
#include <sys/stat.h>
#include <fstream>
#include <mutex>
#include <algorithm>

int just_unanchored = 0;
bool transparentAtoms = false;

// CHAI3D renders into the framebuffer, which is measured in pixels. GLFW cursor
// coordinates are in window coordinates, so derive the conversion from the
// actual framebuffer/window ratio. This is more reliable than content scale on
// macOS Retina displays, where the two can diverge depending on monitor/window
// state.
static void scaleCursorToPixels(double &a_x, double &a_y) {
  int windowWidth = 0;
  int windowHeight = 0;
  int framebufferWidth = 0;
  int framebufferHeight = 0;
  glfwGetWindowSize(window, &windowWidth, &windowHeight);
  glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
  if (windowWidth <= 0 || windowHeight <= 0) {
    return;
  }

  a_x *= static_cast<double>(framebufferWidth) / static_cast<double>(windowWidth);
  a_y *= static_cast<double>(framebufferHeight) / static_cast<double>(windowHeight);
}

static void ensureSelectionBoxLines() {
  for (int i = 0; i < 4; i++) {
    if (selectionBoxLines[i] == NULL) {
      selectionBoxLines[i] = new cShapeLine(cVector3d(0, 0, 0),
                                            cVector3d(0, 0, 0));
      selectionBoxLines[i]->setLineWidth(2);
      selectionBoxLines[i]->m_colorPointA.setYellowGold();
      selectionBoxLines[i]->m_colorPointB.setYellowGold();
      selectionBoxLines[i]->setShowEnabled(false);
      camera->m_frontLayer->addChild(selectionBoxLines[i]);
    }
  }
}

static void setSelectionBoxVisible(bool visible) {
  ensureSelectionBoxLines();
  for (int i = 0; i < 4; i++) {
    selectionBoxLines[i]->setShowEnabled(visible);
  }
}

static void updateSelectionBoxLines() {
  ensureSelectionBoxLines();
  const double left = std::min(selectionStartX, selectionCurrentX);
  const double right = std::max(selectionStartX, selectionCurrentX);
  const double bottom = std::min(selectionStartY, selectionCurrentY);
  const double top = std::max(selectionStartY, selectionCurrentY);

  selectionBoxLines[0]->m_pointA = cVector3d(left, bottom, 0);
  selectionBoxLines[0]->m_pointB = cVector3d(right, bottom, 0);
  selectionBoxLines[1]->m_pointA = cVector3d(right, bottom, 0);
  selectionBoxLines[1]->m_pointB = cVector3d(right, top, 0);
  selectionBoxLines[2]->m_pointA = cVector3d(right, top, 0);
  selectionBoxLines[2]->m_pointB = cVector3d(left, top, 0);
  selectionBoxLines[3]->m_pointA = cVector3d(left, top, 0);
  selectionBoxLines[3]->m_pointB = cVector3d(left, bottom, 0);
}

static bool projectAtomToScreen(Atom *atom, double &screenX, double &screenY) {
  cVector3d atomPos = atom->getLocalPos();
  cVector3d toAtom = atomPos - camera->getLocalPos();
  double depth = toAtom.dot(camera->getLookVector());
  if (depth <= 0.0) {
    return false;
  }

  double scale = (0.5 * height) / tan(0.5 * camera->getFieldViewAngleRad());
  screenX = (toAtom.dot(camera->getRightVector()) / depth) * scale + 0.5 * width;
  screenY = (toAtom.dot(camera->getUpVector()) / depth) * scale + 0.5 * height;
  return true;
}

static void selectAtomsInBox() {
  const double left = std::min(selectionStartX, selectionCurrentX);
  const double right = std::max(selectionStartX, selectionCurrentX);
  const double bottom = std::min(selectionStartY, selectionCurrentY);
  const double top = std::max(selectionStartY, selectionCurrentY);

  bool anySelected = false;
  for (Atom *atom : spheres) {
    double screenX = 0.0;
    double screenY = 0.0;
    bool inside = projectAtomToScreen(atom, screenX, screenY) &&
                  screenX >= left && screenX <= right &&
                  screenY >= bottom && screenY <= top;
    atom->setSelected(inside);
    anySelected = anySelected || inside;
  }

  if (anySelected) {
    for (Atom *atom : spheres) {
      atom->setCurrent(false);
    }
  }
}

void toggleFullscreen() {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  fullscreen = !fullscreen;
  GLFWmonitor *monitor = glfwGetPrimaryMonitor();
  const GLFWvidmode *mode = glfwGetVideoMode(monitor);
  if (fullscreen) {
    glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height,
                          mode->refreshRate);
    glfwSwapInterval(swapInterval);
  } else {
    int w = 2. * mode->height;
    int h = 1.5 * mode->height;
    int x = 1.5 * (mode->width - w);
    int y = 1.5 * (mode->height - h);
    glfwSetWindowMonitor(window, NULL, x, y, w, h, mode->refreshRate);
    glfwSwapInterval(swapInterval);
  }
}

void unanchorAtoms() {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  for (auto i{0}; i < spheres.size(); i++) {
    if (spheres[i]->isAnchor()) {
      spheres[i]->setAnchor(false);
    }
  }
  assert(just_unanchored = 5);
  just_unanchored = 0;
}

void saveScreenshot() {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  cImagePtr image = cImage::create();
  scope->setShowEnabled(false);
  camera->renderView(width, height);
  camera->copyImageBuffer(image);
  scope->setShowEnabled(true);
  int index = 0;
  string filename_stem = "lj" + to_string(spheres.size()) + "_";
  while (fileExists(filename_stem + to_string(index) + ".png")) {
    index++;
  }
  image->saveToFile(filename_stem + to_string(index) + ".png");
  screenshotCounter = 5000;
}

void saveConFile() {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  ofstream writeFile;
  string dir1 = "./log/";
  struct stat buffer;
  if (stat(dir1.c_str(), &buffer) != 0) {
    char cstr[dir1.size() + 1];
    strcpy(cstr, dir1.c_str());
    mkdir(cstr, 0777);
  }
  time_t now = time(0);
  tm *ltm = localtime(&now);
  int year = 1900 + ltm->tm_year;
  int month = 1 + ltm->tm_mon;
  int day = ltm->tm_mday;
  string date = to_string(month) + "-" + to_string(day) + "-" + to_string(year);
  string dir2 = dir1 + date + "/";
  if (stat(dir2.c_str(), &buffer) != 0) {
    char cstr[dir2.size() + 1];
    strcpy(cstr, dir2.c_str());
    mkdir(cstr, 0777);
  }
  int index = 0;
  while (fileExists(dir2 + "atoms" + to_string(index) + ".con")) {
    index++;
  }
  writeConCounter = 5000;
  writeToCon(dir2 + "atoms" + to_string(index) + ".con");
  cout << "LOGGED AT " + date + " atoms" + to_string(index) + ".con" << endl;
}

void anchorAtoms() {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  for (auto i{0}; i < spheres.size(); i++) {
    if (!spheres[i]->isAnchor() && !(spheres[i]->isCurrent())) {
      spheres[i]->setAnchor(true);
    }
  }
}

void moveCameraVertical(bool up) {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  int direction = up ? 1 : -1;
  camera->setSphericalPolarRad(camera->getSphericalPolarRad() +
                                (M_PI / 50) * direction);
  if (camera->getSphericalPolarRad() > 1000 * M_PI)
    camera->setSphericalPolarRad(camera->getSphericalPolarRad() - 1000 * M_PI);
  if (camera->getSphericalPolarRad() < -1000 * M_PI)
    camera->setSphericalPolarRad(camera->getSphericalPolarRad() + 1000 * M_PI);
  updateCameraLabel(camera_pos, camera);
}

void moveCameraHorizontal(bool right) {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  int direction = right ? 1 : -1;
  camera->setSphericalAzimuthRad(camera->getSphericalAzimuthRad() +
                                  (M_PI / 50) * direction);
  if (camera->getSphericalAzimuthRad() > 1000 * M_PI)
    camera->setSphericalAzimuthRad(camera->getSphericalAzimuthRad() - 1000 * M_PI);
  if (camera->getSphericalAzimuthRad() < -1000 * M_PI)
    camera->setSphericalAzimuthRad(camera->getSphericalAzimuthRad() + 1000 * M_PI);
  updateCameraLabel(camera_pos, camera);
}

void zoomCamera(bool zoomIn) {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  int direction = zoomIn ? 1 : -1;
  if ((direction == 1 && rho < 1) || (direction == -1 && rho > .15)) {
    camera->setSphericalRadius(camera->getSphericalRadius() + .01 * direction);
    rho = camera->getSphericalRadius();
    updateCameraLabel(camera_pos, camera);
  }
}

void resetCamera() {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  camera->setSphericalPolarRad(0);
  camera->setSphericalAzimuthRad(0);
  camera->setSphericalRadius(.35);
  rho = .35;
  updateCameraLabel(camera_pos, camera);
}

void toggleHelpPanel() {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  helpPanel->setShowPanel(!helpPanel->getShowPanel());
  helpHeader->setShowEnabled(helpPanel->getShowPanel());
  for (int i = 0; i < hotkeyKeys.size(); i++) {
    hotkeyKeys[i]->setShowEnabled(helpPanel->getShowPanel());
    hotkeyFunctions[i]->setShowEnabled(helpPanel->getShowPanel());
  }
}

void keyCallback(GLFWwindow *a_window, int a_key, int a_scancode, int a_action,
                 int a_mods) {
  if ((a_action != GLFW_PRESS) && (a_action != GLFW_REPEAT)) {
    return;
  } else if ((a_key == GLFW_KEY_ESCAPE) || (a_key == GLFW_KEY_Q)) {
    glfwSetWindowShouldClose(a_window, GLFW_TRUE);
  } else if (a_key == GLFW_KEY_F) {
    toggleFullscreen();
  } else if (a_key == GLFW_KEY_U) {
    // action - unanchor all key
    unanchorAllAtoms();
    assert(just_unanchored = 5);
    just_unanchored = 0;
  } else if (a_key == GLFW_KEY_S) {
    saveScreenshot();
  } else if (a_key == GLFW_KEY_SPACE) {
    freezeAtoms = !freezeAtoms;
  } else if (a_key == GLFW_KEY_1 && a_action == GLFW_PRESS) {  // toggle atom rendering
    renderAtoms = !renderAtoms;
  } else if (a_key == GLFW_KEY_2 && a_action == GLFW_PRESS) {  // toggle force vector rendering
    renderForceVectors = !renderForceVectors;
  } else if (a_key == GLFW_KEY_3 && a_action == GLFW_PRESS) {  // toggle bond rendering
    renderBonds = !renderBonds;
  } else if (a_key == GLFW_KEY_I) {  // move current atom up
    moveCurrentAtom(0, 1, 0);
  } else if (a_key == GLFW_KEY_K) {  // move current atom down
    moveCurrentAtom(0, -1, 0);
  } else if (a_key == GLFW_KEY_J) {  // move current atom left
    moveCurrentAtom(-1, 0, 0);
  } else if (a_key == GLFW_KEY_L) {  // move current atom right
    moveCurrentAtom(1, 0, 0);
  } else if (a_key == GLFW_KEY_O) {  // move current atom forward (away from camera)
    moveCurrentAtom(0, 0, 1);
  } else if (a_key == GLFW_KEY_P) {  // move current atom backward (toward camera)
    moveCurrentAtom(0, 0, -1);
  } else if (a_key == GLFW_KEY_C) {  // save atoms to con file
    std::lock_guard<std::recursive_mutex> lock(sceneMutex);
    ofstream writeFile;
    string dir1 = "./log/";
    struct stat buffer;
    if (stat(dir1.c_str(), &buffer) != 0) { // Check if log directory exists
      char cstr[dir1.size() + 1];
      strcpy(cstr, dir1.c_str());
      mkdir(cstr, 0777);
    }

    // Find local date
    time_t now = time(0);
    tm *ltm = localtime(&now);
    int year = 1900 + ltm -> tm_year;
    int month = 1 + ltm -> tm_mon;
    int day = ltm -> tm_mday;
    string date = to_string(month) + "-" + to_string(day) + "-" + to_string(year);
    string dir2 = dir1 + date + "/";
    if (stat(dir2.c_str(), &buffer) != 0) { // Check if date directory exists
      char cstr[dir2.size() + 1];
      strcpy(cstr, dir2.c_str());
      mkdir(cstr, 0777);
    }
    // Prevent overwriting .con files
    int index = 0;
    while (fileExists(dir2 + "atoms" + to_string(index) + ".con")) {
      index++;
    }
    writeConCounter = 5000;
    writeToCon(dir2 + "atoms" + to_string(index) + ".con");
    cout << "LOGGED AT " + date + " atoms" + to_string(index) + ".con" << endl;
  } else if (a_key == GLFW_KEY_A) {
    // anchor all atoms while maintaining control
    anchorAllAtoms();
  } else if (a_key == GLFW_KEY_UP || a_key == GLFW_KEY_DOWN) {
    moveCameraVertical(a_key == GLFW_KEY_DOWN);
  } else if (a_key == GLFW_KEY_RIGHT || a_key == GLFW_KEY_LEFT) {
    moveCameraHorizontal(a_key == GLFW_KEY_RIGHT);
  } else if (a_key == GLFW_KEY_LEFT_BRACKET || a_key == GLFW_KEY_RIGHT_BRACKET) {
    zoomCamera(a_key == GLFW_KEY_RIGHT_BRACKET);
  } else if (a_key == GLFW_KEY_R) {
    resetCamera();
  } else if ((a_key == GLFW_KEY_LEFT_CONTROL || a_key == GLFW_KEY_RIGHT_CONTROL) &&
             a_action == GLFW_PRESS) {
    toggleHelpPanel();
  } else if (a_key == GLFW_KEY_D) {
    std::lock_guard<std::recursive_mutex> lock(sceneMutex);
    showDebug = !showDebug;
  } else if (a_key == GLFW_KEY_T) {
    std::lock_guard<std::recursive_mutex> lock(sceneMutex);
    for (int i = 0; i < spheres.size(); i++) {
      spheres[i]->setLocalPos(initialPositions[i]);
      spheres[i]->setVelocity(0);
    }
  } else if (a_key == GLFW_KEY_F1) {
    if (!transparentAtoms) {
      for (int i = 0; i < spheres.size(); i++) {
        spheres[i]->setTransparencyLevel(0.0);
      }
    } else {
      for (int i = 0; i < spheres.size(); i++) {
        spheres[i]->setTransparencyLevel(1.0);
      }
    }
    transparentAtoms = !transparentAtoms;
  }
}

void mouseMotionCallback(GLFWwindow *a_window, double a_posX, double a_posY) {
    std::lock_guard<std::recursive_mutex> lock(sceneMutex);
    if (mouseState == MOUSE_BOX_SELECTION) {
        double posX = a_posX, posY = a_posY;
        scaleCursorToPixels(posX, posY);
        selectionCurrentX = posX;
        selectionCurrentY = height - posY;
        updateSelectionBoxLines();
        setSelectionBoxVisible(true);
    } else if ((selectedAtom != NULL) && (mouseState == MOUSE_SELECTION) &&
        (selectedAtom->isAnchor())) {
        // get the vector that goes from the camera to the selected point (mouse
        // click)
        cVector3d vCameraObject = selectedPoint - camera->getLocalPos();

        // get the vector that point in the direction of the camera. ("where the
        // camera is looking at")
        cVector3d vCameraLookAt = camera->getLookVector();

        // compute the angle between both vectors
        double angle = cAngle(vCameraObject, vCameraLookAt);

        // compute the distance between the camera and the plane that intersects the
        // object and which is parallel to the camera plane
        double distanceToObjectPlane = vCameraObject.length() * cos(angle);

        // cursor is in window points; scale to framebuffer pixels to match width/height
        double posX = a_posX, posY = a_posY;
        scaleCursorToPixels(posX, posY);

        // convert the pixel in mouse space into a relative position in the world
        double factor = (distanceToObjectPlane * tan(0.5 *
                        camera->getFieldViewAngleRad())) / (0.5 * height);
        double posRelX = factor * (posX - (0.5 * width));
        double posRelY = factor * ((height - posY) - (0.5 * height));

        // compute the new position in world coordinates
        cVector3d pos = camera->getLocalPos() +
        distanceToObjectPlane * camera->getLookVector() +
        posRelX * camera->getRightVector() +
        posRelY * camera->getUpVector();

        // compute position of object by taking in account offset
        cVector3d posObject = pos - selectedAtomOffset;

        // apply new position to object
        selectedAtom->setLocalPos(posObject);
    }
}

void mouseButtonCallback(GLFWwindow *a_window, int a_button, int a_action,
                         int a_mods) {
    std::lock_guard<std::recursive_mutex> lock(sceneMutex);
    // store mouse position
    double x, y;

    // detect for any collision between mouse and scene
    cCollisionRecorder recorder;
    cCollisionSettings settings;
    if (a_button == GLFW_MOUSE_BUTTON_LEFT && a_action == GLFW_PRESS) {
        glfwGetCursorPos(window, &x, &y);
        scaleCursorToPixels(x, y); // window points -> framebuffer pixels
        bool hit =
        camera->selectWorld(x, (height - y), width, height, recorder, settings);
        if (hit) {
            cGenericObject *selected = recorder.m_nearestCollision.m_object;
            selectedAtom = (Atom *)selected;
            if (a_mods & GLFW_MOD_SHIFT) {
                selectedAtom->setSelected(true);
                mouseState = MOUSE_IDLE;
                return;
            }
            selectedPoint = recorder.m_nearestCollision.m_globalPos;
            selectedAtomOffset =
            recorder.m_nearestCollision.m_globalPos - selectedAtom->getLocalPos();
            mouseState = MOUSE_SELECTION;
        } else {
            selectedAtom = NULL;
            selectionStartX = x;
            selectionStartY = height - y;
            selectionCurrentX = selectionStartX;
            selectionCurrentY = selectionStartY;
            updateSelectionBoxLines();
            setSelectionBoxVisible(true);
            mouseState = MOUSE_BOX_SELECTION;
        }
    } else if (a_button == GLFW_MOUSE_BUTTON_RIGHT && a_action == GLFW_PRESS) {
        glfwGetCursorPos(window, &x, &y);
        scaleCursorToPixels(x, y); // window points -> framebuffer pixels
        bool hit =
        camera->selectWorld(x, (height - y), width, height, recorder, settings);
        if (hit) {
            // retrieve Atom selected by mouse
            cGenericObject *selected = recorder.m_nearestCollision.m_object;
            selectedAtom = (Atom *)selected;

            // Toggle anchor status and color
            if (selectedAtom->isAnchor()) {
                selectedAtom->setAnchor(false);
            } else if (!selectedAtom->isCurrent()) {  // cannot set current to anchor
                selectedAtom->setAnchor(true);
            }
            mouseState = MOUSE_SELECTION;
        }
    } else if (a_button == GLFW_MOUSE_BUTTON_LEFT && a_action == GLFW_RELEASE &&
               mouseState == MOUSE_BOX_SELECTION) {
        glfwGetCursorPos(window, &x, &y);
        scaleCursorToPixels(x, y);
        selectionCurrentX = x;
        selectionCurrentY = height - y;
        selectAtomsInBox();
        setSelectionBoxVisible(false);
        mouseState = MOUSE_IDLE;
    } else {
        setSelectionBoxVisible(false);
        mouseState = MOUSE_IDLE;
    }
}

void anchorAllAtoms() {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  for (auto i{0}; i < spheres.size(); i++) {
    if (!spheres[i]->isAnchor() && !(spheres[i]->isCurrent())) {
      spheres[i]->setAnchor(true);
    }
  }
}

void unanchorAllAtoms() {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  for (auto i{0}; i < spheres.size(); i++) {
    if (spheres[i]->isAnchor()) {
      spheres[i]->setAnchor(false);
    }
  }
}
