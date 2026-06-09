#include "novoferm.h"
#include "esphome/core/gpio.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/util.h"

namespace esphome {
namespace novoferm {

static constexpr const char *const TAG = "novoferm";
static constexpr int const &RECEIVE_TIMEOUT = 50;
static constexpr int const &COMMAND_REPLY_TIMEOUT = 500;

void Novoferm::dump_config() {
  ESP_LOGCONFIG(TAG, "Novoferm");
  this->check_uart_settings(9600, 1, uart::UART_CONFIG_PARITY_NONE, 8);
}

void Novoferm::setup() {}

void Novoferm::request_gate_status() {
  NovofermCommand cmd;
  cmd.target = Target::GATE;
  cmd.action = 0x01;
  this->enqueue_command_(MessageType::STATUS, cmd);
}

void Novoferm::request_light_status() {
  NovofermCommand cmd;
  cmd.target = Target::LIGHT;
  cmd.action = 0x01;
  this->enqueue_command_(MessageType::STATUS, cmd);
}

void Novoferm::perform_gate_action(GateAction action) {
  NovofermCommand cmd;
  cmd.target = Target::GATE;
  cmd.action = action;
  this->enqueue_command_(MessageType::COMMAND, cmd);
  this->request_gate_status();
}

void Novoferm::perform_light_action(bool onOff) {
  NovofermCommand cmd;
  cmd.target = Target::LIGHT;
  cmd.action = onOff ? LightStatus::ON : LightStatus::OFF;
  this->enqueue_command_(MessageType::COMMAND, cmd);
  this->request_light_status();
}

void Novoferm::loop() {
  while (this->available()) {
    uint8_t c;
    if (this->read_byte(&c)) {
      this->handle_char_(c);
    }
  }
  process_command_queue_();
}

void Novoferm::handle_char_(uint8_t c) {
  if (rx_buffer_pos_ >= MESSAGE_SIZE) {
    ESP_LOGW(TAG, "Buffer Overflow, message too long!");
    rx_buffer_pos_ = 0;
  }

  // Always add the character tot he buffer
  rx_buffer_[rx_buffer_pos_++] = c;

  // Validate the message and reset buffer if invalid
  if (!validate_message_()) {
    rx_buffer_pos_ = 0;
  } else {
    last_rx_char_timestamp_ = millis();
  }
}

bool Novoferm::validate_message_() {
  uint8_t at = rx_buffer_pos_ - 1;

  auto *data = &this->rx_buffer_[0];

  // Bytes 0-1: SEQ (any) 2–5: LEN (any), Bytes 6-7 Type (validate once 7th is received)
  if (at < 7)
    return true;

  // Only validate possible types once
  uint16_t type = (uint16_t(data[6]) << 8) | uint16_t(data[7]);
  if (at == 7 && !(type == MessageType::STATUS || type == MessageType::COMMAND)) {
    ESP_LOGW(TAG, "Unexpected TYPE 0x%04X - discarding", type);
    return false;  // reset buffer
  }

  // Check for max payload size
  uint32_t len = (uint32_t(data[2]) << 24) | (uint32_t(data[3]) << 16) | (uint32_t(data[4]) << 8) | data[5];
  const auto payloadSize = len - sizeof(type);
  if (sizeof(MessageHeader) + payloadSize > MESSAGE_SIZE) {
    ESP_LOGW(TAG, "Payload size %u exceeds max allowed %u - discarding", payloadSize, MESSAGE_SIZE);
    return false;
  }

  // Wait until all payload bytes have arrived
  if (at - 7 < payloadSize) {
    return true;
  }

  uint16_t seq = (uint16_t(data[0]) << 8) | uint16_t(data[1]);
  const uint8_t *payload = data + 8;
  char payload_hex[format_hex_pretty_size(MESSAGE_SIZE) + 1];
  format_hex_pretty_to(payload_hex, payload, payloadSize);
  ESP_LOGV(TAG, "Received message: SEQ=%u TYPE=0x%04X PAYLOAD_SIZE=%u PAYLOAD=[%s]", seq, type, payloadSize,
           payload_hex);

  this->last_full_reply_timestamp_ = millis();
  this->handle_message_(seq, type, payload, payloadSize);

  // Returning false here means: reset buffer after processing.
  return false;
}

void Novoferm::handle_message_(uint16_t seq, uint16_t type, const uint8_t *buffer, uint32_t len) {
  const auto target = this->seq_tx_.extractTarget(seq);
  const auto messageType = static_cast<MessageType>(type);

  if (target == Target::GATE && type == MessageType::STATUS) {
    handle_gate_status_(reinterpret_cast<const StatusReply *>(buffer));
  } else if (target == Target::GATE && type == MessageType::COMMAND) {
    handle_gate_echo_reply(reinterpret_cast<const CommandEchoReply *>(buffer));
  } else if (target == Target::LIGHT && type == MessageType::STATUS) {
    handle_light_status_(reinterpret_cast<const StatusReply *>(buffer));
  } else if (target == Target::LIGHT && type == MessageType::COMMAND) {
    handle_light_echo_reply(reinterpret_cast<const CommandEchoReply *>(buffer));
  } else {
    ESP_LOGE(TAG, "Invalid message handled, implementation issue target=0x%02X messageType=0x%04X",
             static_cast<unsigned>(target), static_cast<unsigned>(messageType));
  }
}

void Novoferm::handle_gate_status_(const StatusReply *reply) {
  ESP_LOGD(TAG, "Received gate status: %s", reply->print(Target::GATE).c_str());
  if (this->cover_state_callback_) {
    this->cover_state_callback_(reply->gateState);
  }
  if (this->ventilation_state_callback_) {
    this->ventilation_state_callback_(reply->gateState == GateStatus::VENTILATING);
  }
}

void Novoferm::handle_gate_echo_reply(const CommandEchoReply *reply) {
  ESP_LOGD(TAG, "Received gate cmd reply: %s", reply->print().c_str());
}

void Novoferm::handle_light_echo_reply(const CommandEchoReply *reply) {
  ESP_LOGD(TAG, "Received light cmd reply: %s", reply->print().c_str());
}

void Novoferm::handle_light_status_(const StatusReply *reply) {
  ESP_LOGD(TAG, "Received light status: %s", reply->print(Target::LIGHT).c_str());
  if (this->light_state_callback_) {
    this->light_state_callback_(reply->lightState);
  }
}

void Novoferm::enqueue_command_(const MessageType type, const NovofermCommand &command) {
  if (type == MessageType::STATUS) {
    int pending_count = 0;
    for (const auto &entry : command_queue_) {
      if (entry.type == MessageType::STATUS && entry.command.target == command.target) {
        ++pending_count;
      }
    }

    if (pending_count == 1) {
      ESP_LOGV(TAG, "STATUS for target %u already queued - skipping", static_cast<unsigned>(command.target));
      return;
    } else if (pending_count > 1) {
      ESP_LOGW(TAG,
               "STATUS for target %u already queued (%d pending) - skipping this request."
               "Consider increasing the send interval if this happens often.",
               static_cast<unsigned>(command.target), pending_count);
      return;
    }
  }

  command_queue_.push_back({type, command});
  process_command_queue_();
}

void Novoferm::send_command_(const TXQueueEntry &tx) {
  uint16_t seq = this->seq_tx_.next(tx.command.target);

  uint8_t *p = this->tx_buffer_;
  const auto hdr = MessageHeader(tx.type, seq, sizeof(tx.command));
  hdr.write_to(p);
  tx.command.write_to(p);

  char tx_hex[format_hex_pretty_size(MESSAGE_SIZE) + 1];
  format_hex_pretty_to(tx_hex, this->tx_buffer_, MESSAGE_SIZE);
  ESP_LOGV(TAG, "Sending Novoferm: SEQ=%u CMD=0x%02X DATA=[%s]", seq, tx.type, tx_hex);

  // Write all at once
  this->write_array(this->tx_buffer_, MESSAGE_SIZE);
  this->flush();
}

void Novoferm::process_command_queue_() {
  const uint32_t now = millis();

  // Check if we are still waiting for a reply to the last command or timed out
  const bool waitingForReply = (last_command_timestamp_ > last_full_reply_timestamp_);

  // If no chars have been received for a while, clear RX buffer
  if (waitingForReply && (now - last_rx_char_timestamp_ > RECEIVE_TIMEOUT)) {
    ESP_LOGD(TAG, "RX buffer cleared due to inactivity");
    rx_buffer_pos_ = 0;
  }

  if (waitingForReply && (now - last_command_timestamp_ >= COMMAND_REPLY_TIMEOUT)) {
    ESP_LOGW(TAG, "Timeout waiting for valid reply to last command");
    rx_buffer_pos_ = 0;
  }

  if (command_queue_.empty() || waitingForReply) {
    return;
  }

  send_command_(command_queue_.front());
  command_queue_.pop_front();
  last_command_timestamp_ = now;
}

}  // namespace novoferm
}  // namespace esphome
