from esphome.const import (
    CONF_TYPE,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_POWER,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_WATT,
)
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, climate, sensor, select, switch, binary_sensor

AUTO_LOAD = ["switch", "sensor", "select", "binary_sensor"]
DEPENDENCIES = ["uart"]

panasonic_ac_ns = cg.esphome_ns.namespace("panasonic_ac")
PanasonicAC = panasonic_ac_ns.class_(
    "PanasonicAC", cg.Component, uart.UARTDevice, climate.Climate
)
panasonic_ac_cnt_ns = panasonic_ac_ns.namespace("CNT")
PanasonicACCNT = panasonic_ac_cnt_ns.class_("PanasonicACCNT", PanasonicAC)
panasonic_ac_wlan_ns = panasonic_ac_ns.namespace("WLAN")
PanasonicACWLAN = panasonic_ac_wlan_ns.class_("PanasonicACWLAN", PanasonicAC)

PanasonicACSwitch = panasonic_ac_ns.class_(
    "PanasonicACSwitch", switch.Switch, cg.Component
)
PanasonicACSelect = panasonic_ac_ns.class_(
    "PanasonicACSelect", select.Select, cg.Component
)


CONF_HORIZONTAL_SWING_SELECT = "horizontal_swing_select"
CONF_VERTICAL_SWING_SELECT = "vertical_swing_select"
CONF_OUTSIDE_TEMPERATURE = "outside_temperature"
CONF_OUTSIDE_TEMPERATURE_OFFSET = "outside_temperature_offset"
CONF_CURRENT_TEMPERATURE_SENSOR = "current_temperature_sensor"
CONF_CURRENT_TEMPERATURE_OFFSET = "current_temperature_offset"
CONF_NANOEX_SWITCH = "nanoex_switch"
CONF_ECO_SWITCH = "eco_switch"
CONF_ECONAVI_SWITCH = "econavi_switch"
CONF_MILD_DRY_SWITCH = "mild_dry_switch"
CONF_CURRENT_POWER_CONSUMPTION = "current_power_consumption"
CONF_DEFROST_SENSOR = "defrost_sensor"
CONF_POLL_INTERVAL_FAST = "poll_interval_fast"
CONF_POLL_INTERVAL_SLOW = "poll_interval_slow"
CONF_FAST_POLL_WINDOW = "fast_poll_window"
CONF_WLAN = "wlan"
CONF_CNT = "cnt"

HORIZONTAL_SWING_OPTIONS = ["auto", "left", "left_center", "center", "right_center", "right"]

VERTICAL_SWING_OPTIONS = ["swing", "auto", "up", "up_center", "center", "down_center", "down"]

# Swing stepped by ESPHome through fixed positions (CN-CNT only)
SIMULATED_SWING = "simulated_swing"
CONF_VERTICAL_SWING_INTERVAL = "vertical_swing_interval"
CONF_HORIZONTAL_SWING_INTERVAL = "horizontal_swing_interval"
CONF_VERTICAL_SWING_POSITIONS = "vertical_swing_positions"
CONF_HORIZONTAL_SWING_POSITIONS = "horizontal_swing_positions"
CONF_CLIMATE_SWING = "climate_swing"

# Fixed vane positions and their protocol nibble
VERTICAL_POSITION_CODES = {"up": 0x1, "up_center": 0x2, "center": 0x3, "down_center": 0x4, "down": 0x5}
HORIZONTAL_POSITION_CODES = {
    "left": 0x9,
    "left_center": 0xA,
    "center": 0x6,
    "right_center": 0xB,
    "right": 0xC,
}


def swing_positions(codes):
    def validator(value):
        value = cv.ensure_list(cv.one_of(*codes, lower=True))(value)
        if len(value) < 2:
            raise cv.Invalid("At least 2 positions are needed to swing")
        if len(set(value)) != len(value):
            raise cv.Invalid("Positions must be unique")
        return value

    return validator


SWING_INTERVAL = cv.All(
    cv.positive_time_period_milliseconds,
    cv.Range(min=cv.TimePeriod(seconds=2), max=cv.TimePeriod(minutes=10)),
)

