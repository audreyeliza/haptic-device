#ifndef ATOM_H
#define ATOM_H

#include "chai3d.h"
#include <queue>

using namespace std;
using namespace chai3d;

class Atom : public cShapeSphere {
    private:
        bool anchor;
        bool current;
        bool repeating;
        bool selected;
        cVector3d velocity;
        cVector3d force;
        cVector3d prevForce;
        cVector3d prevPos;
        std::vector<std::vector<std::vector<chai3d::cShapeSphere*>>> periodics;
        std::queue<chai3d::cVector3d> positionBuffer; // a position buffer so physics calculator calculate ahead
        cShapeLine *velVector; // a rendered line that represents the atom's velocity
        int atomicNumber;
        cColorf color;

        void refreshMaterial(chai3d::cShapeSphere *sphere);

    public:
        void setPeriodics(int x, int y, int z);

        /**
         * @brief A constructor for an atom
         * @param radius the radius of the atom. Should be based on the covalent radius
         * @param atomicNum the atomic number of the atom
         */
        Atom(double radius, int atomicNumber, chai3d::cWorld *world, cTexture2dPtr texture);

        const std::vector<std::vector<std::vector<chai3d::cShapeSphere*>>>& getPeriodics() const;

        /**
         * @brief Returns if the atom is anchor
         * @return true if the atom is anchored, false otherwise
         */
        bool isAnchor() const;

        /**
         * @brief Sets if the atom should be anchored
         * @param newAnchor if the atom should be anchored
         */
        void setAnchor(bool newAnchor);

        /**
         * @brief Returns if the atom is repeating
         * @return true if the atom is repeating, false otherwise
         */
        bool isRepeating() const;

        /**
         * @brief Sets if the atom should be repeating
         * @param newRepeat if the atom should be repeating
         */
        void setRepeating(bool newRepeat);

        /**
         * @brief Returns if the atom is current
         * @return true if the atom is current, false otherwise
         */
        bool isCurrent() const;

        /**
         * @brief Sets if the atom should be current
         * @param newCurrent if the atom should be current
         */
        void setCurrent(bool newCurrent);

        /**
         * @brief Returns if the atom is selected
         * @return true if the atom is selected, false otherwise
         */
        bool isSelected() const;

        /**
         * @brief Sets if the atom should be selected
         * @param newSelected if the atom should be selected
         */
        void setSelected(bool newSelected);

        /**
         * @brief Gets the velocity of the atom
         * @return the velocity of the atom in world units. One world unit is 50 Å.
         */
        cVector3d getVelocity() const;
        
        /**
         * @brief Sets the velocity of the atom in world units.
         * @param newVel the velocity of the atom in world units. One world unit is 50 Å.
         */
        void setVelocity(cVector3d newVel);

        /**
         * @brief Gets the force applied to the atom
         * @return the force applied to the atom in eV/Å
         */
        cVector3d getForce() const;

        /**
         * @brief Sets the force applied to the atom
         * @param newForce the force to apply to the atom in eV/Å
         */
        void setForce(cVector3d newForce);

        /**
         * @brief Gets the force previous to the current applied force.
         * @return the force previous to the current applied force
         */
        cVector3d getPrevForce() const;

        /**
         * @brief Gets the velocity vector of the atom as a rendered line
         * @return the velocity vector of the atom as a rendered line
         */
        cShapeLine *getVelVector() const;

        /**
         * @brief Sets the rendered velocity vector of the atom
         * @param newVelVector The new rendered velocity vector of the atom
         */
        void setVelVector(cShapeLine *newVelVector);

        /**
         * @brief Update the atom's rendered velocity vector
         */
        void updateForceVector();

        /**
         * @brief Sets the color of the atom
         * @param color The color to set the atom to
         */
        void setColor(cColorf color);

        /**
         * @brief Gets the atomic number of the atom
         * @return the atomic number of the atom
         */
        int getAtomicNumber() const;

        /**
         * @brief Sets the atomic number of the atom
         * @param num the atomic number of the atom to set to
         */
        void setAtomicNumber(int num);
        
        /**
         * @brief Gets the chemical symbol of the atom
         * @return the chemical symbol of the atom
         */
        string getElement() const;

        /**
         * @brief Gets the mass of the atom
         * @return the atomic mass of the atom in atomic mass units (amu)
         */
        double getMass() const;

        /**
         * @brief Sets the buffer position of the atom
         * @param pos the position to set the buffered pos to
         */
        void addBufferedPos(chai3d::cVector3d pos);

        /**
         * @brief Gets the buffer position of the atom
         * @return the buffered position of the atom
         */
        chai3d::cVector3d nextPos();

        /**
         * @brief Gets if the atom has a next buffered position
         * @return true if the atom has a next buffered position; false otherwise
         */
        bool hasNextPos() const;

        /**
         * @brief Gets the latest position added to the buffer
         * @return the latest position added to the buffer
         */
        chai3d::cVector3d getLatestPos() const;

        /**
         * @brief Gets the position most recently taken from the buffer
         * @return the position most recently taken from the buffer
         */
        chai3d::cVector3d getPrevPos() const;
};

#endif  // ATOM_H
