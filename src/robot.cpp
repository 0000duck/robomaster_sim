#include <math.h>
#include <string.h>
#include <stdexcept>
#include <algorithm>

#include <spdlog/spdlog.h>

#include "robot.hpp"
#include "command.hpp"
#include "utils.hpp"
#include "action.hpp"
#include "streamer.hpp"

static Color breath_led(float _time, Color color, float period_1, float period_2) {
  float f;
  if(_time < period_1){
    f = sin(_time/period_1 * M_PI_2);
  }
  else {
    f = cos((_time - period_1)/period_2 * M_PI_2);
  }
  spdlog::debug("breath {} {}: {} -> {}", period_1, period_2, _time, f);
  return color * (f * f);
}

static Color flash_led(float _time, Color color, float period_1, float period_2) {
  if(_time < period_1) return color;
  return {};
}

static Color scroll_led(float _time, Color color, float period_1, float period_2) {
  if(_time < 0.175f) return color;
  if(_time < (0.175f + period_1)) return {};
  return color;
}

ActiveLED::ActiveLED()
: color(), active(false), _time(0) {
  }

void ActiveLED::update(Color _color, LedEffect _effect, float _period_1, float _period_2, bool _loop) {
  tcolor = _color;
  period_1 = _period_1;
  period_2 = _period_2;
  loop = _loop;
  effect = _effect;
  period = _period_1 + _period_2;
  if(effect == LedEffect::off) color = {};
  if(effect == LedEffect::on) color = tcolor;
  if(effect == LedEffect::scrolling) period = 0.175 + _period_1 + 7.5 * period_2;
  active = (effect != LedEffect::off && effect != LedEffect::on);
  if(active) _time = 0;
}

void ActiveLED::do_step(float time_step) {
  if(!active) return;
  _time = _time + time_step;
  if(!loop && _time > period) {
    active = false;
    // TODO(jerome): Check which color at the end of an effect
    return;
  }
  _time = fmod(_time, period);
  if(effect == LedEffect::flash) {
    color = flash_led(_time, tcolor, period_1, period_2);
  }
  else if (effect == LedEffect::breath) {
    color = breath_led(_time, tcolor, period_1, period_2);
  }
  else if (effect == LedEffect::scrolling) {
    color = scroll_led(_time, tcolor, period_1, period_2);
  }
}

static WheelSpeeds wheel_speeds_from_twist(Twist2D &twist, float l=0.2, float radius=0.05) {
  WheelSpeeds value;
  value.front_left = (twist.x - twist.y - l * twist.theta) / radius;
  value.front_right = (twist.x + twist.y + l * twist.theta) / radius;
  value.rear_left = (twist.x + twist.y - l * twist.theta) / radius;
  value.rear_right = (twist.x - twist.y + l * twist.theta) / radius;
  return value;
}

static Twist2D twist_from_wheel_speeds(WheelSpeeds &speeds, float l=0.2, float radius=0.05) {
  Twist2D value;
  value.x = 0.25 * (speeds.front_left + speeds.front_right + speeds.rear_left + speeds.rear_right) * radius;
  value.y = 0.25 * (-speeds.front_left + speeds.front_right + speeds.rear_left - speeds.rear_right) * radius;
  value.theta = 0.25 * (-speeds.front_left + speeds.front_right - speeds.rear_left + speeds.rear_right) * radius / l;
  return value;
}


static std::vector<unsigned char> generate_strip_image(unsigned i0, unsigned i1, unsigned width, unsigned height) {
  unsigned size = width * height * 3;
  std::vector<unsigned char> buffer(size, 0);
  if(i0 > i1) i1 += width;
  for (size_t i = i0; i < i1; i++)
    for (size_t j = 0; j < height; j++)
      buffer[(3 * (j * width + i)) % size] = 255;
  return buffer;
}

Robot::Robot()
  : imu(), target_wheel_speed(),
  mode(Mode::FREE), axis_x(0.1),  axis_y(0.1), wheel_radius(0.05), sdk_enabled(false),
  odometry(), body_twist(), desired_target_wheel_speed(), wheel_speeds(), wheel_angles(),
  leds(), led_colors(), time_(0.0f), commands(nullptr), video_streamer(nullptr)  {
  }

