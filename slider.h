#include <GLFW/glfw3.h>
#include <string>

/**
 * @brief Initializes the slider window for the simulation
 * @param mainWindow the main window of the simulation
 * @return the slider window as a GLFW window pointer
 */
GLFWwindow* initializeSliderWindow(GLFWwindow* mainWindow);

/**
 * @brief Renders/re-renders the slider window. Closes the the window if it is supposed to be
 *        closed.
 * @param mainWindow the main window of the simulation
 * @param sliderWindow the slider window of the simulation. This window will be
 *                     rendered/re-rendered
 */
void renderSliderWindow(GLFWwindow* mainWindow, GLFWwindow*& sliderWindow);

/**
 * @brief Gets the value of a slider
 * @param id the name of the slider
 * @param fallback a fallback value to use if the slider is not found
 * @return the value of the indicated slider
 */
double getSliderVal(const std::string &id, double fallback);