#pragma once

#include <cinttypes>
#include <vector>
#include <deque>

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/application.h"
namespace esphome {
namespace novoferm {

// PROTOCOL_DEFINITIONS_START

enum MessageType : uint16_t {
  STATUS = 0x0104,
  COMMAND = 0x0106,
  MALFORMED = 0x0184,
};

inline const char *message_type_to_str(MessageType t) {
  switch (t) {
    case STATUS:
      return "Status";
    case COMMAND:
      return "Command";
    default:
      return "Unknown";
  }
}

// Novoferm 423, only supports GATE
// Novoport IV, supports GATE and LIGHT with UNKNOWN being 0x0C
enum Target : uint16_t {
  GATE = 0x0A,
  LIGHT = 0x0B,
  UNKNOWN = 0x0C,
};

enum GateStatus : uint8_t {
  PAUSED,
  CLOSED,
  VENTILATING,
  OPENED,
  OPENING,
  CLOSING,
};

inline const char *gate_status_to_str(GateStatus s) {
  switch (s) {
    case PAUSED:
      return "Paused";
    case CLOSED:
      return "Closed";
    case VENTILATING:
      return "Ventilating";
    case OPENED:
      return "Opened";
    case OPENING:
      return "Opening";
    case CLOSING:
      return "Closing";
    default:
      return "Unknown";
  }
}

enum GateAction : uint8_t {
  EMERGENCY_STOP = 0x00,
  STOP_OR_CLOSE = 0x01,
  VENTILATE = 0x02,
  OPEN = 0x03,
};

enum LightStatus : uint8_t { OFF, ON };
inline const char *light_status_to_str(LightStatus s) {
  switch (s) {
    case OFF:
      return "OFF";
    case ON:
      return "ON";
    default:
      return "Unknown";
  }
}

// MessageHeader appears at the start of every message, both requests and replies.
struct MessageHeader {
  uint16_t seq;
  uint32_t len;
  MessageType type;

  MessageHeader() = default;

  MessageHeader(MessageType type, uint16_t seq, uint32_t payload_size)
      : seq(seq), len(payload_size + sizeof(type)), type(type) {}

  std::string print() const {
    char buf[64];
    snprintf(buf, sizeof(buf), "MessageHeader: seq %u, len %u, type %s", seq, len, message_type_to_str(type));
    return buf;
  }

  // payload_size returns the amount of payload bytes to be read from the uart
  uint32_t payload_size() const { return len - sizeof(type); }

  // Reads a MessageHeader from the buffer, returns the struct
  static MessageHeader read_from(const uint8_t *buffer) {
    uint16_t be_seq;
    uint32_t be_len;
    uint16_t be_type;

    std::memcpy(&be_seq, buffer, sizeof(be_seq));
    buffer += sizeof(be_seq);

    std::memcpy(&be_len, buffer, sizeof(be_len));
    buffer += sizeof(be_len);

    std::memcpy(&be_type, buffer, sizeof(be_type));
    buffer += sizeof(be_type);

    MessageHeader header;
    header.seq = convert_big_endian(be_seq);
    header.len = convert_big_endian(be_len);
    header.type = static_cast<MessageType>(convert_big_endian(be_type));

    return header;
  }

  void write_to(uint8_t *&buffer) const {
    uint16_t be_seq = convert_big_endian(seq);
    uint32_t be_len = convert_big_endian(len);
    uint16_t be_type = convert_big_endian(static_cast<uint16_t>(type));

    std::memcpy(buffer, &be_seq, sizeof(be_seq));
    buffer += sizeof(be_seq);

    std::memcpy(buffer, &be_len, sizeof(be_len));
    buffer += sizeof(be_len);

    std::memcpy(buffer, &be_type, sizeof(be_type));
    buffer += sizeof(be_type);
  }
} __attribute__((packed));

struct StatusReply {
  uint8_t ack = 0x2;
  GateStatus gateState;
  LightStatus lightState;

  // Reads StatusReply from a buffer (must point to at least sizeof(StatusReply) bytes)
  static StatusReply read_from(const uint8_t *buffer) noexcept {
    StatusReply reply;
    reply.ack = buffer[0];
    reply.gateState = static_cast<GateStatus>(buffer[1]);
    reply.lightState = static_cast<LightStatus>(buffer[2]);
    return reply;
  }

  std::string print(Target expected_type) const {
    char buf[64];
    switch (expected_type) {
      case Target::GATE:
        snprintf(buf, sizeof(buf), "StatusReply: gate state %s", gate_status_to_str(this->gateState));
        break;
      case Target::LIGHT:
        snprintf(buf, sizeof(buf), "StatusReply: light state %s", this->lightState == LightStatus::ON ? "ON" : "OFF");
        break;
      default:
        return "StatusReply: unknown status type";
    }
    return buf;
  }
} __attribute__((packed));

struct NovofermCommand {
  Target target;
  uint8_t pad = 0x0;
  uint8_t action;