void Robot::do_step(float time_step) {
  time_ += time_step;

  wheel_speeds = read_wheel_speeds();
  wheel_angles = read_wheel_angles();
  imu = read_imu();
  update_odometry(time_step);
  update_attitude(time_step);

  // spdlog::debug("state {}", odometry);


  // Update Actions

  if(move_action) {
    if(move_action->state == Action::State::started) {
        move_action->goal_odom = get_pose() * move_action->goal;
        move_action->state = Action::State::running;
        spdlog::info("Start Move Action to {} [odom]", move_action->goal_odom);
    }
    if(move_action->state == Action::State::running) {
      Pose2D goal = move_action->goal_odom.relative_to(get_pose());
      spdlog::info("Update Move Action to {} [frame]", goal);
      const float tau = 0.5f;
      float remaining_duration;
      Twist2D twist;
      if (goal.norm() < 0.01) {
        twist = {0, 0, 0};
        move_action->state = Action::State::succeed;
        remaining_duration = 0;
        spdlog::info("Move Action done", goal);
      }
      else {
        float f = std::min(1.0f, move_action->linear_speed / (goal.distance() / tau));
        twist = {
          f * goal.x / tau,
          f * goal.y / tau,
          std::clamp(goal.theta / tau, -move_action->angular_speed, move_action->angular_speed)
        };
        remaining_duration = time_to_goal(goal, move_action->linear_speed, move_action->angular_speed);
        spdlog::info("Move Action continue [{:.2f} s]", remaining_duration);
      }
      move_action->current = goal;
      move_action->remaining_duration = remaining_duration;
      set_target_velocity(twist);
    }
    auto data = move_action->do_step(time_step);
    if(data.size()) {
      spdlog::debug("Push action {} bytes: {:n}", data.size(), spdlog::to_hex(data));
      commands->send(data);
      if (move_action->state == Action::State::failed || move_action->state == Action::State::succeed) {
        move_action = NULL;
      }
    }
  }

  // for (auto const& [key, action] : actions) {
  //   commands->send(action->encode());
  //   if (action->state == Action::State::failed || action->state == Action::State::succeed) {
  //     actions.erase(action->id);
  //   }
  // }

  // LED
  leds.do_step(time_step);
  LEDColors desired_led_colors = leds.desired_colors();
  if (desired_led_colors != led_colors) {
    spdlog::debug("led_colors -> desired_led_colors = {}", desired_led_colors);
    led_colors = desired_led_colors;
    update_led_colors(led_colors);
  }
  // Motors
  if (desired_target_wheel_speed != target_wheel_speed) {
    spdlog::debug("target_wheel_speed -> desired_target_wheel_speed = {}", desired_target_wheel_speed);
    target_wheel_speed = desired_target_wheel_speed;
    update_target_wheel_speeds(target_wheel_speed);
  }

  // arm
  // Gripper
  //

  // Update Publishers
  if(commands)
    commands->do_step(time_step);


  // Stream the [for now dummy] camera image
  if(video_streamer) {
    spdlog::info("[Robot] stream new dummy frame");
    static unsigned seq = 0;
    seq = (seq + 1) % 640;
    auto image = generate_strip_image(seq, seq + 10, 640, 360);
    video_streamer->send(image.data());
  }
}

void Robot::update_odometry(float time_step){
  body_twist = twist_from_wheel_speeds(wheel_speeds);
  odometry.twist = body_twist.rotate_around_z(odometry.pose.theta);
  odometry.pose = odometry.pose + odometry.twist * time_step;
}

void Robot::update_attitude(float time_step){
  // TODO(jerome): compute attitude from the other sensing values
  imu.attitude.yaw += time_step * imu.angular_velocity.z;

  // use attitude as a source for odometry angular data:
  odometry.twist.theta = imu.angular_velocity.z;
  odometry.pose.theta = imu.attitude.yaw;
  // TODO(jerome): body twist too
}

void Robot::set_target_wheel_speeds(WheelSpeeds &speeds) {
  desired_target_wheel_speed = speeds;
}

void Robot::set_led_effect(Color color, LedMask mask, ActiveLED::LedEffect effect, float period_on, float period_off, bool loop)
{
  if(mask & ARMOR_BOTTOM_BACK) leds.rear.update(color, effect, period_on, period_off, loop);
  if(mask & ARMOR_BOTTOM_FRONT) leds.front.update(color, effect, period_on, period_off, loop);
  if(mask & ARMOR_BOTTOM_LEFT) leds.right.update(color, effect, period_on, period_off, loop);
  if(mask & ARMOR_BOTTOM_RIGHT) leds.left.update(color, effect, period_on, period_off, loop);
}

void Robot::set_mode(Mode _mode){
  mode = _mode;
}

Robot::Mode Robot::get_mode(){
  return mode;
}

void Robot::set_target_velocity(Twist2D &twist)
{
  // TODO(jerome): Could also postpose this at do_step time.
  // should do it anyway for the position control
  WheelSpeeds speeds = wheel_speeds_from_twist(twist, axis_x + axis_y, wheel_radius);
  set_target_wheel_speeds(speeds);
}

void Robot::set_enable_sdk(bool value){
  sdk_enabled = value;
}

Twist2D Robot::get_twist(Frame frame) {
  if(frame == Frame::odom)
    return odometry.twist;
  else
    return body_twist;
}

Pose2D Robot::get_pose() {
  return odometry.pose;
}

Attitude Robot::get_attitude() {
  return imu.attitude;
}

IMU Robot::get_imu() {
  return imu;
}

Robot::GripperStatus Robot::get_gripper_status() {
  return gripper_state;
}

Vector3 Robot::get_arm_position() {
  return arm_position;
}

void Robot::set_target_gripper(GripperStatus state, float power) {
  desired_gripper_state = state;
  desired_gripper_power = power;
}

bool Robot::submit_action(std::shared_ptr<MoveAction> action) {
  // TODO(jerome): What should we do if an action is already active?
  if(move_action) return false;
  move_action = action;
  move_action->state = Action::State::started;
  return true;
}

void Robot::start_streaming(int resolution) {
  if(!video_streamer) {
    spdlog::info("[Robot] start streaming");
    video_streamer = std::make_shared<VideoStreamer>(commands->get_io_context(), 640, 360, 25);
  }
}

void Robot::stop_streaming() {
  if(video_streamer) {
    spdlog::info("[Robot] stop streaming");
    video_streamer = nullptr;
  }
}
