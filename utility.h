#include <string>
#include <vector>

#include "chai3d.h"

//checks if given file exists
bool fileExists(const std::string &name);

// Reads in global minimum from global_minima.txt
double getGlobalMinima(int cluster_size);

// checks if char array represents a number
bool isNumber(char number[]);

//add hotkey help labels
void addHotkeyLabel(std::string key, std::string text);

// add a label to the world with default black text
void addLabel(chai3d::cLabel *&label);

/**
 * @brief Updates the text label that displays the camera position.
 */
void updateCameraLabel(chai3d::cLabel *&camera_pos, chai3d::cCamera *&camera);

/**
 * @brief Writes the current atom configuration to a .con file.
 */
void writeToCon(std::string fileName);

std::string getExecutableDir();
template <typename Loader> bool loadChaiResource(Loader loader, const string &relativePath) {
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
  for (const string &root : roots) {
    const string candidate = root + relativePath;
    if (loader(candidate.c_str())) {
      return true;
    }
  }
  return false;
}

bool isFiniteVector(const chai3d::cVector3d &value);

chai3d::cVector3d clampVectorMagnitude(const chai3d::cVector3d &value, const double maxMagnitude);

/// @brief Draws a rectangle as requested.
/// @param x X-pos of the top-left(?) of the rectangle
/// @param y Y-pos of the top-left(?) of the rectangle
/// @param w Width of the rectangle
/// @param h Height of the rectangle
/// @param color Color of the rectangle
void drawRect(double x, double y, double w, double h, chai3d::cColorf color);

/// @brief Draws a circle as requested.
/// @param x X-pos of the center of the circle
/// @param y Y-pos of the center of the circle
/// @param radius Radius of the circle
/// @param color Color of the circle
void drawCircle(double cx, double cy, double radius, chai3d::cColorf color);

/// @brief Draws a pill (stadium) as requested
/// @param xStart First endpoint of the pill
/// @param xEnd Second endpoint of the pill
/// @param y Y-pos of the pill
/// @param height Height of the pill
/// @param color Color of the pill
void drawPill(double xStart, double xEnd, double y, double halfHeight, chai3d::cColorf color);

void framebufferSizeCallback(GLFWwindow *a_window, int a_width, int a_height);
cVector3d scaledToRadius(const cVector3d &position, double radius);

/**
 * @brief Adds a new label to the scene using the default style.
 */
void addDebugLabel(std::string text);