SWITCH_SCHEMA = switch.switch_schema(PanasonicACSwitch).extend(cv.COMPONENT_SCHEMA)

SELECT_SCHEMA = select.select_schema(PanasonicACSelect)

PANASONIC_COMMON_SCHEMA = {
    cv.Optional(CONF_HORIZONTAL_SWING_SELECT): SELECT_SCHEMA,
    cv.Optional(CONF_VERTICAL_SWING_SELECT): SELECT_SCHEMA,
    cv.Optional(CONF_OUTSIDE_TEMPERATURE): sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    cv.Optional(CONF_DEFROST_SENSOR): binary_sensor.binary_sensor_schema(),
    cv.Optional(CONF_NANOEX_SWITCH): SWITCH_SCHEMA,
    cv.Optional(CONF_OUTSIDE_TEMPERATURE_OFFSET): cv.int_range(min=-15, max=15),
    cv.Optional(CONF_CURRENT_TEMPERATURE_OFFSET): cv.int_range(min=-15, max=15),
}

PANASONIC_CNT_SCHEMA = {
    cv.Optional(CONF_ECO_SWITCH): SWITCH_SCHEMA,
    cv.Optional(CONF_ECONAVI_SWITCH): SWITCH_SCHEMA,
    cv.Optional(CONF_MILD_DRY_SWITCH): SWITCH_SCHEMA,
    cv.Optional(CONF_CURRENT_TEMPERATURE_SENSOR): cv.use_id(sensor.Sensor),
    cv.Optional(CONF_CURRENT_POWER_CONSUMPTION): sensor.sensor_schema(
        unit_of_measurement=UNIT_WATT,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_POWER,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    cv.Optional(CONF_POLL_INTERVAL_FAST, default="5s"): cv.All(
        cv.positive_time_period_milliseconds,
        cv.Range(min=cv.TimePeriod(milliseconds=1000)),
    ),
    cv.Optional(CONF_POLL_INTERVAL_SLOW): cv.All(  # defaults to poll_interval_fast
        cv.positive_time_period_milliseconds,
        cv.Range(min=cv.TimePeriod(milliseconds=1000), max=cv.TimePeriod(minutes=10)),
    ),
    cv.Optional(CONF_FAST_POLL_WINDOW, default="60s"): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_VERTICAL_SWING_INTERVAL, default="10s"): SWING_INTERVAL,
    cv.Optional(CONF_HORIZONTAL_SWING_INTERVAL, default="10s"): SWING_INTERVAL,
    cv.Optional(
        CONF_VERTICAL_SWING_POSITIONS, default=list(VERTICAL_POSITION_CODES)
    ): swing_positions(VERTICAL_POSITION_CODES),
    cv.Optional(
        CONF_HORIZONTAL_SWING_POSITIONS, default=list(HORIZONTAL_POSITION_CODES)
    ): swing_positions(HORIZONTAL_POSITION_CODES),
    cv.Optional(CONF_CLIMATE_SWING, default="native"): cv.one_of("native", "simulated", lower=True),
}


def validate_poll_intervals(config):
    fast = config.get(CONF_POLL_INTERVAL_FAST)
    slow = config.get(CONF_POLL_INTERVAL_SLOW)
    if fast is not None and slow is not None and slow < fast:
        raise cv.Invalid(
            f"{CONF_POLL_INTERVAL_SLOW} must be greater than or equal to {CONF_POLL_INTERVAL_FAST}"
        )
    return config


CONFIG_SCHEMA = cv.typed_schema(
    {
        CONF_WLAN: climate.climate_schema(PanasonicACWLAN).extend(PANASONIC_COMMON_SCHEMA).extend(uart.UART_DEVICE_SCHEMA),
        CONF_CNT: climate.climate_schema(PanasonicACCNT).extend(PANASONIC_COMMON_SCHEMA).extend(PANASONIC_CNT_SCHEMA).extend(uart.UART_DEVICE_SCHEMA),
    }
)
CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, validate_poll_intervals)

