"""P4 S7 (NATIVE-DESKTOP-PLAN.md section 20.4): C++-native tests for the
Templates port (`pytcad._core.TemplateCatalog`, exposed via
`workbench/core/templates.py`'s thin wrapper).

No parity gate against a Python oracle -- there is none left to compare
against (`workbench/core/templates.py`'s eight `_build_*` functions are
gone). These tests exercise the REAL compiled `TemplateCatalog` through
the wrapper, the same way S6a's `ModelCatalog` port relies on existing
pinned tests plus this pattern rather than inventing a separate C++
gtest binary (this repo's `core/` has none):

  - golden:      each shipped template, built at its documented default
                 parameters, produces a fixed (StructureModel, MeshModel)
                 pair -- checked-in JSON, safe to pin exactly because
                 template construction is deterministic algebra on
                 literal constants, never an iterative solve (unlike the
                 npz solver goldens root CLAUDE.md's golden-baseline
                 rule is about).
  - invariant:   every built device passes DomainDevice.validate(); an
                 out-of-range/non-integer parameter is refused with a
                 named ValueError, never silently clamped.
  - behavioral:  every template's parameter list matches its documented
                 shape (name/label/unit/default all present and typed
                 right).
"""
import dataclasses
import json
import os

import pytest

from workbench.adapters.spec import structure_from_domain
from workbench.core.templates import get_template, list_templates

_GOLDEN_PATH = os.path.join(os.path.dirname(__file__), "goldens", "templates",
                            "default_params.json")

with open(_GOLDEN_PATH) as _f:
    _GOLDEN = json.load(_f)


# ---------------------------------------------------------------- golden
@pytest.mark.parametrize("tid", sorted(_GOLDEN))
def test_default_build_matches_the_checked_in_golden(tid):
    dev = get_template(tid).build({})
    structure, mesh = structure_from_domain(dev)
    got = {"structure": dataclasses.asdict(structure),
           "mesh": dataclasses.asdict(mesh)}
    assert got == _GOLDEN[tid]


def test_golden_covers_every_shipped_template():
    assert sorted(_GOLDEN) == list_templates()


# ------------------------------------------------------------- invariant
@pytest.mark.parametrize("tid", list_templates())
def test_default_build_passes_validation(tid):
    get_template(tid).build({}).validate()


@pytest.mark.parametrize("tid", list_templates())
def test_out_of_range_parameter_is_refused_not_clamped(tid):
    t = get_template(tid)
    p = t.params[0]
    if p.lo is not None:
        with pytest.raises(ValueError):
            t.build({p.name: p.lo - 1.0})
    if p.hi is not None:
        with pytest.raises(ValueError):
            t.build({p.name: p.hi + 1.0})


@pytest.mark.parametrize("tid", list_templates())
def test_fractional_value_for_an_integer_parameter_is_refused(tid):
    t = get_template(tid)
    int_params = [p for p in t.params if p.integer]
    if not int_params:
        pytest.skip(f"{tid} has no integer parameters")
    p = int_params[0]
    with pytest.raises(ValueError, match="whole number"):
        t.build({p.name: p.default + 0.5})


# ------------------------------------------------------------- behavioral
@pytest.mark.parametrize("tid", list_templates())
def test_param_shape_matches_the_documented_contract(tid):
    t = get_template(tid)
    assert t.id == tid
    assert t.title and t.description
    assert t.params, f"{tid}: no parameters"
    for p in t.params:
        assert isinstance(p.name, str) and p.name
        assert isinstance(p.label, str) and p.label
        assert isinstance(p.unit, str)
        assert isinstance(p.default, float)
        assert p.lo is None or isinstance(p.lo, float)
        assert p.hi is None or isinstance(p.hi, float)
        assert isinstance(p.integer, bool)
