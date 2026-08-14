#include "atom.h"
#include "chai3d.h"
#include "math.h"
#include <unordered_map>
#include <tuple>
#include <GLFW/glfw3.h>

// array of atom stringnames by atomic number
const string ATOM_STRINGS[119] = { "There is no atomic no. 0!",
        "H", "He", "Li", "Be", "B", "C", "N", "O", "F", "Ne", "Na", "Mg", "Al", "Si", "P", "S",
        "Cl", "Ar", "K", "Ca", "Sc", "Ti", "V", "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn", "Ga",
        "Ge", "As", "Se", "Br", "Kr", "Rb", "Sr", "Y", "Zr", "Nb", "Mo", "Tc", "Ru", "Rh", "Pd",
        "Ag", "Cd", "In", "Sn", "Sb", "Te", "I", "Xe", "Cs", "Ba", "La", "Ce", "Pr", "Nd", "Pm",
        "Sm", "Eu", "Gd", "Tb", "Dy", "Ho", "Er", "Tm", "Yb", "Lu", "Hf", "Ta", "W", "Re", "Os",
        "Ir", "Pt", "Au", "Hg", "Tl", "Pb", "Bi", "Po", "At", "Rn", "Fr", "Ra", "Ac", "Th", "Pa",    
        "U", "Np", "Pu", "Am", "Cm", "Bk", "Cf", "Es", "Fm", "Md", "No", "Lr", "Rf", "Db", "Sg",
        "Bh", "Hs", "Mt", "Ds", "Rg", "Cn", "Nh","Fl", "Mc", "Lv", "Ts", "Og"
};

// array of atom weights by atomic number
const double ATOM_WEIGHTS[119] = {
    0.0, 1.008, 4.003, 7.0, 9.012, 10.81, 12.011, 14.007, 15.999, 18.998, 20.18, 22.99, 24.305,
    26.982, 28.085, 30.974, 32.07, 35.45, 39.9, 39.098, 40.08, 44.956, 47.867, 50.942, 51.996,
    54.938, 55.84, 58.933, 58.693, 63.55, 65.4, 69.723, 72.63, 74.922, 78.97, 79.9, 83.8, 85.468, 
    87.62, 88.906, 91.22, 92.906, 95.95, 96.906, 101.1, 102.906, 106.42, 107.868, 112.41, 114.818,
    118.71, 121.76, 127.6, 126.905, 131.29, 132.905, 137.33, 138.906, 140.116, 140.908, 144.24,
    144.913, 150.4, 151.964, 157.25, 158.925, 162.5, 164.93, 167.26, 168.934, 173.05, 174.967,
    178.49, 180.948, 183.84, 186.207, 190.2, 192.22, 195.08, 196.967, 200.59, 204.383, 207, 208.98,
    208.982, 209.987, 222.018, 223.02, 226.025, 227.028, 232.038, 231.036, 238.029, 237.048,
    244.064, 243.061, 247.07, 247.07, 251.08, 252.083, 257.095, 258.098, 259.101, 266.12, 267.122, 
    268.126, 269.128, 270.133, 269.134, 277.154, 282.166, 282.169, 286.179, 286.182, 290.192,
    290.196, 293.205, 294.11, 295.216
};


// map of atom colors by atomic number, using the standard Jmol/CPK color
// scheme so elements read in from structure files (e.g. POSCAR) render with
// their conventional colors instead of falling back to the default magenta

