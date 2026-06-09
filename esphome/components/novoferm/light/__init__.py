import esphome.codegen as cg
from esphome.components import light
import esphome.config_validation as cv
from esphome.const import CONF_OUTPUT_ID, CONF_UPDATE_INTERVAL

from .. import CONF_NOVOFERM_ID, Novoferm, novoferm_ns

DEPENDENCIES = ["novoferm"]

NovofermLight = novoferm_ns.class_(
    "NovofermLight", light.LightOutput, cg.PollingComponent
)
CONFIG_SCHEMA = cv.All(
    light.BRIGHTNESS_ONLY_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(NovofermLight),
            cv.GenerateID(CONF_NOVOFERM_ID): cv.use_id(Novoferm),
        }
    ).extend(cv.polling_component_schema("1s"))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await cg.register_component(var, config)
    config.pop(
        CONF_UPDATE_INTERVAL
    )  # drop UPDATE_INTERVAL as it does not apply to the light component
    await light.register_light(var, config)

    parent = await cg.get_variable(config[CONF_NOVOFERM_ID])
    cg.add(var.set_novoferm_parent(parent))