  NovofermCommand() = default;

  NovofermCommand(Target target, uint8_t action) : target(target), pad(0), action(action) {}

  // Write serialized data into provided buffer (assumes buffer is at least 4 bytes)
  void write_to(uint8_t *&buffer) const {
    uint16_t be_target = convert_big_endian(target);
    std::memcpy(buffer, &be_target, sizeof(be_target));
    buffer += sizeof(be_target);

    *buffer++ = pad;
    *buffer++ = action;
  }

  // Read from buffer to populate fields (buffer must have at least 4 bytes)
  static NovofermCommand read_from(const uint8_t *buffer) {
    uint16_t be_target;
    std::memcpy(&be_target, buffer, sizeof(be_target));
    return {static_cast<Target>(convert_big_endian(be_target)), buffer[3]};
  }
};

// Command tells the gate to start or stop moving.
// It is echoed back by the unit on success.
struct CommandEchoReply {
  Target type;
  uint16_t state_raw;

  CommandEchoReply() = default;
  std::string print() const {
    char buf[32];
    snprintf(buf, sizeof(buf), "CommandEchoReply: 0x%04X", state_raw);
    return buf;
  }

  // Reads CommandEchoReply from buffer (expects at least 4 bytes)
  static CommandEchoReply read_from(const uint8_t *buffer) noexcept {
    CommandEchoReply reply;

    uint16_t be_type, be_state;
    std::memcpy(&be_type, buffer, sizeof(be_type));
    std::memcpy(&be_state, buffer + sizeof(be_type), sizeof(be_state));

    reply.type = static_cast<Target>(convert_big_endian(be_type));
    reply.state_raw = convert_big_endian(be_state);

    return reply;
  }
} __attribute__((packed));

static constexpr uint8_t MESSAGE_SIZE = sizeof(MessageHeader) + sizeof(NovofermCommand);

// PROTOCOL_DEFINITIONS_END

struct NovofermStatusListener {
  Target type;
  std::function<void()> on_data;
};

struct TXQueueEntry {
  MessageType type;
  NovofermCommand command;
};

class Novoferm : public Component, public uart::UARTDevice {
 public:
  float get_setup_priority() const override { return setup_priority::BUS - 0.1F; }
  void setup() override;
  void loop() override;
  void dump_config() override;
  void set_cover_state_listener(const std::function<void(GateStatus)> &func) { cover_state_callback_ = func; }
  void set_light_state_listener(const std::function<void(LightStatus)> &func) { light_state_callback_ = func; }
  void set_ventilation_state_listener(const std::function<void(bool)> &func) { ventilation_state_callback_ = func; }

  void add_on_initialized_callback(std::function<void()> callback) {
    this->initialized_callback_.add(std::move(callback));
  }

  void perform_gate_action(GateAction action);
  void perform_light_action(bool onOff);

  void request_gate_status();
  void request_light_status();

 protected:
  void handle_char_(uint8_t c);
  bool validate_message_();

  void handle_light_status_(const StatusReply *reply);
  void handle_gate_status_(const StatusReply *reply);
  void handle_gate_echo_reply(const CommandEchoReply *reply);
  void handle_light_echo_reply(const CommandEchoReply *reply);

  void handle_message_(uint16_t seq, uint16_t type, const uint8_t *buffer, uint32_t len);
  void process_command_queue_();

  void enqueue_command_(const MessageType type, const NovofermCommand &command);
  void send_command_(const TXQueueEntry &entry);

  struct Sequence {
    uint8_t counter;
    Sequence() : counter(0) {}

    uint16_t next(const Target &target) {
      counter++;
      if (counter == 0)
        counter = 1;

      return (static_cast<uint16_t>(target) << 8) | counter;
    }

    // Extract Target from a raw uint16_t sequence
    static Target extractTarget(uint16_t buf) {
      Target tgt = static_cast<Target>((buf >> 8) & 0xFF);

      // validate the target so we dont return invalid enum values
      switch (tgt) {
        case Target::GATE:
        case Target::LIGHT:
          return tgt;
        default:
          return Target::UNKNOWN;
      }
    }
  };

  Sequence seq_tx_;
  uint32_t last_command_timestamp_{0};
  uint32_t last_rx_char_timestamp_{0};
  uint32_t last_full_reply_timestamp_{0};

  uint8_t rx_buffer_[MESSAGE_SIZE + 1];
  uint8_t rx_buffer_pos_ = 0;

  uint8_t tx_buffer_[MESSAGE_SIZE + 1];
  std::deque<TXQueueEntry> command_queue_;

  CallbackManager<void()> initialized_callback_{};
  std::function<void(GateStatus)> cover_state_callback_ = nullptr;
  std::function<void(LightStatus)> light_state_callback_ = nullptr;
  std::function<void(bool)> ventilation_state_callback_ = nullptr;
};

}  // namespace novoferm
}  // namespace esphome
