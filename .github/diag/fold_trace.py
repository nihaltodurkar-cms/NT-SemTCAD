"""Trace the M15 N=2e16 arc-length sweep and print the branch near the fold."""
import sys, time, warnings, os
root = sys.argv[1] if len(sys.argv) > 1 else "."
sys.path.insert(0, os.path.join(root, "tests")); sys.path.insert(0, root)
import numpy as np
import test_m15_ionization as t
from pytcad.continuation import arc_length_sweep, ArcLengthStalled
from pytcad.device import _II_STAGES
nd = float(sys.argv[2]) if len(sys.argv) > 2 else 2e16
dev = t._diode(impact=True, na=nd); dev.solve_equilibrium()
t0 = time.time()
try:
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        recs = arc_length_sweep(dev, 0.0, -60.0, ds0=10.0, ds_max=500.0, ds_min=10.0/4096,
                                max_steps=800, strength_stages=_II_STAGES, corrector_max_iter=60)
    print("NO STALL"); stalled = None
except ArcLengthStalled as e:
    recs = e.last_records; stalled = e.last_V
print(f"N={nd:g} stall_V={stalled} analysis={t.breakdown_voltage_one_sided(nd):.3f} steps={len(recs)} {time.time()-t0:.0f}s")
Vs = np.array([r["V"] for r in recs]); Js = np.array([r["J"] for r in recs])
print("max|V| along trace = %.4f at step %d" % (np.abs(Vs).max(), np.abs(Vs).argmax()))
for i, r in enumerate(recs):
    if abs(r["V"]) > 25 or i >= len(recs) - 5:
        print(f"{i:4d} V={r['V']:+9.4f} J={r['J']:+.4e} ds={r.get('ds', 0):.4g}")
