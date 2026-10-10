#include "esppac_cnt.h"
#include "esppac_commands_cnt.h"

#include "esphome/core/log.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>

namespace esphome {
namespace panasonic_ac {
namespace CNT {

static const char *const TAG = "panasonic_ac.cz_tacg1";

static climate::ClimateMode determine_mode(uint8_t mode) {
  uint8_t nib1 = (mode >> 4) & 0x0F;  // Left nib for mode
  uint8_t nib2 = (mode >> 0) & 0x0F;  // Right nib for power state

  if (nib2 == 0x00)
    return climate::CLIMATE_MODE_OFF;

  switch (nib1) {
    case 0x00:  // Auto
      return climate::CLIMATE_MODE_HEAT_COOL;
    case 0x03:  // Cool
      return climate::CLIMATE_MODE_COOL;
    case 0x04:  // Heat
      return climate::CLIMATE_MODE_HEAT;
    case 0x02:  // Dry
      return climate::CLIMATE_MODE_DRY;
    case 0x06:  // Fan only
      return climate::CLIMATE_MODE_FAN_ONLY;
    default:
      ESP_LOGW(TAG, "Received unknown climate mode");
      return climate::CLIMATE_MODE_OFF;
  }
}

static const char *determine_fan_speed(uint8_t speed) {
  switch (speed) {
    case 0xA0:  // Auto
      return "Automatic";
    case 0x30:  // 1
      return "1";
    case 0x40:  // 2
      return "2";
    case 0x50:  // 3
      return "3";
    case 0x60:  // 4
      return "4";
    case 0x70:  // 5
      return "5";
    default:
      ESP_LOGW(TAG, "Received unknown fan speed");
      return "Unknown";
  }
}

static const char *determine_vertical_swing(uint8_t swing) {
  uint8_t nib = (swing >> 4) & 0x0F;  // Left nib for vertical swing

  switch (nib) {
    case 0x0E:
      return "swing";
    case 0x0F:
      return "auto";
    case 0x01:
      return "up";
    case 0x02:
      return "up_center";
    case 0x03:
      return "center";
    case 0x04:
      return "down_center";
    case 0x05:
      return "down";
    case 0x00:
      return "unsupported";
    default:
      ESP_LOGW(TAG, "Received unknown vertical swing mode: 0x%02X", nib);
      return "Unknown";
  }
}

static const char *determine_horizontal_swing(uint8_t swing) {
  uint8_t nib = (swing >> 0) & 0x0F;  // Right nib for horizontal swing

  switch (nib) {
    case 0x0D:
      return "auto";
    case 0x09:
      return "left";
    case 0x0A:
      return "left_center";
    case 0x06:
      return "center";
    case 0x0B:
      return "right_center";
    case 0x0C:
      return "right";
    case 0x00:
      return "unsupported";
    default:
      ESP_LOGW(TAG, "Received unknown horizontal swing mode");
      return "Unknown";
  }
}

static const char *determine_preset(uint8_t preset) {
  uint8_t nib = (preset >> 0) & 0x0F;  // Right nib for preset (powerful/quiet)

  switch (nib) {
    case 0x02:
      return "Powerful";
    case 0x04:
      return "Quiet";
    case 0x00:
      return "Normal";
    default:
      ESP_LOGW(TAG, "Received unknown preset");
      return "Normal";
  }
}

static bool determine_preset_nanoex(uint8_t preset) {
  uint8_t nib = (preset >> 4) & 0x04;  // Left nib for nanoex

  if (nib == 0x04)
    return true;
  else if (nib == 0x00)
    return false;
  else {
    ESP_LOGW(TAG, "Received unknown nanoex value");
    return false;
  }
}

static bool determine_eco(uint8_t value) {
  if (value == 0x40)
    return true;
  else if (value == 0x00)
    return false;
  else {
    ESP_LOGW(TAG, "Received unknown eco value");
    return false;
  }
}

static bool determine_econavi(uint8_t value) {
  uint8_t nib = value & 0x10;

  if (nib == 0x10)
    return true;
  else if (nib == 0x00)
    return false;
  else {
    ESP_LOGW(TAG, "Received unknown econavi value");
    return false;
  }
}

static bool determine_mild_dry(uint8_t value) {
  if (value == 0x7F)
    return true;
  else if (value == 0x80)
    return false;
  else {
    ESP_LOGW(TAG, "Received unknown mild dry value");
    return false;
  }
}

uint16_t determine_power_consumption(uint8_t byte_28, uint8_t byte_29, uint8_t offset) {
  int32_t power = (int32_t) byte_28 + ((int32_t) byte_29 * 256) - offset;
  return power > 0 ? (uint16_t) power : 0;  // Never wrap around to a huge or negative value
}

void PanasonicACCNT::setup() {
  PanasonicAC::setup();

  this->last_read_ = millis();
  this->last_packet_received_ = millis();
  this->last_change_ = millis();  // Start with fast polling after boot

  ESP_LOGD(TAG, "Using CZ-TACG1 protocol via CN-CNT");
}

void PanasonicACCNT::dump_config() {
  ESP_LOGCONFIG(TAG, "Panasonic AC (CN-CNT):");
  ESP_LOGCONFIG(TAG, "  Poll interval fast: %" PRIu32 " ms", this->poll_interval_fast_);
  ESP_LOGCONFIG(TAG, "  Poll interval slow: %" PRIu32 " ms", this->poll_interval_slow_);
  ESP_LOGCONFIG(TAG, "  Fast poll window: %" PRIu32 " ms", this->fast_poll_window_);
}

void PanasonicACCNT::loop() {
  PanasonicAC::read_data();
  this->process_rx();

  // Discard an incomplete frame once the line has been quiet for too long
  if (!this->rx_buffer_.empty() && millis() - this->last_read_ > READ_TIMEOUT) {
    ESP_LOGD(TAG, "Dropping incomplete packet (%u bytes)", (unsigned) this->rx_buffer_.size());
    log_packet(this->rx_buffer_);
    this->rx_buffer_.clear();
  }

  this->check_communication();
  handle_cmd();
  handle_poll();  // Handle sending poll packets
}

/*
 * Split the receive buffer into frames using the length byte, so back-to-back
 * packets are not merged. Leading garbage is skipped until a known header.
 */
void PanasonicACCNT::process_rx() {
  while (!this->rx_buffer_.empty()) {
    if (this->rx_buffer_[0] != CTRL_HEADER && this->rx_buffer_[0] != POLL_HEADER) {
      size_t skip = 1;
      while (skip < this->rx_buffer_.size() && this->rx_buffer_[skip] != CTRL_HEADER &&
             this->rx_buffer_[skip] != POLL_HEADER)
        skip++;
      ESP_LOGW(TAG, "Dropping %u bytes without valid header", (unsigned) skip);
      this->rx_buffer_.erase(this->rx_buffer_.begin(), this->rx_buffer_.begin() + skip);
      continue;
    }

    if (this->rx_buffer_.size() < 2)
      return;  // Wait for the length byte

    const size_t len = (size_t) this->rx_buffer_[1] + 3;  // Header, length and checksum
    if (len > BUFFER_SIZE) {
      ESP_LOGW(TAG, "Dropping invalid packet (length %u)", (unsigned) len);
      this->rx_buffer_.erase(this->rx_buffer_.begin());  // Resync on next header
      continue;
    }

    if (this->rx_buffer_.size() < len)
      return;  // Wait for the rest of the frame

    this->frame_.assign(this->rx_buffer_.begin(), this->rx_buffer_.begin() + len);
    this->rx_buffer_.erase(this->rx_buffer_.begin(), this->rx_buffer_.begin() + len);
    log_packet(this->frame_);

    if (!verify_packet())
      continue;

    this->waiting_for_response_ = false;
    this->last_packet_received_ = millis();  // Set the time at which we received our last packet

    if (this->comm_lost_) {
      ESP_LOGI(TAG, "Communication with AC restored");
      this->comm_lost_ = false;
      this->has_published_ = false;  // Republish everything
      this->status_clear_warning();
    }

    handle_packet();
  }
}

/*
 * The climate entity has no deduplication in ESPHome, so only publish when
 * something visible changed instead of on every poll.
 */
void PanasonicACCNT::publish_if_changed() {
  auto same_float = [](float a, float b) { return (std::isnan(a) && std::isnan(b)) || a == b; };

  if (this->has_published_ && this->data == this->published_data_ &&
      same_float(this->current_temperature, this->published_current_temperature_) &&
      this->action == this->published_action_)
    return;

  this->published_data_ = this->data;
  this->published_current_temperature_ = this->current_temperature;
  this->published_action_ = this->action;
  this->has_published_ = true;
  this->publish_state();
}

/*
 * Flag a warning and invalidate sensor values when the AC stops answering
 */
void PanasonicACCNT::check_communication() {
  if (this->comm_lost_)
    return;

  const uint32_t timeout = 3 * std::max(this->poll_interval_fast_, this->poll_interval_slow_) + 2000;
  if (millis() - this->last_packet_received_ <= timeout)
    return;

  ESP_LOGW(TAG, "No valid response from AC for %" PRIu32 " ms", timeout);
  this->comm_lost_ = true;
  this->status_set_warning();

  if (this->current_temperature_sensor_ == nullptr)
    this->current_temperature = NAN;
  if (this->outside_temperature_sensor_ != nullptr)
    this->outside_temperature_sensor_->publish_state(NAN);
  if (this->current_power_consumption_sensor_ != nullptr)
    this->current_power_consumption_sensor_->publish_state(NAN);
  this->publish_state();
}

/*
 * ESPHome control request
 */

void PanasonicACCNT::control(const climate::ClimateCall &call) {
  if (this->state_ != ACState::Ready)
    return;

  this->prepare_cmd();

  if (call.get_mode().has_value()) {
    ESP_LOGV(TAG, "Requested mode change");

    switch (*call.get_mode()) {
      case climate::CLIMATE_MODE_COOL:
        this->cmd[0] = 0x34;
        break;
      case climate::CLIMATE_MODE_HEAT:
        this->cmd[0] = 0x44;
        break;
      case climate::CLIMATE_MODE_DRY:
        this->cmd[0] = 0x24;
        break;
      case climate::CLIMATE_MODE_HEAT_COOL:
        this->cmd[0] = 0x04;
        break;
      case climate::CLIMATE_MODE_FAN_ONLY:
        this->cmd[0] = 0x64;
        break;
      case climate::CLIMATE_MODE_OFF:
        this->cmd[0] = this->cmd[0] & 0xF0;  // Strip right nib to turn AC off
        break;
      default:
        ESP_LOGV(TAG, "Unsupported mode requested");
        break;
    }
  }

  if (call.get_target_temperature().has_value()) {
    ESP_LOGV(TAG, "Requested target temp change to %.2f, %.2f including offset", *call.get_target_temperature(), *call.get_target_temperature() - this->current_temperature_offset_);
    this->cmd[1] = (*call.get_target_temperature() - this->current_temperature_offset_) / TEMPERATURE_STEP;
  }

  if (call.has_custom_fan_mode()) {
    ESP_LOGV(TAG, "Requested fan mode change");

    if (this->get_custom_preset() != "Normal") {
      ESP_LOGV(TAG, "Resetting preset");
      this->cmd[5] = (this->cmd[5] & 0xF0);  // Clear right nib for normal mode
    }

    const auto fanMode = call.get_custom_fan_mode();

    if (fanMode == "Automatic")
      this->cmd[3] = 0xA0;
    else if (fanMode == "1")
      this->cmd[3] = 0x30;
    else if (fanMode == "2")
      this->cmd[3] = 0x40;
    else if (fanMode == "3")
      this->cmd[3] = 0x50;
    else if (fanMode == "4")
      this->cmd[3] = 0x60;
    else if (fanMode == "5")
      this->cmd[3] = 0x70;
    else
      ESP_LOGV(TAG, "Unsupported fan mode requested");
  }

  if (call.get_swing_mode().has_value()) {
    ESP_LOGV(TAG, "Requested swing mode change");

    switch (*call.get_swing_mode()) {
      case climate::CLIMATE_SWING_BOTH:
        this->cmd[4] = 0xFD;
        break;
      case climate::CLIMATE_SWING_OFF:
        this->cmd[4] = 0x36;  // Reset both to center
        break;
      case climate::CLIMATE_SWING_VERTICAL:
        this->cmd[4] = 0xF6;  // Swing vertical, horizontal center
        break;
      case climate::CLIMATE_SWING_HORIZONTAL:
        this->cmd[4] = 0x3D;  // Swing horizontal, vertical center
        break;
      default:
        ESP_LOGV(TAG, "Unsupported swing mode requested");
        break;
    }
  }

  if (call.has_custom_preset()) {
    ESP_LOGV(TAG, "Requested preset change");

    const auto preset = call.get_custom_preset();

    if (preset == "Normal")
      this->cmd[5] = (this->cmd[5] & 0xF0);  // Clear right nib for normal mode
    else if (preset == "Powerful")
      this->cmd[5] = (this->cmd[5] & 0xF0) + 0x02;  // Clear right nib and set powerful mode
    else if (preset == "Quiet")
      this->cmd[5] = (this->cmd[5] & 0xF0) + 0x04;  // Clear right nib and set quiet mode
    else
      ESP_LOGV(TAG, "Unsupported preset requested");
  }
}

/*
 * Set the data array to the fields
 */
void PanasonicACCNT::set_data(bool set) {
  this->mode = determine_mode(this->data[0]);
  this->set_custom_fan_mode_(determine_fan_speed(this->data[3]));

  StringRef verticalSwing(determine_vertical_swing(this->data[4]));
  StringRef horizontalSwing(determine_horizontal_swing(this->data[4]));

  const char *preset = determine_preset(this->data[5]);
  bool nanoex = determine_preset_nanoex(this->data[5]);
  bool eco = determine_eco(this->data[8]);
  bool econavi = determine_econavi(this->data[5]);
  bool mildDry = determine_mild_dry(this->data[2]);

  this->update_target_temperature((int8_t) this->data[1]);

  if (set) {
    // Payload byte i exists only if it is not the trailing checksum
    auto has = [this](size_t i) { return i + 1 < this->frame_.size(); };

    // Also set current and outside temperature
    // 128 means not supported
    if (this->current_temperature_sensor_ == nullptr) {
      if (has(18) && this->frame_[18] != 0x80)
        this->update_current_temperature((int8_t) this->frame_[18]);
      else if (has(21) && this->frame_[21] != 0x80)
        this->update_current_temperature((int8_t) this->frame_[21]);
      else
        ESP_LOGV(TAG, "Current temperature is not supported");
    }

    if (this->outside_temperature_sensor_ != nullptr) {
      if (has(19) && this->frame_[19] != 0x80)
        this->update_outside_temperature((int8_t) this->frame_[19]);
      else if (has(22) && this->frame_[22] != 0x80)
        this->update_outside_temperature((int8_t) this->frame_[22]);
      else
        ESP_LOGV(TAG, "Outside temperature is not supported");
    }

    // Power is parsed even without a sensor, it is also used for the action
    if (has(30)) {
      this->power_consumption_ =
          determine_power_consumption(this->frame_[28], this->frame_[29], this->frame_[30]);
      this->update_current_power_consumption(this->power_consumption_);
    } else {
      this->power_consumption_ = -1;
      ESP_LOGV(TAG, "Power consumption is not supported");
    }

    if (this->defrost_sensor_ != nullptr) {
      if (has(14)) {
        bool defrost = (this->frame_[14] == 0x02);
        update_defrost(defrost);
      } else {
        ESP_LOGV(TAG, "Defrost status is not supported");
      }
    }
  }

  // "swing" (0xE) moves the vertical vanes just like "auto"
  const bool vertical_moving = verticalSwing == "auto" || verticalSwing == "swing";
  const bool horizontal_moving = horizontalSwing == "auto";

  if (vertical_moving && horizontal_moving)
    this->swing_mode = climate::CLIMATE_SWING_BOTH;
  else if (vertical_moving)
    this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
  else if (horizontal_moving)
    this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
  else
    this->swing_mode = climate::CLIMATE_SWING_OFF;

  this->update_swing_vertical(verticalSwing);
  this->update_swing_horizontal(horizontalSwing);

  this->set_custom_preset_(preset);

  this->update_nanoex(nanoex);
  this->update_eco(eco);
  this->update_econavi(econavi);
  this->update_mild_dry(mildDry);
  this->action = this->determine_action();

  // determine_action() only compares temperatures. When the AC reports no power
  // draw at all the compressor is not running, so heating/cooling is idle.
  if (this->power_consumption_ == 0 &&
      (this->action == climate::CLIMATE_ACTION_HEATING || this->action == climate::CLIMATE_ACTION_COOLING))
    this->action = climate::CLIMATE_ACTION_IDLE;
}

/*
 * Send a command, attaching header, packet length and checksum
 */
void PanasonicACCNT::send_command(std::vector<uint8_t> command, CommandType type, uint8_t header = CNT::CTRL_HEADER) {
  uint8_t length = command.size();
  command.insert(command.begin(), header);
  command.insert(command.begin() + 1, length);

  uint8_t checksum = 0;

  for (uint8_t i : command)
    checksum -= i;  // Add to checksum

  command.push_back(checksum);

  send_packet(command, type);  // Actually send the constructed packet
}

/*
 * Send a raw packet, as is
 */
void PanasonicACCNT::send_packet(const std::vector<uint8_t> &packet, CommandType type) {
  this->last_packet_sent_ = millis();  // Save the time when we sent the last packet

  if (type != CommandType::Response)     // Don't wait for a response for responses
    this->waiting_for_response_ = true;  // Mark that we are waiting for a response

  write_array(packet);       // Write to UART
  log_packet(packet, true);  // Write to log
}

/*
 * Loop handling
 */

void PanasonicACCNT::handle_poll() {
  const uint32_t now = millis();
  // Poll fast during initialization and for a while after a change, slow otherwise
  const bool fast = this->state_ != ACState::Ready || now - this->last_change_ < this->fast_poll_window_;
  const uint32_t interval = fast ? this->poll_interval_fast_ : this->poll_interval_slow_;

  if (now - this->last_packet_sent_ > interval) {
    ESP_LOGV(TAG, "Polling AC (%s, %" PRIu32 " ms)", fast ? "fast" : "slow", interval);
    send_command(CMD_POLL, CommandType::Normal, POLL_HEADER);
  }
}

/*
 * Start building a command from the last known AC state. The command is rebased on fresh
 * data before it is sent, so changes made meanwhile (e.g. IR remote) are not overwritten.
 */
void PanasonicACCNT::prepare_cmd() {
  if (!this->cmd.empty())
    return;

  ESP_LOGV(TAG, "Copying data to cmd");
  this->cmd = this->data;
  this->cmd_base_ = this->data;
  this->cmd_needs_refresh_ = true;
  this->cmd_refresh_poll_sent_ = false;
}

/*
 * Protocol fields per data byte as bit masks. A field changed by ESPHome/HA is taken from cmd as a
 * whole, all other fields from the latest AC state. Merging per field (not per bit) keeps numeric
 * values such as the target temperature intact.
 */
static const std::vector<uint8_t> CMD_FIELDS[10] = {
    {0xF0, 0x0F},                    // 0: mode | power
    {0xFF},                          // 1: target temperature
    {0xFF},                          // 2: mild dry
    {0xFF},                          // 3: fan speed
    {0xF0, 0x0F},                    // 4: vertical swing | horizontal swing
    {0x0F, 0x10, 0x40, 0x20, 0x80},  // 5: preset | econavi | nanoex | unknown bits
    {0xFF},                          // 6
    {0xFF},                          // 7
    {0xFF},                          // 8: eco
    {0xFF},                          // 9
};

void PanasonicACCNT::rebase_cmd() {
  for (size_t i = 0; i < this->cmd.size() && i < 10; i++) {
    uint8_t merged = 0;
    for (uint8_t mask : CMD_FIELDS[i]) {
      bool changed = ((this->cmd[i] ^ this->cmd_base_[i]) & mask) != 0;
      merged |= (changed ? this->cmd[i] : this->data[i]) & mask;
    }
    this->cmd[i] = merged;
  }
  this->cmd_needs_refresh_ = false;
  ESP_LOGV(TAG, "Command rebased on fresh AC state");
}

void PanasonicACCNT::handle_cmd() {
  if (this->cmd.empty() || millis() - this->last_packet_sent_ <= CMD_INTERVAL)
    return;

  if (this->cmd_needs_refresh_) {
    if (!this->cmd_refresh_poll_sent_) {
      ESP_LOGV(TAG, "Polling AC before sending command");
      send_command(CMD_POLL, CommandType::Normal, POLL_HEADER);
      this->cmd_refresh_poll_sent_ = true;
      this->cmd_refresh_poll_time_ = millis();
      return;
    }
    if (millis() - this->cmd_refresh_poll_time_ < CMD_REFRESH_TIMEOUT)
      return;  // Wait for the poll response

    ESP_LOGW(TAG, "No poll response, sending command based on possibly stale state");
    this->cmd_needs_refresh_ = false;
  }

  ESP_LOGV(TAG, "Sending Command");
  send_command(this->cmd, CommandType::Normal, CTRL_HEADER);
  this->cmd.clear();
  this->last_change_ = millis();  // Poll fast to confirm the new state
}

/*
 * Packet handling
 */

bool PanasonicACCNT::verify_packet() {
  // Header and length were checked while framing
  uint8_t checksum = 0;

  for (uint8_t b : this->frame_) {
    checksum += b;
  }

  if (checksum != 0) {
    ESP_LOGD(TAG, "Dropping invalid packet (checksum)");
    return false;
  }

  return true;
}

void PanasonicACCNT::handle_packet() {
  if (this->frame_[0] == POLL_HEADER) {
    if (this->frame_.size() < 13) {  // Header, length, 10 data bytes and checksum
      ESP_LOGW(TAG, "Dropping too short poll response (%u bytes)", (unsigned) this->frame_.size());
      return;
    }

    std::vector<uint8_t> new_data(this->frame_.begin() + 2, this->frame_.begin() + 12);

    if (this->state_ == ACState::Ready && new_data != this->data) {
      ESP_LOGV(TAG, "AC state changed, switching to fast polling");
      this->last_change_ = millis();  // e.g. IR remote used, follow up quickly
    }

    this->data = new_data;

    if (!this->cmd.empty()) {
      if (this->cmd_needs_refresh_)
        this->rebase_cmd();
      // Don't publish while a command is pending, it would briefly revert the
      // optimistic state of switches/selects in HA. The next poll publishes.
      return;
    }

    this->set_data(true);
    this->publish_if_changed();

    if (this->state_ != ACState::Ready)
      this->state_ = ACState::Ready;  // Mark as ready after first poll
  } else {
    ESP_LOGD(TAG, "Received unknown packet");
  }
}

/*
 * Sensor handling
 */

void PanasonicACCNT::on_vertical_swing_change(const StringRef &swing) {
  if (this->state_ != ACState::Ready)
    return;

  ESP_LOGD(TAG, "Setting vertical swing position");

  this->prepare_cmd();

  if (swing == "down")
    this->cmd[4] = (this->cmd[4] & 0x0F) + 0x50;
  else if (swing == "down_center")
    this->cmd[4] = (this->cmd[4] & 0x0F) + 0x40;
  else if (swing == "center")
    this->cmd[4] = (this->cmd[4] & 0x0F) + 0x30;
  else if (swing == "up_center")
    this->cmd[4] = (this->cmd[4] & 0x0F) + 0x20;
  else if (swing == "up")
    this->cmd[4] = (this->cmd[4] & 0x0F) + 0x10;
  else if (swing == "swing")
    this->cmd[4] = (this->cmd[4] & 0x0F) + 0xE0;
  else if (swing == "auto")
    this->cmd[4] = (this->cmd[4] & 0x0F) + 0xF0;
  else {
    ESP_LOGW(TAG, "Unsupported vertical swing position received");
    return;
  }
}

void PanasonicACCNT::on_horizontal_swing_change(const StringRef &swing) {
  if (this->state_ != ACState::Ready)
    return;

  ESP_LOGD(TAG, "Setting horizontal swing position");

  this->prepare_cmd();

  if (swing == "left")
    this->cmd[4] = (this->cmd[4] & 0xF0) + 0x09;
  else if (swing == "left_center")
    this->cmd[4] = (this->cmd[4] & 0xF0) + 0x0A;
  else if (swing == "center")
    this->cmd[4] = (this->cmd[4] & 0xF0) + 0x06;
  else if (swing == "right_center")
    this->cmd[4] = (this->cmd[4] & 0xF0) + 0x0B;
  else if (swing == "right")
    this->cmd[4] = (this->cmd[4] & 0xF0) + 0x0C;
  else if (swing == "auto")
    this->cmd[4] = (this->cmd[4] & 0xF0) + 0x0D;
  else {
    ESP_LOGW(TAG, "Unsupported horizontal swing position received");
    return;
  }
}

void PanasonicACCNT::on_nanoex_change(bool state) {
  if (this->state_ != ACState::Ready)
    return;

  this->prepare_cmd();

  this->nanoex_state_ = state;

  if (state) {
    ESP_LOGV(TAG, "Turning nanoex on");
    this->cmd[5] |= 0x40;  // Only touch the nanoex bit
  } else {
    ESP_LOGV(TAG, "Turning nanoex off");
    this->cmd[5] &= ~0x40;
  }
}

void PanasonicACCNT::on_eco_change(bool state) {
  if (this->state_ != ACState::Ready)
    return;

  this->prepare_cmd();

  this->eco_state_ = state;

  if (state) {
    ESP_LOGV(TAG, "Turning eco mode on");
    this->cmd[8] = 0x40;
  } else {
    ESP_LOGV(TAG, "Turning eco mode off");
    this->cmd[8] = 0x00;
  }
}

void PanasonicACCNT::on_econavi_change(bool state) {
  if (this->state_ != ACState::Ready)
    return;

  this->prepare_cmd();

  this->econavi_state_ = state;

  if (state) {
    ESP_LOGV(TAG, "Turning econavi mode on");
    this->cmd[5] |= 0x10;  // Only touch the econavi bit, keep preset and nanoex
  } else {
    ESP_LOGV(TAG, "Turning econavi mode off");
    this->cmd[5] &= ~0x10;
  }
}

void PanasonicACCNT::on_mild_dry_change(bool state) {
  if (this->state_ != ACState::Ready)
    return;

  this->prepare_cmd();

  this->mild_dry_state_ = state;

  if (state) {
    ESP_LOGV(TAG, "Turning mild dry on");
    this->cmd[2] = 0x7F;
  } else {
    ESP_LOGV(TAG, "Turning mild dry off");
    this->cmd[2] = 0x80;
  }
}

}  // namespace CNT
}  // namespace panasonic_ac
}  // namespace esphome
