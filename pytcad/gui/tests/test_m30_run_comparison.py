"""M30 Phase 7 acceptance tests: Run Comparison.

Contract under test:
  - gui/services/provenance_diff.py's provenance_diff(records, labels)
    is a pure, Qt-free function: it flags every field that genuinely
    differs between RunRecords and marks identical fields as such.

PySide6/QML removed from this repo: StudyController.compareRows itself
(and the QML comparison panel) were removed with it -- the pure
provenance_diff() gates below are unaffected.
"""
from gui.services.provenance_diff import provenance_diff
from gui.services.solver_backend import RunRecord


def _record(**overrides):
    base = dict(backend="pytcad", created_utc="2026-01-01T00:00:00",
               dimensionality=2, material="Silicon", T=300.0,
               models={"srh": True, "auger": False}, numerics={})
    base.update(overrides)
    return RunRecord(**base)


# ----------------------------------------------------------------------
#  G-PROVDIFF: every genuine difference is flagged; identical fields
#  are not
# ----------------------------------------------------------------------
def test_provenance_diff_flags_real_differences_only():
    a = _record()
    b = _record(T=350.0, models={"srh": True, "auger": True})
    rows = provenance_diff([a, b], labels=["A", "B"])

    by_field = {r["field"]: r for r in rows}
    assert by_field["T"]["differs"] is True
    assert by_field["T"]["values"] == [300.0, 350.0]
    assert by_field["models"]["differs"] is True
    assert by_field["backend"]["differs"] is False
    assert by_field["dimensionality"]["differs"] is False
    assert by_field["material"]["differs"] is False


def test_provenance_diff_identical_records_flags_nothing():
    a, b = _record(), _record()
    rows = provenance_diff([a, b])
    assert not any(r["differs"] for r in rows)


def test_provenance_diff_handles_a_missing_record():
    rows = provenance_diff([_record(), None])
    by_field = {r["field"]: r for r in rows}
    assert by_field["backend"]["differs"] is True
    assert by_field["backend"]["values"] == ["pytcad", None]
