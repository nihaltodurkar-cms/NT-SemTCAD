"""Parametric device templates (M5): the Device Builder's vocabulary.

NATIVE-DESKTOP-PLAN.md section 20.3 decision 3 / P4 S7: C++
(`pytcad._core.TemplateCatalog`) is now the SOLE implementation of the
parameter registry and the per-template build() geometry logic. This
module is a thin compatibility layer: it reconstructs the real
`DomainDevice`/`Region`/`ContactDef`/`Boundary` Python dataclasses from
the C++ layer's plain-dict output and calls their EXISTING, unmodified
`.validate()` -- those dataclasses are domain core shared by every other
device-authoring path in the workbench, not part of "Templates" in the
P4 decision, and stay pure Python untouched by this port.

No permanent Python oracle: the eight builder functions that used to
live here (`_build_pn_diode`, `_build_nmos`, ...) are gone, not kept
for comparison. Correctness is the existing pinned tests
(gui/tests/test_device_templates.py, gui/tests/test_m11s5_templates.py)
plus core/tests' C++-native behavioral/golden tests.
"""
from dataclasses import dataclass

from pytcad import _accel

from .device import Boundary, ContactDef, DomainDevice, Region

__all__ = ["TemplateParam", "DeviceTemplate", "TEMPLATES", "list_templates",
           "get_template"]


@dataclass(frozen=True)
class TemplateParam:
    name: str
    label: str
    unit: str
    default: float
    lo: float = None
    hi: float = None
    integer: bool = False


def _region_from_dict(d):
    kwargs = dict(d)
    return Region(**kwargs)


def _boundary_from_dict(d):
    return Boundary(edge=d["edge"], range_lo=d.get("range_lo"),
                    range_hi=d.get("range_hi"))


def _contact_from_dict(d):
    kwargs = dict(d)
    boundary = kwargs.pop("boundary", None)
    if boundary is not None:
        kwargs["boundary"] = _boundary_from_dict(boundary)
    return ContactDef(**kwargs)


def _domain_device_from_dict(d):
    kwargs = dict(d)
    kwargs["regions"] = [_region_from_dict(r) for r in kwargs.get("regions", [])]
    kwargs["contacts"] = [_contact_from_dict(c) for c in kwargs.get("contacts", [])]
    return DomainDevice(**kwargs)


@dataclass(frozen=True)
class DeviceTemplate:
    id: str
    title: str
    description: str
    params: tuple

    def build(self, values=None):
        """Merge defaults with `values`, validate, and build -- via the
        C++ TemplateCatalog; the returned device is then re-validated
        through DomainDevice.validate() exactly as before the port."""
        _accel.require_accel()
        d = _accel.core.TemplateCatalog.build(self.id, values)
        dev = _domain_device_from_dict(d)
        dev.validate()
        return dev


def _template_from_info(info):
    params = tuple(
        TemplateParam(name=p["name"], label=p["label"], unit=p["unit"],
                      default=p["default"], lo=p["lo"], hi=p["hi"],
                      integer=p["integer"])
        for p in info["params"])
    return DeviceTemplate(id=info["id"], title=info["title"],
                          description=info["description"], params=params)


def list_templates():
    _accel.require_accel()
    return list(_accel.core.TemplateCatalog.list())


def get_template(template_id):
    _accel.require_accel()
    info = _accel.core.TemplateCatalog.describe(template_id)
    return _template_from_info(info)


class _TemplateRegistry:
    """A dict-like, lazy facade over the C++ registry: `TEMPLATES[id]`
    still works for any existing caller, without this module needing
    `pytcad._core` just to be imported (the same import-must-never-fail
    contract `_accel.py` and `catalog.py` already keep)."""

    def __getitem__(self, template_id):
        return get_template(template_id)

    def __iter__(self):
        return iter(list_templates())

    def __len__(self):
        return len(list_templates())

    def __contains__(self, template_id):
        return template_id in list_templates()

    def keys(self):
        return list_templates()


TEMPLATES = _TemplateRegistry()
