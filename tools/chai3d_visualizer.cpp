// Minimal CHAI3D/GLFW visualizer for the custom 1-DOF capstan device -
// no atoms, no simulation, just a cursor sphere tracking shaft position
// against a fixed reference track so the motion is visible. Adapted from
// chai3d's templates/GLFW/application-GLFW/application-GLFW.cpp, with the
// device swapped from cHapticDeviceHandler's auto-detect to CustomHapticDevice.
//
// Usage: chai3d_visualizer /dev/cu.usbmodemXXXX

#include "chai3d.h"
#include <GLFW/glfw3.h>

#include "../customHapticDevice.h"

using namespace chai3d;
using namespace std;

cWorld *world;
cCamera *camera;
cViewport *viewport = nullptr;
cDirectionalLight *light;
cGenericHapticDevicePtr hapticDevice;
cLabel *labelRates;
cShapeSphere *cursor;
cShapeLine *track;

bool simulationRunning = false;
bool simulationFinished = true;
cFrequencyCounter freqCounterGraphics;
cFrequencyCounter freqCounterHaptics;
cThread *hapticsThread;
double lastPositionX = 0.0; // written by the haptics thread, read by graphics -
                             // a torn read is harmless for this display-only value

GLFWwindow *window = nullptr;
int windowW = 0, windowH = 0;
int framebufferW = 0, framebufferH = 0;
int swapInterval = 1;

void onWindowSizeCallback(GLFWwindow *a_window, int a_width, int a_height) {
  windowW = a_width;
  windowH = a_height;
}

void onFrameBufferSizeCallback(GLFWwindow *a_window, int a_width, int a_height) {
  framebufferW = a_width;
  framebufferH = a_height;
}

void onWindowContentScaleCallback(GLFWwindow *a_window, float a_xscale, float a_yscale) {
  viewport->setContentScale(a_xscale, a_yscale);
}

void onErrorCallback(int a_error, const char *a_description) {
  cout << "Error: " << a_description << endl;
}

void onKeyCallback(GLFWwindow *a_window, int a_key, int a_scancode, int a_action, int a_mods) {
  if (a_action != GLFW_PRESS) return;
  if ((a_key == GLFW_KEY_ESCAPE) || (a_key == GLFW_KEY_Q)) {
    glfwSetWindowShouldClose(a_window, GLFW_TRUE);
  }
}

void renderHaptics(void) {
  simulationRunning = true;
  simulationFinished = false;

  while (simulationRunning) {
    cVector3d position;
    hapticDevice->getPosition(position);
    cursor->setLocalPos(position);
    lastPositionX = position.x();

    // Pure visualization, no force feedback - and critically, do NOT send a
    // CommandFrame here. This loop runs unthrottled (can exceed 1M Hz), and
    // sendTorque() writes a CommandFrame over the same USB-serial link the
    // Arduino uses to report state; flooding it at that rate was starving/
    // corrupting the state stream, which looked like the sensor being frozen.

    freqCounterHaptics.signal(1);
  }

  simulationFinished = true;
}

void renderGraphics(void) {
  int displayW = viewport->getDisplayWidth();
  int displayH = viewport->getDisplayHeight();

  labelRates->setText(cStr(freqCounterGraphics.getFrequency(), 0) + " Hz / " +
                       cStr(freqCounterHaptics.getFrequency(), 0) + " Hz   x=" +
                       cStr(lastPositionX, 4) + " m");
  labelRates->setLocalPos((int)(0.5 * (displayW - labelRates->getWidth())), 15);

  world->updateShadowMaps(false, false);
  viewport->renderView(framebufferW, framebufferH);
  glFinish();

  glfwSwapBuffers(window);
  freqCounterGraphics.signal(1);
}

