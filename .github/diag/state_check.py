"""TEMPORARY diag (PR #2). save: trace N=2e16 to just before the stall and store
two consecutive states + F/J there. check: re-evaluate F/J at the stored state and
retry the staged corrector from it, printing diffs vs the stored reference."""
import sys, os, warnings, hashlib
mode, root, path = sys.argv[1], sys.argv[2], sys.argv[3]
sys.path.insert(0, os.path.join(root, "tests")); sys.path.insert(0, root)
import numpy as np
import test_m15_ionization as t
import pytcad.continuation as C
from pytcad.device import _II_STAGES, NewtonOptions
warnings.simplefilter("ignore")
dev = t._diode(impact=True, na=2e16); dev.solve_equilibrium()
VT = dev.VT; N = dev.N
bc_at = lambda v: dev._contact_values([v, 0.0])
if mode == "save":
    try:
        C.arc_length_sweep(dev, 0.0, -60.0, ds0=10.0, ds_max=500.0, ds_min=10.0/4096,
                           max_steps=800, strength_stages=_II_STAGES, corrector_max_iter=60)
    except C.ArcLengthStalled as e:
        recs = e.last_records
    i = max(k for k, r in enumerate(recs) if abs(r["V"]) < 35.4)
    a, b = recs[i - 1], recs[i]
    F, J, *_ = dev._residual_jacobian(b["psi"], b["n"], b["p"], bc_at(b["V"]))
    J = J.tocoo()
    np.savez(path, Va=a["V"], Vb=b["V"], psia=a["psi"], na=a["n"], pa=a["p"],
             psib=b["psi"], nb=b["n"], pb=b["p"], F=F, Jr=J.row, Jc=J.col, Jv=J.data)
    print("saved states at V=", a["V"], b["V"])
z = np.load(path)
F, J, *_ = dev._residual_jacobian(z["psib"], z["nb"], z["pb"], bc_at(float(z["Vb"])))
J = J.tocoo(); import scipy.sparse as sp
Jref = sp.coo_matrix((z["Jv"], (z["Jr"], z["Jc"])), shape=J.shape).tocsr(); Jc = J.tocsr()
dF = np.abs(F - z["F"]); scale = np.maximum(np.abs(z["F"]), 1e-300)
dJ = abs(Jc - Jref); 
print(f"F: max|dF|={dF.max():.3e} max rel={np.max(dF/np.maximum(np.abs(z['F']),1e-30)):.3e} |F|max={np.abs(F).max():.3e}")
print(f"J: max|dJ|={dJ.max():.3e} |J|max={abs(Jref).max():.3e}")
ua = C._pack(z["psia"], z["na"], z["pa"]); ub = C._pack(z["psib"], z["nb"], z["pb"])
Va, Vb = float(z["Va"]), float(z["Vb"])
raw = np.concatenate([ub - ua, [Vb - Va]]); mask = np.zeros(3*N+1, bool); mask[0:3*N:3] = True; mask[-1] = True
raw = raw * mask; tf = raw / np.linalg.norm(raw); tu, tV = tf[:-1], tf[-1]
c = np.zeros(3*N); c[0] = -1.0/VT
for ds in (500.0, 250.0, 125.0, 60.0, 30.0, 10.0, 4.0, 1.0):
    u, V, ok, it, _ = C._bordered_corrector_staged(dev, ub + ds*tu, Vb + ds*tV, ub, Vb, tu, tV, ds, c,
        bc_at, NewtonOptions(), 1e-8, 60, _II_STAGES, "_ii_strength")
    print(f"ds={ds:7.2f} converged={ok} iters={it} V={V:+.5f}")