const std::tuple<const GLfloat, const GLfloat, const GLfloat> ATOM_COLORS[110] = {
    {255, 20, 147}, // fallback color; Elements past 110 (Ds) are magenta
    {255, 255, 255},
    {217, 255, 255},
    {204, 128, 255},
    {194, 255, 0},
    {255, 181, 181},
    {144, 144, 144},
    {48, 80, 248},
    {255, 13, 13},
    {144, 224, 80},
    {179, 227, 245},
    {171, 92, 242},
    {138, 255, 0},
    {191, 166, 166},
    {240, 200, 160},
    {255, 128, 0},
    {255, 255, 48},
    {31, 240, 31},
    {128, 209, 227},
    {143, 64, 212},
    {61, 255, 0},
    {230, 230, 230},
    {191, 194, 199},
    {166, 166, 171},
    {138, 153, 199},
    {156, 122, 199},
    {224, 102, 51},
    {240, 144, 160},
    {80, 208, 80},
    {200, 128, 51},
    {125, 128, 176},
    {194, 143, 143},
    {102, 143, 143},
    {189, 128, 227},
    {255, 161, 0},
    {166, 41, 41},
    {92, 184, 209},
    {112, 46, 176},
    {0, 255, 0},
    {148, 255, 255},
    {148, 224, 224},
    {115, 194, 201},
    {84, 181, 181},
    {59, 158, 158},
    {36, 143, 143},
    {10, 125, 140},
    {0, 105, 133},
    {192, 192, 192},
    {255, 217, 143},
    {166, 117, 115},
    {102, 128, 128},
    {158, 99, 181},
    {212, 122, 0},
    {148, 0, 148},
    {66, 158, 176},
    {87, 23, 143},
    {0, 201, 0},
    {112, 212, 255},
    {255, 255, 199},
    {217, 255, 199},
    {199, 255, 199},
    {163, 255, 199},
    {143, 255, 199},
    {97, 255, 199},
    {69, 255, 199},
    {48, 255, 199},
    {31, 255, 199},
    {0, 255, 156},
    {0, 230, 117},
    {0, 212, 82},
    {0, 191, 56},
    {0, 171, 36},
    {77, 194, 255},
    {77, 166, 255},
    {33, 148, 214},
    {38, 125, 171},
    {38, 102, 150},
    {23, 84, 135},
    {208, 208, 224},
    {255, 209, 35},
    {184, 184, 208},
    {166, 84, 77},
    {87, 89, 97},
    {158, 79, 181},
    {171, 92, 0},
    {117, 79, 69},
    {66, 130, 150},
    {66, 0, 102},
    {0, 125, 0},
    {112, 171, 250},
    {0, 186, 255},
    {0, 161, 255},
    {0, 143, 255},
    {0, 128, 255},
    {0, 107, 255},
    {84, 92, 242},
    {120, 92, 227},
    {138, 79, 227},
    {161, 54, 212},
    {179, 31, 212},
    {179, 31, 186},
    {179, 13, 166},
    {189, 13, 135},
    {199, 0, 102},
    {204, 0, 89},
    {209, 0, 79},
    {217, 0, 69},
    {224, 0, 56},
    {230, 0, 46},
    {235, 0, 38},
};

/**
 * @brief Refreshes the material of the atom, changing the atom's color to red, blue, or
 * black, respectively based on if the atom is selected/curent, anchored, or repeating.
 * Otherwise, the atom reverts to its JMol coloring.
 */
void Atom::refreshMaterial(chai3d::cShapeSphere *sphere) {
    sphere->m_material->m_emission.set(0.0f, 0.0f, 0.0f, 1.0f);
    if (selected || current) {
        sphere->m_material->setRed();
    } else if (anchor) {
        sphere->m_material->setBlue();
    } else if (repeating) {
        sphere->m_material->setBlack();
    } else {
        sphere->m_material->setColor(color);
    }
}

void Atom::setPeriodics(int x, int y, int z) {
    std::cout << "Updating periodics..." << std::endl;
    for (auto& plane : periodics) {
        for (auto& row : plane){
            for (chai3d::cShapeSphere* s : row) {
                getParent()->removeChild(s);  // detach BEFORE delete
                delete s;                           // frees GL display list (needs GL ctx)
            }
        }
    }
    periodics.resize(x);
    for (int i = 0; i < x; i++) {
        periodics[i].resize(y);
        for (int j = 0; j < y; j++) {
            periodics[i][j].resize(z);
            for (int k = 0; k < z; k++) {
                periodics[i][j][k] = copy();
                refreshMaterial(periodics[i][j][k]);
                getParent()->addChild(periodics[i][j][k]);
            }
        }
    } 
}

