#ifndef POTENTIALS_H
#define POTENTIALS_H

#include <array>
#include <string>
#include <vector>

#include "atom.h"

struct AseStructureData {
    std::vector<std::array<double, 3>> positions;
    std::vector<int> atomicNumbers;
    std::array<double, 9> cell = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    std::array<int, 3> pbc = {0, 0, 0};
    std::vector<double> radii;
  };

AseStructureData loadAseStructure(const std::string& filename,
                                  const std::array<int, 3>& repeat = {1, 1, 1});

////// Base class for all calculators
class Calculator {
  public:
    virtual ~Calculator() = default;
    // Note: any other values used in this function should be private variables
    // For good design, anything that is expected to change frequently should be included in the function call. In our case, that is only positions.
    // You could get a nasty unreported error by relying too much on these private parameters
    virtual std::vector<std::vector<double>> getFandU(std::vector<Atom*>& spheres) = 0;
    virtual void setTemperature(double temperature) {}
};

////// Calculator for the morse potential
class morseCalculator:public Calculator {
  double rho0;
  double r0;
  double epsilon;
  double forceDamping;
  double distanceScale;
  public:
    morseCalculator(double rho=6.0, double r=1.0, double e=1.0, double fd=1.0, double ds=.02) {
      rho0 = rho;
      r0 = r;
      epsilon = e;
      forceDamping = fd;
      distanceScale = ds;
    }
    vector<vector<double>> getFandU(vector<Atom*>& spheres);
    double getMorseEnergy(double distance);
    double getMorseForce(double distance);
};

////// Calculator for the LJ potential
class ljCalculator:public Calculator {
  double sigma;
  double epsilon;
  double distanceScale;
  public:
    ljCalculator(double s=1.0, double e=1.0, double ds=.02) {
      sigma = s;
      epsilon = e;
      distanceScale = ds;
    }
    vector<vector<double>> getFandU(vector<Atom*>& spheres);
    double getLennardJonesEnergy(double distance);
    double getLennardJonesForce(double distance);
};

////// Calculator for ASE
class aseCalculator:public Calculator {
  std::string calculatorModule;
  std::string calculatorClass;
  std::string calculatorKwargs;
  std::array<double, 9> cell;
  std::array<int, 3> pbc;
  double temperature = 0.0;
  public:
    explicit aseCalculator(const std::string& cName,
                           const std::array<double, 9>& cell,
                           const std::array<int, 3>& pbc);
    std::vector<std::vector<double>> getFandU(std::vector<Atom*>& spheres);
    void setTemperature(double temperature) override;
};

#endif
