import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

DEPENDENCIES = ["uart"]
MULTI_CONF = False

CONF_WIFI_ICON = "wifi_icon"

ecam_ns = cg.esphome_ns.namespace("ecam")
Ecam = ecam_ns.class_("Ecam", cg.Component, uart.UARTDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Ecam),
            # ed f0 3x: 30 none, 31 4 bars, 32 cloud off, 33 3 bars, 34 1 bar, 35 grey, 36 2 red.
            # "auto" (default) mirrors the Atom's own wifi and Home Assistant link.
            cv.Optional(CONF_WIFI_ICON, default="auto"): cv.Any(
                cv.one_of("auto", lower=True), cv.int_range(min=0x30, max=0x36)
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema("ecam", baud_rate=115200, require_tx=True, require_rx=True)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    if config[CONF_WIFI_ICON] != "auto":
        cg.add(var.set_wifi_icon(config[CONF_WIFI_ICON]))
