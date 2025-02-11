#ifndef COPPELIASIM_PLUGIN_COPPELIASIM_ROBOT_HPP_
#define COPPELIASIM_PLUGIN_COPPELIASIM_ROBOT_HPP_

#include <map>
#include <string>
#include <vector>

#include "robot/robot.hpp"

#include <simPlusPlus/Lib.h>

class CoppeliaSimRobot : public Robot {
 public:
  CoppeliaSimRobot(WheelValues<simInt> _wheel_joint_handles, ChassisLEDValues<simInt> _led_handles,
                   bool _enable_arm, simInt _camera_handle, ServoValues<simInt> _servo_motor,
                   GimbalValues<simInt> _gimbal_motor,
                   GimbalLEDValues<std::vector<simInt>> _gimbal_led_handles,
                   simInt _blaster_light_handle, bool _enable_gripper,
                   std::string _gripper_state_signal, std::string _gripper_target_signal,
                   simInt _imu_handle, std::string _accelerometer_signal, std::string _gyro_signal)
      : Robot(_enable_arm, _enable_gripper,
              {_servo_motor[0] > 0, _servo_motor[1] > 0, _servo_motor[2] > 0},
              (_gimbal_motor.yaw > 0 && _gimbal_motor.pitch > 0), _camera_handle > 0, false)
      , wheel_joint_handles(_wheel_joint_handles)
      , chassis_led_handles(_led_handles)
      , gimbal_led_handles(_gimbal_led_handles)
      , blaster_light_handle(_blaster_light_handle)
      , camera_handle(_camera_handle)
      , servo_handles({{0, _servo_motor[0]},
                       {1, _servo_motor[1]},
                       {2, _servo_motor[2]},
                       {3, _gimbal_motor.yaw},
                       {4, _gimbal_motor.pitch}})
      , gripper_state_signal(_gripper_state_signal)
      , gripper_target_signal(_gripper_target_signal)
      , imu_handle(_imu_handle)
      , accelerometer_signal(_accelerometer_signal)
      , gyro_signal(_gyro_signal) {}

  // void update_led_colors(LEDColors &);
  WheelSpeeds read_wheel_speeds() const;
  void forward_target_wheel_speeds(const WheelSpeeds &);
  WheelValues<float> read_wheel_angles() const;
  IMU read_imu() const;
  void forward_chassis_led(size_t index, const Color &);
  void forward_gimbal_led(size_t index, size_t part, const Color &);
  Gripper::Status read_gripper_state() const;
  void forward_target_gripper(Gripper::Status state, float power);
  bool forward_camera_resolution(unsigned width, unsigned height);
  Image read_camera_image() const;
  hit_event_t read_hit_events() const;
  ir_event_t read_ir_events() const;
  DetectedObjects read_detected_objects() const;
  void forward_target_servo_angle(size_t index, float angle);
  void forward_target_servo_speed(size_t index, float speed);
  float read_servo_angle(size_t index) const;
  float read_servo_speed(size_t index) const;
  void forward_servo_mode(size_t index, Servo::Mode mode);
  void forward_servo_enabled(size_t index, bool value);
  void forward_target_gimbal_speed(const GimbalValues<float> &speed);
  void forward_target_gimbal_angle(const GimbalValues<float> &angle);
  void forward_blaster_led(float value) const;
  void forward_engage_wheel_motors(bool value);
  void enable_tof(size_t index, simInt sensor_handle);
  float read_tof(size_t index) const;

  // void has_read_accelerometer(float x, float y, float z);
  // void has_read_gyro(float x, float y, float z);
  // void update_orientation(float alpha, float beta, float gamma);

 private:
  WheelValues<simInt> wheel_joint_handles;
  ChassisLEDValues<simInt> chassis_led_handles;
  GimbalLEDValues<std::vector<simInt>> gimbal_led_handles;
  simInt blaster_light_handle;
  const std::map<unsigned, int> servo_handles;
  std::map<unsigned, int> tof_handles;
  simInt camera_handle;
  std::string gripper_state_signal;
  std::string gripper_target_signal;
  simInt imu_handle;
  std::string accelerometer_signal;
  std::string gyro_signal;
};

#endif  // COPPELIASIM_PLUGIN_COPPELIASIM_ROBOT_HPP_
