#include "boundaryConditions.h"
#include "chai3d.h"

extern int just_unanchored;

using namespace chai3d;

// David's method -- atoms are not slowed down in any way, but atoms launched to walls are stopped theere
void applyDavidBoundaryConditions(cVector3d &A, cVector3d &B) {
    // freezing atoms at wall when they fly off 
    if (just_unanchored != 5) {
        // threshold for cube contining atoms
        double threshold = .5;
        // correct to keep atom inside of cube
        if (B.x() > threshold) {
        B.x(threshold);
        } else if (B.x() < threshold * -1) {
        B.x(threshold * -1);
        }
        if (B.y() > threshold) {
        B.y(threshold);
        } else if (B.y() < threshold * -1) {
        B.y(threshold * -1);
        }
        if (B.z() > threshold) {
        B.z(threshold);
        } else if (B.z() < threshold * -1) {
        B.z(threshold * -1);
        }
    }
}

// Sean's method
// this checkbounds is a temporary duplicate. All boundary information should be moved into this file eventually
bool checkBounds(cVector3d location, const double boundaryLimit) {
  if (location.y() > boundaryLimit || location.y() < -boundaryLimit ||
      location.x() > boundaryLimit || location.x() < -boundaryLimit ||
      location.z() > boundaryLimit || location.z() < -boundaryLimit) {
    return false;
  }
  return true;
}

void applySeanBoundaryConditions(chai3d::cVector3d &A,
                                chai3d::cVector3d &B, 
                                chai3d::cVector3d &spherePos, 
                                const chai3d::cVector3d &northPlanePos,
                                const chai3d::cVector3d &northPlaneNorm,
                                const chai3d::cVector3d &southPlanePos,
                                const chai3d::cVector3d &southPlaneNorm,
                                const chai3d::cVector3d &eastPlanePos,
                                const chai3d::cVector3d &eastPlaneNorm,
                                const chai3d::cVector3d &westPlanePos,
                                const chai3d::cVector3d &westPlaneNorm,
                                const chai3d::cVector3d &forwardPlanePos,
                                const chai3d::cVector3d &forwardPlaneNorm,
                                const chai3d::cVector3d &backPlanePos,
                                const chai3d::cVector3d &backPlaneNorm,
                                const double boundaryLimit
                                ) {
    const cVector3d originalA = A;
    const cVector3d originalB = B;
    const cVector3d delta = B - A;
    if (delta.length() <= 1e-12) {
        spherePos = A;
        return;
    }

    auto reflectAcrossPlane = [&](const cVector3d &planePos,
                                  const cVector3d &planeNorm,
                                  cVector3d &currentA,
                                  cVector3d &currentB) {
        const cVector3d normalizedNorm = cNormalize(planeNorm);
        const double startDist = normalizedNorm.dot(currentA - planePos);
        const double endDist = normalizedNorm.dot(currentB - planePos);

        if (startDist <= 0.0 && endDist > 0.0) {
            const double t = startDist / (startDist - endDist);
            const cVector3d intersectPoint = currentA + (currentB - currentA) * t;
            const cVector3d remaining = currentB - intersectPoint;
            const cVector3d reflected = remaining - normalizedNorm * (2.0 * normalizedNorm.dot(remaining));
            currentB = intersectPoint + reflected;
        }
    };

    reflectAcrossPlane(northPlanePos, northPlaneNorm, A, B);
    reflectAcrossPlane(southPlanePos, southPlaneNorm, A, B);
    reflectAcrossPlane(eastPlanePos, eastPlaneNorm, A, B);
    reflectAcrossPlane(westPlanePos, westPlaneNorm, A, B);
    reflectAcrossPlane(forwardPlanePos, forwardPlaneNorm, A, B);
    reflectAcrossPlane(backPlanePos, backPlaneNorm, A, B);

    spherePos = B;
    if (!checkBounds(spherePos, boundaryLimit)) {
        spherePos = originalB;
    }
}
