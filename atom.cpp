#include "atom.h"
#include "chai3d.h"
#include "math.h"
#include <unordered_map>
#include <tuple>
#include <GLFW/glfw3.h>

using namespace std;
using namespace chai3d;

// map of atom stringnames by atomic number
std::unordered_map<int, std::string> atomStringNames({
    {1, "H"},
    {2, "He"},
    {3, "Li"},
    {4, "Be"},
    {5, "B"},
    {6, "C"},
    {7, "N"},
    {8, "O"},
    {9, "F"},
    {10, "Ne"},
    {11, "Na"},
    {12, "Mg"},
    {13, "Al"},
    {14, "Si"},
    {15, "P"},
    {16, "S"},
    {17, "Cl"},
    {18, "Ar"},
    {19, "K"},
    {20, "Ca"},
    {21, "Sc"},
    {22, "Ti"},
    {23, "V"},
    {24, "Cr"},
    {25, "Mn"},
    {26, "Fe"},
    {27, "Co"},
    {28, "Ni"},
    {29, "Cu"},
    {30, "Zn"},
    {31, "Ga"},
    {32, "Ge"},
    {33, "As"},
    {34, "Se"},
    {35, "Br"},
    {36, "Kr"},
    {37, "Rb"},
    {38, "Sr"},
    {39, "Y"},
    {40, "Zr"},
    {41, "Nb"},
    {42, "Mo"},
    {43, "Tc"},
    {44, "Ru"},
    {45, "Rh"},
    {46, "Pd"},
    {47, "Ag"},
    {48, "Cd"},
    {49, "In"},
    {50, "Sn"},
    {51, "Sb"},
    {52, "Te"},
    {53, "I"},
    {54, "Xe"},
    {55, "Cs"},
    {56, "Ba"},
    {57, "La"},
    {58, "Ce"},
    {59, "Pr"},
    {60, "Nd"},
    {61, "Pm"},
    {62, "Sm"},
    {63, "Eu"},
    {64, "Gd"},
    {65, "Tb"},
    {66, "Dy"},
    {67, "Ho"},
    {68, "Er"},
    {69, "Tm"},
    {70, "Yb"},
    {71, "Lu"},
    {72, "Hf"},
    {73, "Ta"},
    {74, "W"},
    {75, "Re"},
    {76, "Os"},
    {77, "Ir"},
    {78, "Pt"},
    {79, "Au"},
    {80, "Hg"},
    {81, "Tl"},
    {82, "Pb"},
    {83, "Bi"},
    {84, "Po"},
    {85, "At"},
    {86, "Rn"},
    {87, "Fr"},
    {88, "Ra"},
    {89, "Ac"},
    {90, "Th"},
    {91, "Pa"},
    {92, "U"},
    {93, "Np"},
    {94, "Pu"},
    {95, "Am"},
    {96, "Cm"},
    {97, "Bk"},
    {98, "Cf"},
    {99, "Es"},
    {100, "Fm"},
    {101, "Md"},
    {102, "No"},
    {103, "Lr"},
    {104, "Rf"},
    {105, "Db"},
    {106, "Sg"},
    {107, "Bh"},
    {108, "Hs"},
    {109, "Mt"},
    {110, "Ds"}, 
    {111, "Rg"}, 
    {112, "Cn"}, 
    {113, "Nh"},
    {114, "Fl"},
    {115, "Mc"},
    {116, "Lv"},
    {117, "Ts"},
    {118, "Og"}
});

// map of atom weights by atomic number
std::unordered_map<int, double> atomWeights({
    {1, 1.007},
    {2, 4.002},
    {3, 6.941},
    {4, 9.012},
    {5, 10.811},
    {6, 12.011},
    {7, 14.007},
    {8, 15.999},
    {9, 18.998},
    {10, 20.18},
    {11, 22.99},
    {12, 24.305},
    {13, 26.982},
    {14, 28.086},
    {15, 30.974},
    {16, 32.065},
    {17, 35.453},
    {18, 39.948},
    {19, 39.098},
    {20, 40.078},
    {21, 44.956},
    {22, 47.867},
    {23, 50.942},
    {24, 51.996},
    {25, 54.938},
    {26, 55.845},
    {27, 58.933},
    {28, 58.693},
    {29, 63.546},
    {30, 65.38},
    {31, 69.723},
    {32, 72.64},
    {33, 74.922},
    {34, 78.96},
    {35, 79.904},
    {36, 83.798},
    {37, 85.468},
    {38, 87.62},
    {39, 88.906},
    {40, 91.224},
    {41, 92.906},
    {42, 95.96},
    {43, 98},
    {44, 101.07},
    {45, 102.906},
    {46, 106.42},
    {47, 107.868},
    {48, 112.411},
    {49, 114.818},
    {50, 118.71},
    {51, 121.76},
    {52, 127.6},
    {53, 126.904},
    {54, 131.293},
    {55, 132.905},
    {56, 137.327},
    {57, 138.905},
    {58, 140.116},
    {59, 140.908},
    {60, 144.242},
    {61, 145},
    {62, 150.36},
    {63, 151.964},
    {64, 157.25},
    {65, 158.925},
    {66, 162.5},
    {67, 164.93},
    {68, 167.259},
    {69, 168.934},
    {70, 173.054},
    {71, 174.967},
    {72, 178.49},
    {73, 180.948},
    {74, 183.84},
    {75, 186.207},
    {76, 190.23},
    {77, 192.217},
    {78, 195.084},
    {79, 196.967},
    {80, 200.59},
    {81, 204.383},
    {82, 207.2},
    {83, 208.98},
    {84, 210},
    {85, 210},
    {86, 222},
    {87, 223},
    {88, 226},
    {89, 227},
    {90, 232.038},
    {91, 231.036},
    {92, 238.029},
    {93, 237},
    {94, 244},
    {95, 243},
    {96, 247},
    {97, 247},
    {98, 251},
    {99, 252},
    {100, 257},
    {101, 258},
    {102, 259},
    {103, 262},
    {104, 261},
    {105, 262},
    {106, 266},
    {107, 264},
    {108, 267},
    {109, 268},
    {110, 271},
    {111, 272},
    {112, 285},
    {113, 284},
    {114, 289},
    {115, 288},
    {116, 292},
    {117, 295},
    {118, 294}
});

