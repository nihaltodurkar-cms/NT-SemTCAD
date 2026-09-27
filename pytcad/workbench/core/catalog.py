"""The Model Catalog: physics as documented, selectable components.

As of P4 S6 (NATIVE-DESKTOP-PLAN.md section 20.3 decision 3), C++ is the
SOLE implementation -- the registry data (equations, parameters,
references, applicability, limitations for every physics model flag)
and the four operations below live in `core/src/uicore/catalog.cpp`
(data in `catalog_data.inc`, generated once from this module's own
former `_MODELS` dict, never hand-retyped) and are exposed as
`pytcad._core.ModelCatalog`/`ModelInfo`. This module is a thin
compatibility layer, not a second implementation kept for comparison --
see the plan's own "no permanent oracle" language for why.

Follows the EXACT lazy-`require_accel()` pattern `pytcad/_accel.py`
already uses for every accelerated numerical kernel: importing this
module never fails (`workbench/core/device.py`'s own
`models: dict = field(default_factory=ModelCatalog.default_config)`
would otherwise turn "extension not built" into "cannot even import
device.py" at class-definition time), but calling any of
list()/describe()/default_config()/validate() without `pytcad._core`
built raises the same actionable `ImportError` every other accelerated
kernel does.
"""
from pytcad import _accel

__all__ = ["ModelCatalog"]


class ModelCatalog:
    """Registry API: list()/describe()/validate()/default_config().
    See core/src/uicore/catalog.cpp for the real implementation --
    nothing here is a second copy of it."""

    @staticmethod
    def list():
        _accel.require_accel()
        return _accel.core.ModelCatalog.list()

    @staticmethod
    def describe(key):
        _accel.require_accel()
        return _accel.core.ModelCatalog.describe(key)

    @staticmethod
    def default_config():
        _accel.require_accel()
        return _accel.core.ModelCatalog.default_config()

    @staticmethod
    def validate(config):
        _accel.require_accel()
        return _accel.core.ModelCatalog.validate(config)
