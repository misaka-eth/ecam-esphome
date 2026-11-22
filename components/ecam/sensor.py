import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import STATE_CLASS_TOTAL_INCREASING

from . import Ecam

DEPENDENCIES = ["ecam"]

CONF_ECAM_ID = "ecam_id"
CONF_COUNTER = "counter"

# A statistics counter (read live as PAR 95 0f): 3000-3003 categories, 3003 + drink id per drink (docs/protocol.md).
CONFIG_SCHEMA = sensor.sensor_schema(
    icon="mdi:counter",
    accuracy_decimals=0,
    state_class=STATE_CLASS_TOTAL_INCREASING,
).extend(
    {
        cv.GenerateID(CONF_ECAM_ID): cv.use_id(Ecam),
        cv.Required(CONF_COUNTER): cv.int_range(min=0, max=0xFFFF),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_ECAM_ID])
    var = await sensor.new_sensor(config)
    cg.add(parent.add_counter(config[CONF_COUNTER], var))
