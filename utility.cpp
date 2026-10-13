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

static std::string getExecutableDir() {
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

static std::vector<std::string> getGlobalMinimaCandidates() {
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
                      chai3d::cStr(rho * sin(camera->getSphericalPolarRad()) *
                           cos(camera->getSphericalAzimuthRad())) +
                      ", " +
                      chai3d::cStr(rho * sin(camera->getSphericalPolarRad()) *
                           sin(camera->getSphericalAzimuthRad())) +
                      ", " + chai3d::cStr(rho * cos(camera->getSphericalPolarRad())) +
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
  << spheres.size() << endl
  << "1.007940" << endl
  << "H" << endl
  << "Coordinates of Component 1" << endl;
  writeFile.precision(7);
  writeFile << centerCoords[0] << " " << centerCoords[1] << " "
  << centerCoords[2] << " 0 0" << endl;
  for (int i = 1; i < spheres.size(); i++) {
    chai3d::cVector3d pos = spheres[i]->getLocalPos();
    writeFile << (pos.x() / 0.02) + centerCoords[0] << " "
    << (pos.y() / 0.02) + centerCoords[1] << " "
    << (pos.z() / 0.02) + centerCoords[2] << " 0 " << i << endl;
  }
  writeFile.close();
}