# Both CN-CNT and CN-WLAN run at 9600 baud 8E1, catch a mismatching uart: block
FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "panasonic_ac",
    baud_rate=9600,
    data_bits=8,
    parity="EVEN",
    stop_bits=1,
)


async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_HORIZONTAL_SWING_SELECT in config:
        conf = config[CONF_HORIZONTAL_SWING_SELECT]
        options = HORIZONTAL_SWING_OPTIONS + ([SIMULATED_SWING] if config[CONF_TYPE] == CONF_CNT else [])
        swing_select = await select.new_select(conf, options=options)
        await cg.register_component(swing_select, conf)
        cg.add(var.set_horizontal_swing_select(swing_select))

    if CONF_VERTICAL_SWING_SELECT in config:
        conf = config[CONF_VERTICAL_SWING_SELECT]
        options = VERTICAL_SWING_OPTIONS + ([SIMULATED_SWING] if config[CONF_TYPE] == CONF_CNT else [])
        swing_select = await select.new_select(conf, options=options)
        await cg.register_component(swing_select, conf)
        cg.add(var.set_vertical_swing_select(swing_select))

    if CONF_OUTSIDE_TEMPERATURE in config:
        sens = await sensor.new_sensor(config[CONF_OUTSIDE_TEMPERATURE])
        cg.add(var.set_outside_temperature_sensor(sens))

    if CONF_DEFROST_SENSOR in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_DEFROST_SENSOR])
        cg.add(var.set_defrost_sensor(sens))

    if CONF_OUTSIDE_TEMPERATURE_OFFSET in config:
        cg.add(var.set_outside_temperature_offset(config[CONF_OUTSIDE_TEMPERATURE_OFFSET]))

    for s in [CONF_ECO_SWITCH, CONF_NANOEX_SWITCH, CONF_MILD_DRY_SWITCH, CONF_ECONAVI_SWITCH]:
        if s in config:
            conf = config[s]
            a_switch = await switch.new_switch(conf)
            await cg.register_component(a_switch, conf)
            cg.add(getattr(var, f"set_{s}")(a_switch))

    if CONF_CURRENT_TEMPERATURE_SENSOR in config:
        sens = await cg.get_variable(config[CONF_CURRENT_TEMPERATURE_SENSOR])
        cg.add(var.set_current_temperature_sensor(sens))

    if CONF_CURRENT_TEMPERATURE_OFFSET in config:
        cg.add(var.set_current_temperature_offset(config[CONF_CURRENT_TEMPERATURE_OFFSET]))

    if CONF_CURRENT_POWER_CONSUMPTION in config:
        sens = await sensor.new_sensor(config[CONF_CURRENT_POWER_CONSUMPTION])
        cg.add(var.set_current_power_consumption_sensor(sens))

    if CONF_POLL_INTERVAL_FAST in config:
        cg.add(var.set_poll_interval_fast(config[CONF_POLL_INTERVAL_FAST]))
        # Without an explicit slow interval, poll at a constant rate (legacy behaviour)
        cg.add(
            var.set_poll_interval_slow(
                config.get(CONF_POLL_INTERVAL_SLOW, config[CONF_POLL_INTERVAL_FAST])
            )
        )
    if CONF_FAST_POLL_WINDOW in config:
        cg.add(var.set_fast_poll_window(config[CONF_FAST_POLL_WINDOW]))

    if config[CONF_TYPE] == CONF_CNT:
        cg.add(var.set_vertical_swing_interval(config[CONF_VERTICAL_SWING_INTERVAL]))
        cg.add(var.set_horizontal_swing_interval(config[CONF_HORIZONTAL_SWING_INTERVAL]))
        cg.add(
            var.set_vertical_swing_positions(
                [VERTICAL_POSITION_CODES[p] for p in config[CONF_VERTICAL_SWING_POSITIONS]]
            )
        )
        cg.add(
            var.set_horizontal_swing_positions(
                [HORIZONTAL_POSITION_CODES[p] for p in config[CONF_HORIZONTAL_SWING_POSITIONS]]
            )
        )
        cg.add(var.set_climate_swing_simulated(config[CONF_CLIMATE_SWING] == "simulated"))
