#ifndef EVENT_H
#define EVENT_H

#include <memory>
#include "robot.hpp"
#include "protocol.hpp"
#include "command.hpp"

template <typename B>
struct Event
{
  Commands * cmd;
  Robot * robot;
  uint8_t sender;
  uint8_t receiver;

  Event(Commands * cmd, Robot * robot, uint8_t sender, uint8_t receiver)
  : cmd(cmd), robot(robot), sender(sender), receiver(receiver) {};

  void do_step(float time_step) {
    for(auto & msg : update_msg()) {
      auto data = msg.encode_msg(B::set, B::cmd);
      spdlog::debug("Push Event Msg {} bytes: {:n}", data.size(), spdlog::to_hex(data));
      cmd->send(data);
    }
  }
  virtual std::vector<typename B::Response> update_msg() = 0;
  virtual ~Event(){};
};

void encode(std::vector<uint8_t> &buffer, size_t location, BoundingBox & object) {
  write<float>(buffer, location + 0, object.x);
  write<float>(buffer, location + 4, object.y);
  write<float>(buffer, location + 8, object.width);
  write<float>(buffer, location + 12, object.height);
}

void encode(std::vector<uint8_t> &buffer, size_t location, DetectedObjects::Person & object) {
  encode(buffer, location, object.bounding_box);
}

void encode(std::vector<uint8_t> &buffer, size_t location, DetectedObjects::Gesture & object) {
  encode(buffer, location, object.bounding_box);
  write<uint32_t>(buffer, location+ 16, object.id);
}

void encode(std::vector<uint8_t> &buffer, size_t location, DetectedObjects::Line & object) {
  write<float>(buffer, location + 0, object.x);
  write<float>(buffer, location + 4, object.y);
  write<float>(buffer, location + 8, object.angle);
  write<float>(buffer, location + 12, object.curvature);
  write<uint32_t>(buffer, location + 16, object.info);
}

void encode(std::vector<uint8_t> &buffer, size_t location, DetectedObjects::Marker & object) {
  encode(buffer, location, object.bounding_box);
  write<uint16_t>(buffer, location + 16, object.id);
  write<uint16_t>(buffer, location + 18, object.distance);
}

void encode(std::vector<uint8_t> &buffer, size_t location, DetectedObjects::Robot & object) {
  encode(buffer, location, object.bounding_box);
}

struct VisionDetectInfo : Proto<0xa, 0xa4>
{
  struct Response : ResponseT{

    Response(uint8_t sender, uint8_t receiver, uint16_t type, uint8_t number)
    : ResponseT(sender, receiver), type(type), number(number), status(0), errcode(0),
      buffer(20 * number + 9) {};

    uint8_t type;
    uint8_t number;
    uint8_t status;
    uint16_t errcode;
    std::vector<uint8_t> buffer;

    std::vector<uint8_t> encode()
    {
      buffer[0] = type;
      buffer[1] = status;
      write<uint16_t>(buffer, 6, errcode);
      buffer[8] = number;
      // buffer is encoded externally to get around polymorphic items
      return buffer;
    }

  };

};


struct VisionEvent : Event<VisionDetectInfo> {

  VisionEvent(Commands * cmd, Robot * robot, uint8_t sender, uint8_t receiver, uint8_t type) :
    Event(cmd, robot, sender, receiver), type(type){};

  template<typename T>
  void add_message(std::vector<VisionDetectInfo::Response> & msgs, DetectedObjects & objects) {
    if(type & (1 << T::type)) {
      auto items = objects.get<T>();
      auto size = items.size();
      if(size) {
        VisionDetectInfo::Response msg(sender, receiver, T::type, size);
        size_t location = 9;
        for (auto & item : items) {
          encode(msg.buffer, location, item);
          location += 20;
        }
        msgs.push_back(msg);
      }
    }
  }

  std::vector<VisionDetectInfo::Response> update_msg() {
    auto objects = robot->get_detected_objects();
    std::vector<VisionDetectInfo::Response> msgs;
    add_message<DetectedObjects::Person>(msgs, objects);
    add_message<DetectedObjects::Gesture>(msgs, objects);
    add_message<DetectedObjects::Line>(msgs, objects);
    add_message<DetectedObjects::Marker>(msgs, objects);
    add_message<DetectedObjects::Robot>(msgs, objects);
    return msgs;
  }

  uint8_t type;

};


struct ArmorHitEventMsg : Proto<0x3f, 0x02>
{
  struct Response : ResponseT{

    Response(uint8_t sender, uint8_t receiver, uint8_t type, uint8_t index,
             uint16_t mic_value, uint16_t mic_len)
    : ResponseT(sender, receiver), type(type), index(index), mic_value(mic_value), mic_len(mic_len)
    {
    }

    uint8_t type;
    uint8_t index;
    uint16_t mic_value;
    uint16_t mic_len;

    std::vector<uint8_t> encode()
    {
      std::vector<uint8_t> buffer(5, 0);
      buffer[0] = (index << 4) | type;
      write<uint16_t>(buffer, 1, mic_value);
      write<uint16_t>(buffer, 3, mic_len);
      return buffer;
    }
  };

};

struct ArmorHitEvent : Event<ArmorHitEventMsg> {

  ArmorHitEvent(Commands * cmd, Robot * robot, uint8_t sender=0xc9, uint8_t receiver=0x38) :
    Event(cmd, robot, sender, receiver) {};

  std::vector<ArmorHitEventMsg::Response> update_msg() {
    auto hits = robot->get_hit_events();
    std::vector<ArmorHitEventMsg::Response> msgs;
    for (auto const& hit : hits) {
      msgs.emplace_back(sender, receiver, hit.type, hit.index, 0, 0);
    }
    return msgs;
  }


  uint8_t type;

};

#endif /* end of include guard: EVENT_H */
