import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv

from .. import CONF_NOVOFERM_ID, Novoferm, novoferm_ns

DEPENDENCIES = ["novoferm"]

NovofermBinarySensor = novoferm_ns.class_(
    "NovofermBinarySensor", binary_sensor.BinarySensor, cg.Component
)

CONFIG_SCHEMA = cv.All(
    binary_sensor.binary_sensor_schema(NovofermBinarySensor)
    .extend(
        {
            cv.GenerateID(CONF_NOVOFERM_ID): cv.use_id(Novoferm),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)
    await cg.register_component(var, config)

    parent = await cg.get_variable(config[CONF_NOVOFERM_ID])
    cg.add(var.set_novoferm_parent(parent))
