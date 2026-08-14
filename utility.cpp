#include "globals.h"
#include "utility.h"
#include <cctype>
#include <sys/stat.h>
#include <sys/types.h>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <unistd.h>
#include <vector>

// a font for rendering text
chai3d::cFontPtr font = chai3d::NEW_CFONT_CALIBRI_20();

// font for help screen
chai3d::cFontPtr helpFont = chai3d::NEW_CFONT_CALIBRI_32();

// check if file already exists in directory
bool fileExists(const std::string &name) {
  struct stat buffer;
  return (stat(name.c_str(), &buffer) == 0);
}

std::string getExecutableDir() {
  char buffer[4096];
  ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (length <= 0) {
    return "";
  }

  buffer[length] = '\0';
  std::string executablePath(buffer);
  size_t separator = executablePath.find_last_of('/');
  if (separator == std::string::npos) {
    return "";
  }

  return executablePath.substr(0, separator);
}

 std::vector<std::string> getGlobalMinimaCandidates() {
  std::vector<std::string> candidates;
  std::string executableDir = getExecutableDir();

  if (!executableDir.empty()) {
    candidates.push_back(executableDir + "/../resources/data/global_minima.txt");
    candidates.push_back(executableDir + "/resources/data/global_minima.txt");
  }

  candidates.push_back("../resources/data/global_minima.txt");
  candidates.push_back("./resources/data/global_minima.txt");
  candidates.push_back("./bin/resources/data/global_minima.txt");
  candidates.push_back("../bin/resources/data/global_minima.txt");

  return candidates;
}

// read in global minimum by cluster size
double getGlobalMinima(int cluster_size) {
  std::string file_name = "global_minima.txt";
  std::ifstream infile;
  std::string resolvedPath;

  for (const std::string& candidate : getGlobalMinimaCandidates()) {
    infile.open(candidate);
    if (infile) {
      resolvedPath = candidate;
      break;
    }
    infile.clear();
  }

  if (!infile) {
    std::cerr << "Could not open \"" + file_name + "\" for reading" << std::endl;
    std::cerr << "Looked in these locations:" << std::endl;
    for (const std::string& candidate : getGlobalMinimaCandidates()) {
      std::cerr << "  " << candidate << std::endl;
    }
    exit(1);
  } else if ((cluster_size < 2) || (cluster_size > 150)) {
    std::cout << "WARNING: \"" + file_name +
                "\" doesn't have data for clusters of this size yet."
         << std::endl;
    std::cout << "The graph may not be accurate." << std::endl;
    return 0;
  }

  int cluster_size_file;
  double minimum;
  while (infile >> cluster_size_file >> minimum) {
    if (cluster_size_file == cluster_size) {
      break;
    }
  }
  return minimum;
}

// check if char array represents a number
bool isNumber(char number[]) {
  for (int i = 0; number[i] != 0; i++) {
    if (!isdigit(number[i])) return false;
  }
  return true;
}

// add help panel labels
void addHotkeyLabel(std::string keys, std::string function) {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  chai3d::cLabel *tempKeyLabel = new chai3d::cLabel(helpFont);
  chai3d::cLabel *tempFuncLabel = new chai3d::cLabel(helpFont);
  tempKeyLabel->m_fontColor.setBlack();
  tempFuncLabel->m_fontColor.setBlack();
  tempKeyLabel->setText(keys);
  tempFuncLabel->setText(function);
  tempKeyLabel->setShowPanel(false);
  tempFuncLabel->setShowPanel(false);
  tempKeyLabel->setShowEnabled(false);
  tempFuncLabel->setShowEnabled(false);
  camera->m_frontLayer->addChild(tempKeyLabel);
  camera->m_frontLayer->addChild(tempFuncLabel);
  hotkeyKeys.push_back(tempKeyLabel);
  hotkeyFunctions.push_back(tempFuncLabel);
}

// add debug labels
void addDebugLabel(std::string text) {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  chai3d::cLabel *label = new chai3d::cLabel(font);
  label->m_fontColor.setBlack();
  label->setText(text);
  label->setShowEnabled(false);
  camera->m_frontLayer->addChild(label);
  debugLabels.push_back(label);
}

// add status labels
void addLabel(chai3d::cLabel *&label) {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  label = new chai3d::cLabel(font);
  label->m_fontColor.setBlack();
  camera->m_frontLayer->addChild(label);
}

