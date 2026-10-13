#ifndef _WIN32

#include "customHapticDevice.h"

using namespace chai3d;

CustomHapticDevice::CustomHapticDevice(const std::string &serialPort)
    : cGenericHapticDevice(0), m_serialPort(serialPort), m_forcesEnabled(true),
      m_referenceAngle(0.0f) {
  m_deviceAvailable = false;
  m_deviceReady = false;

  m_specifications.m_model = C_HAPTIC_DEVICE_CUSTOM;
  m_specifications.m_modelName = "Custom 1-DOF Capstan Device";
  m_specifications.m_manufacturerName = "DIY";
  // PLACEHOLDERS below - retune once the cable/capstan mechanism is attached
  // and its real force/workspace limits are known.
  m_specifications.m_maxLinearForce = 1.0;
  m_specifications.m_maxAngularTorque = 0.0;
  m_specifications.m_maxGripperForce = 0.0;
  m_specifications.m_maxLinearStiffness = 500.0;
  m_specifications.m_maxAngularStiffness = 0.0;
  m_specifications.m_maxGripperLinearStiffness = 0.0;
  m_specifications.m_maxLinearDamping = 0.0;
  m_specifications.m_maxAngularDamping = 0.0;
  m_specifications.m_maxGripperAngularDamping = 0.0;
  m_specifications.m_workspaceRadius = 0.05;
  m_specifications.m_gripperMaxAngleRad = 0.0;
  m_specifications.m_sensedPosition = true;
  m_specifications.m_sensedRotation = false;
  m_specifications.m_sensedGripper = false;
  m_specifications.m_actuatedPosition = true;
  m_specifications.m_actuatedRotation = false;
  m_specifications.m_actuatedGripper = false;
  m_specifications.m_leftHand = true;
  m_specifications.m_rightHand = true;
}

CustomHapticDevice::~CustomHapticDevice() {
  close();
}

bool CustomHapticDevice::open() {
  if (!m_serial.open(m_serialPort)) {
    m_deviceAvailable = false;
    m_deviceReady = false;
    return C_ERROR;
  }
  m_deviceAvailable = true;
  m_deviceReady = true;
  return C_SUCCESS;
}

bool CustomHapticDevice::close() {
  m_serial.close();
  m_deviceAvailable = false;
  m_deviceReady = false;
  return C_SUCCESS;
}

bool CustomHapticDevice::calibrate(bool a_forceCalibration) {
  (void)a_forceCalibration;
  // Opening the serial port triggers a DTR reset on the Arduino, so the
  // firmware reboots and has to rerun motor.initFOC()'s self-driven
  // calibration (open-loop spin for direction-finding, plus electrical zero
  // alignment) before it sends its first real StateFrame - that alone can
  // take 3+ seconds. Wait comfortably longer than that so we don't zero
  // against a stale/default (0.0) reading while the board is still rebooting.
  for (int i = 0; i < 1600 && !m_serial.hasState(); i++) {
    m_serial.poll();
    cSleepMs(5);
  }
  // motor.shaft_angle accumulates continuously in the firmware and never
  // wraps, so whatever it reads right now becomes "home" - getPosition()
  // reports motion relative to this, not the raw absolute angle.
  m_referenceAngle = m_serial.shaftAngle();
  return m_deviceReady;
}

bool CustomHapticDevice::getPosition(cVector3d &a_position) {
  m_serial.poll();
  a_position.set((m_serial.shaftAngle() - m_referenceAngle) * metersPerRadian, 0.0, 0.0);
  return m_deviceReady;
}

bool CustomHapticDevice::getLinearVelocity(cVector3d &a_linearVelocity) {
  m_serial.poll();
  a_linearVelocity.set(m_serial.shaftVelocity() * metersPerRadian, 0.0, 0.0);
  m_linearVelocity = a_linearVelocity;
  return m_deviceReady;
}

bool CustomHapticDevice::setForceAndTorqueAndGripperForce(const cVector3d &a_force,
                                                           const cVector3d &a_torque,
                                                           double a_gripperForce) {
  m_prevForce = a_force;
  m_prevTorque = a_torque;
  m_prevGripperForce = a_gripperForce;

  auto now = std::chrono::steady_clock::now();
  if (now - m_lastTorqueSend < kTorqueSendInterval) {
    return m_deviceReady;
  }
  m_lastTorqueSend = now;

  double torqueVolts = m_forcesEnabled ? (a_force.x() / newtonsPerVolt) : 0.0;
  m_serial.sendTorque(static_cast<float>(torqueVolts));
  return m_deviceReady;
}

bool CustomHapticDevice::enableForces(bool a_value) {
  m_forcesEnabled = a_value;
  if (!a_value) {
    m_serial.sendTorque(0.0f);
  }
  return true;
}

#endif // _WIN32
