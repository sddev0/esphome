from esphome import automation
import esphome.codegen as cg
from esphome.components import cover
import esphome.config_validation as cv
from esphome.const import CONF_CLOSE_DURATION, CONF_ID, CONF_OPEN_DURATION, CONF_STOP

from .. import CONF_NOVOFERM_ID, Novoferm, novoferm_ns

DEPENDENCIES = ["novoferm"]

NovofermCover = novoferm_ns.class_("NovofermCover", cover.Cover, cg.PollingComponent)

CONF_LEARN_CYCLE_TIMES = "learn_cycle_times"

CONFIG_SCHEMA = cv.All(
    cover.cover_schema(NovofermCover)
    .extend(
        {
            cv.GenerateID(CONF_NOVOFERM_ID): cv.use_id(Novoferm),
            cv.Optional(
                CONF_OPEN_DURATION, default="15s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_CLOSE_DURATION, default="22s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_LEARN_CYCLE_TIMES, default=True): cv.boolean,
        }
    )
    .extend(cv.polling_component_schema("300ms"))
)


async def to_code(config):
    var = await cover.new_cover(config)
    await cg.register_component(var, config)

    paren = await cg.get_variable(config[CONF_NOVOFERM_ID])
    cg.add(var.set_novoferm_parent(paren))
    cg.add(var.set_close_duration(config[CONF_CLOSE_DURATION]))
    cg.add(var.set_open_duration(config[CONF_OPEN_DURATION]))
    cg.add(var.set_learn_cycle_times(config[CONF_LEARN_CYCLE_TIMES]))


VentilationAction = novoferm_ns.class_("VentilationAction", automation.Action)


@automation.register_action(
    "novoferm.cover.ventilate",
    VentilationAction,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(NovofermCover),
            cv.Optional(CONF_STOP): cv.templatable(cv.boolean),
        }
    ),
    synchronous=True,
)
async def ventilation_action_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    if (stop := config.get(CONF_STOP)) is not None:
        template_ = await cg.templatable(stop, args, bool)
        cg.add(var.set_stop(template_))
    return var
