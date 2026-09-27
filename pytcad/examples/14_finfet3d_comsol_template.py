"""Example 14 -- 3D tri-gate FinFET reproducing COMSOL's Semiconductor
Module "Fin Field-Effect Transistor" demo (applications/Semiconductor_
Module/Transistors/finfet.mph) on PyTCAD's structured Device3D core.

COMSOL's model is a bare geometry/mesh template (dimensionless block
sizes, physics set separately in the GUI): a 3.0 x 0.7 x 0.7 fin built
from 8 boolean blocks -- source cap (0.5) | spacer (0.2) | gate (1.6,
wrapped by two 0.25-thick oxide blocks on the sidewalls) | spacer (0.2)
| drain cap (0.5) -- meshed with a free-triangular source face swept
along two sub-regions. See finfet.mph's dmodel.xml for the exact block
list this mirrors.

This example maps those same proportions onto real device dimensions
(scale factor 0.1 um/unit -> Lg=160nm, Lsd=50nm, fin 70nm x 70nm,
tox=10nm) and runs PyTCAD's existing M26 tri-gate FinFET builder
(pytcad.finfet3d.build_finfet3d) -- the same closed-form-doping,
structured-tensor-product-mesh model examples/12 uses, see that
module's own honesty clause for what is and is not modeled (no 3D
process simulation, no corner rounding, closed-form Gaussian x erfc
source/drain doping tiled along the fin width).

Reproduces COMSOL's own two-part study: (1) Id-Vg at fixed Vds to get
the gate transfer characteristic, (2) Id-Vd at a few fixed Vg to get
the output characteristic.

Runtime: ~1 minute.

    python examples/14_finfet3d_comsol_template.py

DIAGNOSED: WHY THE Id-Vg CURVE BELOW IS NEARLY FLAT (not a textbook
switching curve). A dedicated sweep (mesh NX in {6,12,24}; Lg in
{1.6um, 3.0um}; Na in {1e18, 5e18, 1e19}; a +-1..4 V wide-range Vg
sweep; and a control run with the body contact removed) ruled out the
obvious suspects one at a time:

  * NOT mesh resolution: Ion/Ioff shape (a gentle ~10-30% rise across
    the whole Vg sweep) is essentially unchanged from NX=6 to NX=24 --
    a 4x refinement in the along-channel direction does not fix it.
  * NOT DIBL/punch-through: a LONGER gate (3.0um) gives MORE current
    than the shorter one (1.6um) at every Vg, the opposite sign from
    classical short-channel leakage. This alone rules out DIBL as an
    explanation.
  * NOT the grounded body contact: removing it entirely rescales the
    current (~3x higher) but leaves the same flat shape -- it isn't
    the source of the flatness, just a parallel path.
  * Strongly, and correctly-signed, sensitive to body doping: Id drops
    ~100x per decade of Na (1e18 -> 5e18 -> 1e19), consistent with a
    depletion-width-limited leakage/diffusion current, NOT with
    inversion-channel drift current (which wouldn't collapse like that
    once the channel is actually inverted).
  * A wide Vg sweep (-1 V to +4 V, i.e. ~5x past the analytic 1D Vth
    estimate of 1.56 V for Na=1e18 computed from this same Vfb/doping)
    shows NO kink at threshold at all -- current rises smoothly and
    monotonically the entire way.

CONCLUSION: the gate never drives this structured-mesh model into a
resolved strong-inversion regime anywhere in the swept range. The
current instead behaves like a depletion/leakage current that is
strongly Na-dependent but only weakly, smoothly Vg-dependent. The
prime remaining suspect -- NOT covered by the tests above, and an
honest gap in this diagnosis -- is the TRANSVERSE mesh (NY=NZ=4,
never varied), which is graded across the full 70 nm fin height/width
and is far coarser than the few-nm scale an inversion sheet charge
needs to resolve at the oxide interface; build_finfet3d's structured
tensor-product mesh may simply be too coarse there to ever form one.
This has NOT been confirmed by a dedicated NY/NZ convergence test --
that is real follow-up work, not a claimed result. The numbers below
are reported as-is, not tuned to look like a textbook curve.
"""

import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import warnings
warnings.simplefilter("ignore")

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

from pytcad.finfet3d import build_finfet3d, id_vg_sweep_3d
from pytcad.device import NewtonOptions

# --- COMSOL finfet.mph block geometry, scaled 0.1 um/unit ---------------
SCALE = 0.1e-6  # meters per COMSOL geometry unit
Lsd = 0.5 * SCALE       # source/drain cap length (blk2/blk6): 50 nm
Lg = 1.6 * SCALE        # gate length (blk4/blk7/blk8): 160 nm
Hfin = 0.7 * SCALE      # fin height (blk1 lz... here used as fin height): 70 nm
Wfin = 0.7 * SCALE      # fin width: 70 nm
tox_cm = 0.1 * SCALE * 100.0  # oxide thickness (blk7/blk8 lz=0.1) in cm: 10 nm

Na = 1e18       # p-type fin body doping [cm^-3]
Nsd_peak = 1e20 # n+ source/drain peak doping [cm^-3]

COMMON = dict(Lsd=Lsd, Lg=Lg, Hfin=Hfin, Wfin=Wfin, tox_cm=tox_cm,
              Na=Na, Nsd_peak=Nsd_peak, NX=6, NY=4, NZ=4, mesh_ratio=1.3)

print(f"Fin: L={2*Lsd+Lg:.3e} m (Lsd={Lsd:.3e}, Lg={Lg:.3e}), "
      f"{Hfin:.3e} x {Wfin:.3e} m cross-section, tox={tox_cm*1e7:.1f} nm")

opts = NewtonOptions()

# --- 1) Id-Vg transfer characteristic (COMSOL's gate voltage sweep) -----
Vg = np.linspace(-0.2, 1.0, 13)
Vds_transfer = 0.05
dev = build_finfet3d(**COMMON)
Id_vg = id_vg_sweep_3d(dev, Vg, Vds=Vds_transfer)

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11, 4.5))
ax1.semilogy(Vg, np.abs(Id_vg), "o-")
ax1.set_xlabel("Vg [V]"); ax1.set_ylabel("|Id| [A]")
ax1.set_title(f"Id-Vg (Vds={Vds_transfer} V)")

# --- 2) Id-Vd output characteristic at fixed Vg (COMSOL's drain sweep) --
Vd = np.linspace(0.0, 0.5, 11)
for Vg_fixed in (0.4, 0.7, 1.0):
    dev2 = build_finfet3d(**COMMON)
    dev2.solve_equilibrium(opts)
    Id_vd = []
    for Vd_i in Vd:
        dev2.solve_bias({"drain": Vd_i, "gate_top": Vg_fixed,
                          "gate_left": Vg_fixed, "gate_right": Vg_fixed}, opts)
        Id_vd.append(dev2.terminal_current("drain"))
    ax2.plot(Vd, np.array(Id_vd) * 1e6, "o-", label=f"Vg={Vg_fixed} V")

ax2.set_xlabel("Vd [V]"); ax2.set_ylabel("Id [uA]")
ax2.set_title("Id-Vd")
ax2.legend()

fig.tight_layout()
fig.savefig("finfet3d_comsol_template.png", dpi=140)
print("\nWrote finfet3d_comsol_template.png")
