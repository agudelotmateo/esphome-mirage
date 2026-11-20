import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_CONFIG
from esphome.types import ConfigType

from ..climate import CONF_MIRAGE_ID, MirageClimate

CODEOWNERS = ["@agudelotmateo"]

CONF_DISPLAY = "display"

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(CONF_MIRAGE_ID): cv.use_id(MirageClimate),
            cv.Optional(CONF_DISPLAY): button.button_schema(
                button.ApplyButton,
                icon="mdi:led-outline",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
        }
    ),
    cv.has_at_least_one_key(CONF_DISPLAY),
)


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_MIRAGE_ID])
    buttons = button.sub_buttons(config, parent=hub)
    await buttons(CONF_DISPLAY, None, apply="toggle_display()")
