import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import fram
from esphome.const import CONF_ID

DEPENDENCIES = ["fram"]
CONF_FRAM_ID = "fram_id"
CONF_POOL_SIZE = "pool_size"
CONF_POOL_START = "pool_start"

fram_pref_ns = cg.esphome_ns.namespace("fram_pref")
FRAMPREFComponent = fram_pref_ns.class_("FRAM_PREF", cg.Component, cg.esphome_ns.class_("ESPPreferences"))

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(FRAMPREFComponent),
    cv.GenerateID(CONF_FRAM_ID): cv.use_id(fram.FRAMComponent),
    cv.Required(CONF_POOL_SIZE): cv.All(fram.validate_bytes_1024, cv.int_range(min=9,max=65535)),
    cv.Optional(CONF_POOL_START, default=0): cv.int_range(min=0,max=65535)
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    fram = await cg.get_variable(config[CONF_FRAM_ID])
    var = cg.new_Pvariable(config[CONF_ID], fram, config[CONF_POOL_SIZE], config[CONF_POOL_START])
    await cg.register_component(var, config)