// map of atom colors by atomic number, using the standard Jmol/CPK color
// scheme so elements read in from structure files (e.g. POSCAR) render with
// their conventional colors instead of falling back to the default magenta.
std::unordered_map<int, std::tuple<const GLfloat, const GLfloat, const GLfloat>> atomColors({
    {0, {255, 20, 147}},
    {1, {255, 255, 255}},
    {2, {217, 255, 255}},
    {3, {204, 128, 255}},
    {4, {194, 255, 0}},
    {5, {255, 181, 181}},
    {6, {144, 144, 144}},
    {7, {48, 80, 248}},
    {8, {255, 13, 13}},
    {9, {144, 224, 80}},
    {10, {179, 227, 245}},
    {11, {171, 92, 242}},
    {12, {138, 255, 0}},
    {13, {191, 166, 166}},
    {14, {240, 200, 160}},
    {15, {255, 128, 0}},
    {16, {255, 255, 48}},
    {17, {31, 240, 31}},
    {18, {128, 209, 227}},
    {19, {143, 64, 212}},
    {20, {61, 255, 0}},
    {21, {230, 230, 230}},
    {22, {191, 194, 199}},
    {23, {166, 166, 171}},
    {24, {138, 153, 199}},
    {25, {156, 122, 199}},
    {26, {224, 102, 51}},
    {27, {240, 144, 160}},
    {28, {80, 208, 80}},
    {29, {200, 128, 51}},
    {30, {125, 128, 176}},
    {31, {194, 143, 143}},
    {32, {102, 143, 143}},
    {33, {189, 128, 227}},
    {34, {255, 161, 0}},
    {35, {166, 41, 41}},
    {36, {92, 184, 209}},
    {37, {112, 46, 176}},
    {38, {0, 255, 0}},
    {39, {148, 255, 255}},
    {40, {148, 224, 224}},
    {41, {115, 194, 201}},
    {42, {84, 181, 181}},
    {43, {59, 158, 158}},
    {44, {36, 143, 143}},
    {45, {10, 125, 140}},
    {46, {0, 105, 133}},
    {47, {192, 192, 192}},
    {48, {255, 217, 143}},
    {49, {166, 117, 115}},
    {50, {102, 128, 128}},
    {51, {158, 99, 181}},
    {52, {212, 122, 0}},
    {53, {148, 0, 148}},
    {54, {66, 158, 176}},
    {55, {87, 23, 143}},
    {56, {0, 201, 0}},
    {57, {112, 212, 255}},
    {58, {255, 255, 199}},
    {59, {217, 255, 199}},
    {60, {199, 255, 199}},
    {61, {163, 255, 199}},
    {62, {143, 255, 199}},
    {63, {97, 255, 199}},
    {64, {69, 255, 199}},
    {65, {48, 255, 199}},
    {66, {31, 255, 199}},
    {67, {0, 255, 156}},
    {68, {0, 230, 117}},
    {69, {0, 212, 82}},
    {70, {0, 191, 56}},
    {71, {0, 171, 36}},
    {72, {77, 194, 255}},
    {73, {77, 166, 255}},
    {74, {33, 148, 214}},
    {75, {38, 125, 171}},
    {76, {38, 102, 150}},
    {77, {23, 84, 135}},
    {78, {208, 208, 224}},
    {79, {255, 209, 35}},
    {80, {184, 184, 208}},
    {81, {166, 84, 77}},
    {82, {87, 89, 97}},
    {83, {158, 79, 181}},
    {84, {171, 92, 0}},
    {85, {117, 79, 69}},
    {86, {66, 130, 150}},
    {87, {66, 0, 102}},
    {88, {0, 125, 0}},
    {89, {112, 171, 250}},
    {90, {0, 186, 255}},
    {91, {0, 161, 255}},
    {92, {0, 143, 255}},
    {93, {0, 128, 255}},
    {94, {0, 107, 255}},
    {95, {84, 92, 242}},
    {96, {120, 92, 227}},
    {97, {138, 79, 227}},
    {98, {161, 54, 212}},
    {99, {179, 31, 212}},
    {100, {179, 31, 186}},
    {101, {179, 13, 166}},
    {102, {189, 13, 135}},
    {103, {199, 0, 102}},
    {104, {204, 0, 89}},
    {105, {209, 0, 79}},
    {106, {217, 0, 69}},
    {107, {224, 0, 56}},
    {108, {230, 0, 46}},
    {109, {235, 0, 38}}
});

