import esphome.codegen as cg
from esphome.components import climate_ir, remote_base
from esphome.components.climate_ir import CONF_SUPPORTS_HEAT_COOL
import esphome.config_validation as cv
from esphome.const import CONF_MODEL, CONF_SUPPORTS_HEAT
from esphome.types import ConfigType

from . import mirage_ns

AUTO_LOAD = ["climate_ir"]
CODEOWNERS = ["@agudelotmateo"]

CONF_MIRAGE_ID = "mirage_id"

MirageClimate = mirage_ns.class_("MirageClimate", climate_ir.ClimateIR)

Model = mirage_ns.enum("Model")
MODELS = Model.options("KKG29A-C1", prefix="MODEL_")

CONFIG_SCHEMA = climate_ir.climate_ir_with_receiver_schema(MirageClimate).extend(
    {
        cv.Optional(CONF_MODEL, default="KKG29A-C1"): cv.enum(MODELS, upper=True),
        # Only tested on cool-only KKG29A-C1 units; heat and auto (heat/cool) are not supported
        cv.Optional(CONF_SUPPORTS_HEAT, default=False): cv.boolean_false,
        cv.Optional(CONF_SUPPORTS_HEAT_COOL, default=False): cv.boolean_false,
    }
)


async def to_code(config: ConfigType) -> None:
    remote_base.request_protocol("mirage")
    var = await climate_ir.new_climate_ir(config)
    cg.add(var.set_model(config[CONF_MODEL]))
