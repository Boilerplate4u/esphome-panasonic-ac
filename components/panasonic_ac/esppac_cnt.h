#include "esphome/components/climate/climate.h"
#include "esphome/components/climate/climate_mode.h"
#include "esppac.h"
#include "esphome/core/preferences.h"

#include <cmath>

namespace esphome {
namespace panasonic_ac {
namespace CNT {

static const uint8_t CTRL_HEADER = 0xF0;  // The header for control frames
static const uint8_t POLL_HEADER = 0x70;  // The header for the poll command

// Defaults, overridable from YAML (poll_interval_fast, poll_interval_slow, fast_poll_window)
static const uint32_t DEFAULT_POLL_INTERVAL_FAST = 5000;  // Poll interval right after a change
static const uint32_t DEFAULT_POLL_INTERVAL_SLOW = 5000;  // Poll interval when idle (same as fast = legacy behaviour)
static const uint32_t DEFAULT_FAST_POLL_WINDOW = 60000;   // How long fast polling lasts after a change
static const int CMD_INTERVAL = 250;                       // The interval at which to send commands
static const uint32_t CMD_REFRESH_TIMEOUT = 1000;          // Max wait for a fresh poll before sending a command
static const uint32_t DEFAULT_SWING_INTERVAL = 10000;     // Time between vane steps in simulated swing

static const char *const SIMULATED_SWING = "simulated_swing";  // Select option for the simulated swing

/*
 * Swing driven by ESPHome: steps the vane back and forth through fixed positions.
 * Works in every operating mode, unlike the AC's own auto airflow direction.
 */
struct SimulatedSwing {
  bool active{false};
  std::vector<uint8_t> positions;   // Nibble codes in stepping order
  uint32_t interval{DEFAULT_SWING_INTERVAL};
  size_t index{0};
  int8_t direction{1};
  uint32_t last_step{0};
  uint8_t restore{0};               // Nibble to return to when stopped
  uint8_t commanded{0};             // Last nibble sent by the simulation
  uint8_t previous{0};              // Nibble sent before that
  bool needs_init{false};           // (Re)initialize from the current AC state once it is known
};

struct SwingPrefs {  // Persisted so the simulation resumes after a restart
  uint8_t vertical_active;
  uint8_t horizontal_active;
  uint8_t vertical_restore;
  uint8_t horizontal_restore;
} __attribute__((packed));

enum class ACState {
  Initializing,  // Before first query response is receive
  Ready,         // All done, ready to receive regular packets
};

class PanasonicACCNT : public PanasonicAC {
 public:
  void control(const climate::ClimateCall &call) override;

  void on_horizontal_swing_change(const StringRef &swing) override;
  void on_vertical_swing_change(const StringRef &swing) override;
  void on_nanoex_change(bool nanoex) override;
  void on_eco_change(bool eco) override;
  void on_econavi_change(bool eco) override;
  void on_mild_dry_change(bool mild_dry) override;

  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_poll_interval_fast(uint32_t ms) { this->poll_interval_fast_ = ms; }
  void set_poll_interval_slow(uint32_t ms) { this->poll_interval_slow_ = ms; }
  void set_fast_poll_window(uint32_t ms) { this->fast_poll_window_ = ms; }

  void set_vertical_swing_interval(uint32_t ms) { this->vswing_.interval = ms; }
  void set_horizontal_swing_interval(uint32_t ms) { this->hswing_.interval = ms; }
  void set_vertical_swing_positions(const std::vector<uint8_t> &codes) { this->vswing_.positions = codes; }
  void set_horizontal_swing_positions(const std::vector<uint8_t> &codes) { this->hswing_.positions = codes; }
  void set_climate_swing_simulated(bool simulated) { this->climate_swing_simulated_ = simulated; }

 protected:
  ACState state_ = ACState::Initializing;  // Stores the internal state of the AC, used during initialization

  uint32_t poll_interval_fast_{DEFAULT_POLL_INTERVAL_FAST};
  uint32_t poll_interval_slow_{DEFAULT_POLL_INTERVAL_SLOW};
  uint32_t fast_poll_window_{DEFAULT_FAST_POLL_WINDOW};
  uint32_t last_change_{0};  // Last time a command was sent or the AC state changed (e.g. IR remote)

  // uint8_t data[10];
  std::vector<uint8_t> data = std::vector<uint8_t>(10);  // Stores the data received from the AC
  std::vector<uint8_t> cmd;                              // Used to build next command
  std::vector<uint8_t> cmd_force_;                       // Bits always taken from the command when rebasing
  bool cmd_needs_refresh_{false};                        // Pending command must be rebased on fresh data
  bool cmd_refresh_poll_sent_{false};
  uint32_t cmd_refresh_poll_time_{0};

  std::vector<uint8_t> frame_;  // Current complete frame taken from the receive buffer
  bool comm_lost_{false};       // No valid response from the AC for too long
  int32_t power_consumption_{-1};  // Last estimated power in W, -1 if unknown

  bool has_published_{false};             // Climate state was published at least once
  std::vector<uint8_t> published_data_;   // Last published settings bytes
  float published_current_temperature_{NAN};
  climate::ClimateAction published_action_{climate::CLIMATE_ACTION_OFF};

  SimulatedSwing vswing_;  // Vertical vane, high nibble of data[4]
  SimulatedSwing hswing_;  // Horizontal vane, low nibble of data[4]
  bool climate_swing_simulated_{false};
  bool cmd_from_user_{false};  // Pending command contains a change requested by the user
  ESPPreferenceObject swing_pref_;
  bool swing_restored_{false};

  void start_swing(SimulatedSwing &swing, bool vertical);
  void stop_swing(SimulatedSwing &swing, bool vertical, bool restore);
  void handle_swing(SimulatedSwing &swing, bool vertical);
  bool check_swing_override(SimulatedSwing &swing, bool vertical);
  void save_swing_prefs();
  void set_field(size_t index, uint8_t value, uint8_t mask = 0xFF);
  void set_vane(uint8_t nibble, bool vertical);
  static uint8_t get_nibble(uint8_t value, bool vertical) { return vertical ? value >> 4 : value & 0x0F; }
  static uint8_t set_nibble(uint8_t value, uint8_t nibble, bool vertical) {
    return vertical ? (uint8_t) ((value & 0x0F) | (nibble << 4)) : (uint8_t) ((value & 0xF0) | (nibble & 0x0F));
  }

  void process_rx();
  void check_communication();
  void publish_if_changed();
  void handle_poll();
  void handle_cmd();
  void prepare_cmd(bool from_user = true);
  void rebase_cmd();

  void set_data(bool set);

  void send_command(std::vector<uint8_t> command, CommandType type, uint8_t header);
  void send_packet(const std::vector<uint8_t> &command, CommandType type);

  bool verify_packet();
  void handle_packet();
};

}  // namespace CNT
}  // namespace panasonic_ac
}  // namespace esphome