Atom::Atom(double radius, int atomicNumber, cColorf color)
: cShapeSphere(radius) {
    anchor = false;
    current = false;
    repeating = false;
    selected = false;
    velVector = new cShapeLine(cVector3d(0, 0, 0), cVector3d(0, 0, 0));
    force.zero();
    this->atomicNumber = atomicNumber;
    // note - cColorf defaults to white, as such
    // the default for atoms is also white (see the header file)
    base_color = color;
    
    // set the color
    refreshMaterial();
}

Atom::Atom(double radius, int atomicNumber) : cShapeSphere(radius) {
    anchor = false;
    current = false;
    repeating = false;
    selected = false;
    velVector = new cShapeLine(cVector3d(0, 0, 0), cVector3d(0, 0, 0));
    force.zero();
    this->atomicNumber = atomicNumber;

    // check if atomic number has a registered color. If not, use 0 (magenta)
    std::tuple<const GLfloat, const GLfloat, const GLfloat> col = 
        (atomColors.find(atomicNumber) == atomColors.end()) ? atomColors[0] : atomColors[atomicNumber];

    base_color = cColorf();
    base_color.set(get<0>(col)/255, get<1>(col)/255, get<2>(col)/255);
    refreshMaterial();
}

void Atom::refreshMaterial() {
    m_material->m_emission.set(0.0f, 0.0f, 0.0f, 1.0f);
    if (selected || current) {
        m_material->setRed();
    } else if (anchor) {
        m_material->setBlue();
    } else if (repeating) {
        m_material->setBlack();
    } else {
        m_material->setColor(base_color);
    }
}

bool Atom::isAnchor() { return anchor; }

void Atom::setAnchor(bool newAnchor) {
    if (newAnchor) {
        current = false;
    }
    anchor = newAnchor;
    refreshMaterial();
}

bool Atom::isCurrent() { return current; }

void Atom::setCurrent(bool newCurrent) {
    if (newCurrent) {
        anchor = false;  // cannot be both anchor and current
    }
    current = newCurrent;
    refreshMaterial();
}

bool Atom::isRepeating() { return repeating; }

void Atom::setRepeating(bool newRepeat) {
    if (newRepeat) {
        anchor = false;
    }
    repeating = newRepeat;
    refreshMaterial();
}

bool Atom::isSelected() { return selected; }

void Atom::setSelected(bool newSelected) {
    selected = newSelected;
    refreshMaterial();
}

cVector3d Atom::getVelocity() { return velocity; }

void Atom::setVelocity(cVector3d newVel) { velocity = newVel; }

cVector3d Atom::getForce() { return force; }

void Atom::setForce(cVector3d newForce) {
    force = newForce;  // Add exception for if controlled atom is in the same
    // location as the anchored atom
}

cShapeLine* Atom::getVelVector() { return velVector; }

void Atom::setVelVector(cShapeLine* newVelVector) { velVector = newVelVector; }

void Atom::updateVelVector() {
    // Create a line representing the forces felt on the atom
    cVector3d newPointNormalized = cAdd(this->getLocalPos(), this->getForce());
    this->getForce().normalizer(newPointNormalized);
    this->velVector->m_pointA =
    cAdd(this->getLocalPos(), newPointNormalized * this->getRadius());
    this->velVector->m_pointB =
    cAdd(this->getVelVector()->m_pointA, this->getForce() * .005);
    this->velVector->setLineWidth(5);

    // Update the color based on the current status of the atom
    if (current || selected) {
        this->velVector->m_colorPointA.setRed();
        this->velVector->m_colorPointB.setRed();
    } else {
        this->velVector->m_colorPointA.setBlack();
        this->velVector->m_colorPointB.setBlack();
    }
}

void Atom::setInitialPosition(double spawn_dist) {
    double phi = rand() / double(RAND_MAX) * 2 * M_PI;
    double costheta = rand() / double(RAND_MAX) * 2 - 1;
    double u = rand() / double(RAND_MAX);
    double theta = acos(costheta);
    double r = spawn_dist * cbrt(u);
    setLocalPos(r * sin(theta) * cos(phi), r * sin(theta) * sin(phi),
                r * cos(theta));
}

void Atom::setColor(cColorf color) {
    if (!selected) {
        m_material->setColor(color);
        m_material->m_emission.set(0.0f, 0.0f, 0.0f, 1.0f);
    }
}

int Atom::getAtomicNumber() const { return atomicNumber; }

void Atom::setAtomicNumber( int num ) { atomicNumber = num; }

string Atom::getElement() {
    return atomStringNames[atomicNumber];
}

double Atom::getMass() {
    return atomWeights[atomicNumber];
}
