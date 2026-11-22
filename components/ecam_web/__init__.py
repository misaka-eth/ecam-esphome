"""Serves the ECAM web UI (webapp/, built to ecam.js) next to ESPHome's web_server.

web_server's own page loads it through `js_url: /ecam.js` (see ecam/web-ui.yaml); the UI talks to the
web_server REST API and its /events stream like the stock frontend.
"""

import gzip
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import web_server_base
from esphome.const import CONF_ID

DEPENDENCIES = ["web_server"]

CONF_DATA_ID = "data_id"

ecam_web_ns = cg.esphome_ns.namespace("ecam_web")
EcamWeb = ecam_web_ns.class_("EcamWeb", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EcamWeb),
        cv.GenerateID(CONF_DATA_ID): cv.declare_id(cg.uint8),
        cv.GenerateID(web_server_base.CONF_WEB_SERVER_BASE_ID): cv.use_id(web_server_base.WebServerBase),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    await cg.get_variable(config[web_server_base.CONF_WEB_SERVER_BASE_ID])
    data = gzip.compress((Path(__file__).parent / "ecam.js").read_bytes(), 9, mtime=0)
    arr = cg.progmem_array(config[CONF_DATA_ID], list(data))
    var = cg.new_Pvariable(config[CONF_ID], arr, len(data))
    await cg.register_component(var, config)