// update camera label
void updateCameraLabel(chai3d::cLabel *&camera_pos, chai3d::cCamera *&camera) {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  camera_pos->setText("Camera located at: (" +
                      chai3d::cStr(CAMERA_RADIUS * sin(camera->getSphericalPolarRad()) *
                           cos(camera->getSphericalAzimuthRad())) +
                      ", " +
                      chai3d::cStr(CAMERA_RADIUS * sin(camera->getSphericalPolarRad()) *
                           sin(camera->getSphericalAzimuthRad())) +
                      ", " + chai3d::cStr(CAMERA_RADIUS * cos(camera->getSphericalPolarRad())) +
                      ")");
}

void writeToCon(std::string fileName) {
  std::lock_guard<std::recursive_mutex> lock(sceneMutex);
  ofstream writeFile;
  writeFile.open(fileName);
  writeFile << "Generated by haptic device" << endl << endl;
  writeFile << "100.000000   100.000000   100.000000" << endl
  << "90.000000    90.000000    90.000000" << endl
  << endl
  << endl;
  writeFile << "1" << endl
  << atoms.size() << endl
  << "1.007940" << endl
  << "H" << endl
  << "Coordinates of Component 1" << endl;
  writeFile.precision(7);
  writeFile << centerCoords[0] << " " << centerCoords[1] << " "
  << centerCoords[2] << " 0 0" << endl;
  for (int i = 1; i < atoms.size(); i++) {
    chai3d::cVector3d pos = atoms[i]->getLocalPos();
    writeFile << (pos.x() / DIST_SCALE) + centerCoords[0] << " "
    << (pos.y() / DIST_SCALE) + centerCoords[1] << " "
    << (pos.z() / DIST_SCALE) + centerCoords[2] << " 0 " << i << endl;
  }
  writeFile.close();
}

bool isFiniteVector(const chai3d::cVector3d &v) {
  return std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z());
}

// THIS CALCULATES THE MAGNTIUDES OF THE VECTORS  
chai3d::cVector3d clampVectorMagnitude(const chai3d::cVector3d &v, const double maxMagnitude) {
  if (!isFiniteVector(v)) {
    return chai3d::cVector3d(0.0, 0.0, 0.0);
  }
  const double length = v.length();
  if (length > maxMagnitude && length > 0.0) {
    return maxMagnitude * chai3d::cNormalize(v);
  }
  return v;
}

/// @brief Draws a rectangle as requested.
/// @param x X-pos of the top-left(?) of the rectangle
/// @param y Y-pos of the top-left(?) of the rectangle
/// @param w Width of the rectangle
/// @param h Height of the rectangle
/// @param color Color of the rectangle
void drawRect(double x, double y, double w, double h, chai3d::cColorf color) {
  glColor3f(color.getR(), color.getG(), color.getB());
  glBegin(GL_QUADS);
  glVertex2d(x, y);
  glVertex2d(x + w, y);
  glVertex2d(x + w, y + h);
  glVertex2d(x, y + h);
  glEnd();
}

/// @brief Draws a circle as requested.
/// @param x X-pos of the center of the circle
/// @param y Y-pos of the center of the circle
/// @param radius Radius of the circle
/// @param color Color of the circle
void drawCircle(double cx, double cy, double radius, chai3d::cColorf color) {
  const int segments = 24;
  glColor3f(color.getR(), color.getG(), color.getB());
  glBegin(GL_TRIANGLE_FAN);
  glVertex2d(cx, cy);
  for (int i = 0; i <= segments; i++) {
    const double angle = 2.0 * M_PI * i / segments;
    glVertex2d(cx + radius * cos(angle), cy + radius * sin(angle));
  }
  glEnd();
}

/// @brief Draws a pill (stadium) as requested
/// @param xStart First endpoint of the pill
/// @param xEnd Second endpoint of the pill
/// @param y Y-pos of the pill
/// @param height Height of the pill
/// @param color Color of the pill
void drawPill(double xStart, double xEnd, double y, double height, chai3d::cColorf color) {
  if (xEnd < xStart) {
    xEnd = xStart;
  }
  drawRect(xStart, y - height / 2.0, xEnd - xStart, height, color);
  drawCircle(xStart, y, height / 2.0, color);
  drawCircle(xEnd, y, height / 2.0, color);
}

/**
 * @brief Callback triggered when the framebuffer size changes.
 */
void framebufferSizeCallback(GLFWwindow *a_window, int a_width, int a_height) {
  // update framebuffer (pixel) size used for rendering
  width = a_width;
  height = a_height;
}

chai3d::cVector3d scaledToRadius(const cVector3d &position, double radius) {
  double length = position.length();
  if (length <= 1e-12) {
    return cVector3d(0.0, 0.0, radius);
  }
  return position *(radius / length);
}