Atom::Atom(double radius, int atomicNum, chai3d::cWorld *world, cTexture2dPtr texture) : chai3d::cShapeSphere(radius) {
    anchor = false;
    current = false;
    repeating = false;
    selected = false;
    velVector = new chai3d::cShapeLine(chai3d::cVector3d(0, 0, 0), chai3d::cVector3d(0, 0, 0));
    force.zero();
    prevForce.zero();
    prevPos = getLocalPos();

    atomicNumber = atomicNum;

    std::tuple<GLfloat, GLfloat, GLfloat> colorTuple;
    if (atomicNum <= 109) {
        colorTuple = ATOM_COLORS[atomicNum];
    } else {
        colorTuple = ATOM_COLORS[0];
    }

    color.set(get<0>(colorTuple)/255, get<1>(colorTuple)/255, get<2>(colorTuple)/255);
    setUseCulling(true);
    refreshMaterial(this);
    setTexture(texture);
    m_texture->setSphericalMappingEnabled(true);
    setUseTexture(true);
    world->addChild(this);
}

const std::vector<std::vector<std::vector<chai3d::cShapeSphere*>>>& Atom::getPeriodics() const {
    return periodics;
}

bool Atom::isAnchor() const { 
    return anchor; 
}

void Atom::setAnchor(bool newAnchor) {
    if (newAnchor) {
        current = false;
    }
    anchor = newAnchor;
    refreshMaterial(this);
}

bool Atom::isCurrent() const { 
    return current; 
}

void Atom::setCurrent(bool newCurrent) {
    if (newCurrent) {
        anchor = false;  // cannot be both anchor and current
    }
    current = newCurrent;
    refreshMaterial(this);
}

bool Atom::isRepeating() const { 
    return repeating; 
}

void Atom::setRepeating(bool newRepeat) {
    if (newRepeat) {
        anchor = false; // cannot be both anchor and repeating
    }
    repeating = newRepeat;
    refreshMaterial(this);
}

bool Atom::isSelected() const { 
    return selected; 
}

void Atom::setSelected(bool newSelected) {
    selected = newSelected;
    refreshMaterial(this);
}

chai3d::cVector3d Atom::getVelocity() const { 
    return velocity; 
}

void Atom::setVelocity(chai3d::cVector3d newVel) { 
    velocity = newVel; 
}

chai3d::cVector3d Atom::getForce() const { 
    return force; 
}

void Atom::setForce(chai3d::cVector3d newForce) {
    prevForce = force;
    force = newForce;  // Add exception for if controlled atom is in the same
    // location as the anchored atom
}

cVector3d Atom::getPrevForce() const {
    return prevForce;
}

cShapeLine* Atom::getVelVector() const { 
    return velVector; 
}

void Atom::setVelVector(cShapeLine* newVelVector) { 
    velVector = newVelVector;
}

void Atom::updateForceVector() {
    cVector3d forceDir = this->getForce();
    forceDir.normalize();
    velVector->m_pointA = getLocalPos() + forceDir * getRadius();
    velVector->m_colorPointA.setBlack();
    velVector->m_colorPointB.setBlack();
    const double FORCE_VECTOR_WIDTH = 3.0;
    velVector->setLineWidth(FORCE_VECTOR_WIDTH);
    velVector->m_pointB = velVector->m_pointA + getForce() * .005;
}

void Atom::setColor(cColorf color) {
    if (!selected) {
        m_material->setColor(color);
        m_material->m_emission.set(0.0f, 0.0f, 0.0f, 1.0f);
    }
}

int Atom::getAtomicNumber() const { 
    return atomicNumber;
}

void Atom::setAtomicNumber(int num) {
    atomicNumber = num;
}

string Atom::getElement() const {
    return ATOM_STRINGS[atomicNumber];
}

double Atom::getMass() const {
    return ATOM_WEIGHTS[atomicNumber];
}

void Atom::addBufferedPos(chai3d::cVector3d pos) {
    prevPos = getLatestPos();
    positionBuffer.push(pos);
}

chai3d::cVector3d Atom::nextPos() {
    cVector3d result = positionBuffer.front();
    positionBuffer.pop();
    return result;
}

chai3d::cVector3d Atom::getPrevPos() const {
    return prevPos;
}

bool Atom::hasNextPos() const {
    return !positionBuffer.empty();
}

chai3d::cVector3d Atom::getLatestPos() const {
    if (positionBuffer.empty()) {
        return getLocalPos();
    }
    return positionBuffer.back();
}