void close(void) {
  simulationRunning = false;
  while (!simulationFinished) {
    cSleepMs(100);
  }
  hapticDevice->close();
  delete hapticsThread;
  delete world;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <serial-device>\n", argv[0]);
    return 1;
  }

  if (!glfwInit()) {
    cout << "failed initialization" << endl;
    return 1;
  }
  glfwSetErrorCallback(onErrorCallback);

  const GLFWvidmode *mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
  windowW = 0.6 * mode->height;
  windowH = 0.4 * mode->height;
  int x = 0.5 * (mode->width - windowW);
  int y = 0.5 * (mode->height - windowH);

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
  glfwWindowHint(GLFW_SAMPLES, 4);
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

  window = glfwCreateWindow(windowW, windowH, "Capstan Device Visualizer", NULL, NULL);
  if (!window) {
    cout << "failed to create window" << endl;
    glfwTerminate();
    return 1;
  }

  glfwSetKeyCallback(window, onKeyCallback);
  glfwSetWindowSizeCallback(window, onWindowSizeCallback);
  glfwSetFramebufferSizeCallback(window, onFrameBufferSizeCallback);
  glfwSetWindowContentScaleCallback(window, onWindowContentScaleCallback);
  glfwGetFramebufferSize(window, &framebufferW, &framebufferH);
  glfwSetWindowPos(window, x, y);
  glfwMakeContextCurrent(window);
  glfwSwapInterval(swapInterval);

#ifdef GLEW_VERSION
  if (glewInit() != GLEW_OK) {
    cout << "failed to initialize GLEW library" << endl;
    glfwTerminate();
    return 1;
  }
#endif

  world = new cWorld();
  world->m_backgroundColor.setBlack();

  camera = new cCamera(world);
  world->addChild(camera);
  // Looking down the Y axis so X-axis shaft motion reads as clear left/right
  // screen motion, rather than toward/away from the camera.
  camera->set(cVector3d(0.0, -0.5, 0.1), cVector3d(0.0, 0.0, 0.0), cVector3d(0.0, 0.0, 1.0));
  camera->setClippingPlanes(0.01, 10.0);

  light = new cDirectionalLight(world);
  world->addChild(light);
  light->setEnabled(true);
  light->setDir(0.0, 1.0, -0.5);

  // Fixed reference track along X so the cursor's motion has something to
  // move against - metersPerRadian in customHapticDevice.h maps about
  // +/-5 rad of shaft rotation onto this +/-0.1m span. shaft_angle
  // accumulates without wrapping, so sustained cranking easily exceeds this
  // and pushes the cursor off-track - the on-screen x= readout (not just the
  // ball position) is the reliable way to confirm motion is being tracked.
  track = new cShapeLine(cVector3d(-0.1, 0.0, 0.0), cVector3d(0.1, 0.0, 0.0));
  track->m_colorPointA.setGrayLevel(0.4f);
  track->m_colorPointB.setGrayLevel(0.4f);
  track->setLineWidth(2);
  world->addChild(track);

  cursor = new cShapeSphere(0.01);
  cursor->m_material->setRedFireBrick();
  world->addChild(cursor);

  hapticDevice = std::make_shared<CustomHapticDevice>(argv[1]);
  if (!hapticDevice->open()) {
    cout << "failed to open device on " << argv[1] << endl;
    glfwTerminate();
    return 1;
  }
  hapticDevice->calibrate();

  cFontPtr font = NEW_CFONT_CALIBRI_20();
  labelRates = new cLabel(font);
  labelRates->m_fontColor.setWhite();
  camera->m_frontLayer->addChild(labelRates);

  float contentScaleW, contentScaleH;
  glfwGetWindowContentScale(window, &contentScaleW, &contentScaleH);
  viewport = new cViewport(camera, contentScaleW, contentScaleH);

  hapticsThread = new cThread();
  hapticsThread->start(renderHaptics, CTHREAD_PRIORITY_HAPTICS);

  atexit(close);

  while (!glfwWindowShouldClose(window)) {
    renderGraphics();
    glfwPollEvents();
  }

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
