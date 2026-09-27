"""Example 16: a genuinely CURVED, non-box FinFET cross-section --
tapered (trapezoidal) sidewalls with fillet-rounded top corners, built
directly with gmsh's OCC geometry kernel (points/lines + a real 3D
`occ.fillet()` corner rounding, not pytcad.finfet3d's closed-form
structured tensor-product box, and not gmsh_finfet3d.py's flattened
process2d staircase extrusion, which by its own docstring has neither
sloped sidewalls nor corner rounding).

Solved on the UNSTRUCTURED tet mesh path (pytcad.unstructured_dd3d),
which already carries M26's tri-gate Robin/GateBC physics -- this
example is a new GEOMETRY, not new physics or a new solver.

Region/contact/gate classification is done at the MESH level (each
tet/boundary-triangle centroid's x-coordinate against the source/gate/
drain x-ranges, and the bottom-face-exclusion for the gate wrap), not
via gmsh Physical Groups -- see build_curved_finfet_mesh3d()'s own
docstring for why: a single continuous filleted solid has no natural
CAD-level seam at the source/gate and gate/drain boundaries, so
splitting it at the CAD level would need extra cut/fragment operations
this example does not need for a demonstration of curved geometry.

HONEST SCOPE: doping is uniform per region (source/gate/drain), exactly
gmsh_finfet3d.py's own stated simplification -- not a diffused implant
profile. The coupled bias sweep is voltage-ramped (Vds first, then Vg,
each step warm-started from the last), because unstructured_dd3d.py's
own module docstring records that a single unramped Newton call from
V=0 to full doping contrast on a tet mesh often fails to converge.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import numpy as np

from pytcad.device import NewtonOptions, thermal_voltage
from pytcad.materials import SILICON
from pytcad.mosfet import flatband_voltage
from pytcad.unstructured_assembly3d import (
    build_edge_flux_geometry3d, build_unstructured_stencil3d,
)
from pytcad.unstructured_dd3d import (
    evaluate_doping_at_nodes3d, solve_bias3d, solve_poisson_equilibrium3d,
)


def _require_gmsh():
    try:
        import gmsh
        return gmsh
    except ImportError as exc:
        raise ImportError(
            "this example requires the optional 'gmsh' package "
            f"(pip install gmsh): {exc}") from exc


def build_curved_finfet_mesh3d(Lsd=3.0e-6, Lg=4.0e-6, Hfin=3.0e-6,
                               Wbase=2.4e-6, Wtop=1.8e-6, r_fillet=4.0e-7,
                               mesh_size_cm=4.0e-7):
    """A tapered, top-corner-rounded fin (a real polygon-with-fillets
    cross-section, extruded along x): the (y, z) profile is a trapezoid
    (Wbase at the base y=0, narrowing to Wtop at the top y=Hfin -- a
    real fabrication effect, not just a rectangle) with BOTH top
    corners rounded to radius r_fillet by gmsh's own OCC 3D fillet
    operator, applied to the two longitudinal top-ridge edges of the
    straight-sided prism (so the rounding runs the full fin length,
    matching a real etched fin's constant cross-section).

    Region/contact/gate classification is done AFTER meshing, from
    each element's own coordinates, not from gmsh Physical Groups:
    source/gate/drain volumes only differ from each other by uniform
    doping (not a distinct CAD solid), so there's no shared-face
    fragment() step needed the way build_diode_mesh3d needs one for
    its two differently-doped box solids -- classifying tets/boundary
    triangles by x-position after the single continuous solid is meshed
    is simpler and exactly as correct here.

    Returns (nodes (N,3), tets (T,4), boundary_tris (B,3), region_of_tet
    (T,) str array in {"source","gate","drain"}, gate_tris (subset of
    boundary_tris whose centroid lies in the gate x-range and is not
    the flat y=0 base), source_tris, drain_tris). All lengths in cm.
    """
    gmsh = _require_gmsh()
    # OCC's fillet() genuinely crashes ("access violation") on this
    # machine's gmsh 4.15.2 build when the solid's absolute coordinates
    # are ~1e-6 (this device's natural cm scale) or smaller -- measured
    # directly: the identical box fillet succeeds at 1e-5 and fails at
    # 1e-6, independent of the fillet radius chosen. Not a geometry
    # mistake; a real tool precision floor. Fix: build in a SCALED unit
    # (this function's real cm dimensions x GEOM_SCALE, safely above
    # that floor), then divide the extracted node coordinates back down
    # to true cm before returning -- the mesh TOPOLOGY (tets, boundary
    # triangles, which nodes touch what) is scale-invariant, so nothing
    # downstream needs to know this happened.
    GEOM_SCALE = 1.0e4
    L_total = 2.0 * Lsd + Lg
    dz_taper = 0.5 * (Wbase - Wtop)  # inward taper per side, base -> top

    def s(v):
        return v * GEOM_SCALE

    gmsh.initialize()
    try:
        gmsh.model.add("finfet3d_curved")
        occ = gmsh.model.occ

        # The (y, z) cross-section at x=0: a trapezoid, bottom-left ->
        # bottom-right -> top-right -> top-left, straight sides (the
        # rounding is applied in 3D, below, to the two top edges).
        p_bl = occ.addPoint(0.0, 0.0, 0.0)
        p_br = occ.addPoint(0.0, 0.0, s(Wbase))
        p_tr = occ.addPoint(0.0, s(Hfin), s(Wbase - dz_taper))
        p_tl = occ.addPoint(0.0, s(Hfin), s(dz_taper))
        l_bottom = occ.addLine(p_bl, p_br)
        l_right = occ.addLine(p_br, p_tr)
        l_top = occ.addLine(p_tr, p_tl)
        l_left = occ.addLine(p_tl, p_bl)
        loop = occ.addCurveLoop([l_bottom, l_right, l_top, l_left])
        profile = occ.addPlaneSurface([loop])
        occ.synchronize()

        extruded = occ.extrude([(2, profile)], s(L_total), 0.0, 0.0)
        solid = next(tag for dim, tag in extruded if dim == 3)
        occ.synchronize()

        # The two top-ridge edges: curves running the FULL length along
        # x, at constant y = Hfin and constant z (one of the two top
        # corners' z). Found geometrically, not by a fragile creation-
        # order assumption -- gmsh does not document an ordering
        # contract for extrude()'s side-edge tags.
        # gmsh's own OCC bounding-box query pads by a fixed geometric
        # tolerance (measured ~1e-7 cm here, independent of feature
        # size) -- comparing tightly to the nominal coordinates finds
        # nothing; a tolerance well above that pad, and a "spans nearly
        # the full length" test rather than an exact x-span match, is
        # what actually discriminates the two full-length top-ridge
        # edges from the four short end-cap edges and the two
        # full-length BOTTOM-ridge edges (measured directly, not guessed).
        bbox_tol = s(2.0e-7)
        top_edges = []
        for dim, tag in gmsh.model.getEntities(1):
            xmin, ymin, zmin, xmax, ymax, zmax = occ.getBoundingBox(dim, tag)
            spans_full_length = (xmax - xmin) > 0.9 * s(L_total)
            at_top = abs(ymin - s(Hfin)) < bbox_tol and abs(ymax - s(Hfin)) < bbox_tol
            if spans_full_length and at_top:
                top_edges.append(tag)
        if len(top_edges) != 2:
            raise RuntimeError(
                f"build_curved_finfet_mesh3d: expected 2 top-ridge edges "
                f"to fillet, found {len(top_edges)}")

        filleted = occ.fillet([solid], top_edges, [s(r_fillet)])
        solid = next(tag for dim, tag in filleted if dim == 3)
        occ.synchronize()

        gmsh.option.setNumber("Mesh.MeshSizeMax", s(mesh_size_cm))
        gmsh.option.setNumber("Mesh.MeshSizeMin", s(0.5 * mesh_size_cm))
        gmsh.model.mesh.generate(3)

        node_tags, node_coords, _ = gmsh.model.mesh.getNodes()
        nodes = np.asarray(node_coords, dtype=float).reshape(-1, 3) / GEOM_SCALE
        tag_to_idx = {int(t): i for i, t in enumerate(node_tags)}

        tets = []
        for dim, tag in gmsh.model.getEntities(3):
            etypes, _etags, enodes = gmsh.model.mesh.getElements(3, tag)
            for et, nds in zip(etypes, enodes):
                if et != 4:
                    continue
                nds = np.asarray(nds, dtype=int).reshape(-1, 4)
                for row in nds:
                    tets.append([tag_to_idx[int(n)] for n in row])
        tets = np.asarray(tets, dtype=int)

        boundary_tris = []
        for dim, tag in gmsh.model.getEntities(2):
            etypes, _etags, enodes = gmsh.model.mesh.getElements(2, tag)
            for et, nds in zip(etypes, enodes):
                if et != 2:
                    continue
                nds = np.asarray(nds, dtype=int).reshape(-1, 3)
                for row in nds:
                    boundary_tris.append([tag_to_idx[int(n)] for n in row])
        boundary_tris = np.asarray(boundary_tris, dtype=int)
    finally:
        gmsh.finalize()

    tet_centroid_x = nodes[tets].mean(axis=1)[:, 0]
    region_of_tet = np.where(tet_centroid_x < Lsd, "source",
                             np.where(tet_centroid_x < Lsd + Lg, "gate", "drain"))

    tri_centroid = nodes[boundary_tris].mean(axis=1)
    tri_x, tri_y = tri_centroid[:, 0], tri_centroid[:, 1]
    source_tris = boundary_tris[np.abs(tri_x - 0.0) < 1e-9]
    drain_tris = boundary_tris[np.abs(tri_x - L_total) < 1e-9]
    gate_mask = ((tri_x > Lsd + 1e-9) & (tri_x < Lsd + Lg - 1e-9)
                & (tri_y > 1e-9))  # excludes the flat y=0 base (body side)
    gate_tris = boundary_tris[gate_mask]

    return nodes, tets, boundary_tris, region_of_tet, gate_tris, source_tris, drain_tris


def _export_structured_view(nodes, psi_s, n_s, p_s, C_phys, scale, T, out_path,
                            grid=(48, 24, 20)):
    """Resample the solved TET-mesh fields onto a regular grid and write
    an ordinary structured-3D result .npz, so the curved geometry can be
    opened in the native app's existing VTK 3D viewer.

    HONEST LIMITATION: the native app's result schema (gui/services/
    solver_backend.py's own grammar comment) only knows GEOM_STRUCTURED
    ("structured_rectilinear") -- there is no unstructured-tet geometry
    kind the viewer can read, so this does NOT show the true curved/
    filleted surface; it shows a LINEAR INTERPOLATION of the solved
    fields onto a regular box grid, which smooths the rounded corners
    into a coarser staircase at this grid's resolution. Good enough to
    see the potential/electron-density distribution and the gate's
    effect; not a substitute for the real geometry. Points outside the
    tet mesh's convex hull (the corners of the grid's bounding box,
    since the fin is narrower than its own bounding box) are filled by
    nearest-neighbor rather than left NaN, so field__doping still shows
    a clean source/gate/drain split at the grid's edges.
    """
    from scipy.interpolate import LinearNDInterpolator
    from scipy.spatial import Delaunay, cKDTree

    VT, Ns = scale["VT"], scale["Ns"]
    psi_V = psi_s * VT
    n_cm3 = n_s * Ns
    p_cm3 = p_s * Ns

    xmin, ymin, zmin = nodes.min(axis=0)
    xmax, ymax, zmax = nodes.max(axis=0)
    nx, ny, nz = grid
    x = np.linspace(xmin, xmax, nx)
    y = np.linspace(ymin, ymax, ny)
    z = np.linspace(zmin, zmax, nz)
    Zg, Yg, Xg = np.meshgrid(z, y, x, indexing="ij")  # shape (Nz, Ny, Nx)
    grid_pts = np.column_stack([Xg.ravel(), Yg.ravel(), Zg.ravel()])

    # Built ONCE and reused across the 4 fields below: the ~1300-node
    # Delaunay triangulation (griddata's own dominant cost) and the
    # convex-hull-outside mask + nearest-neighbor indices it needs for
    # the NaN fallback both depend only on `nodes`/`grid_pts`, never on
    # the field values being resampled -- rebuilding them per field was
    # pure waste, confirmed by /code-review.
    tri = Delaunay(nodes)
    outside = tri.find_simplex(grid_pts) < 0  # griddata's own NaN criterion
    nearest_idx = cKDTree(nodes).query(grid_pts[outside])[1] if outside.any() else None

    def resample(values):
        lin = LinearNDInterpolator(tri, values)(grid_pts)
        if outside.any():
            lin[outside] = values[nearest_idx]
        return lin.reshape(Zg.shape)

    fields = {
        "potential": (resample(psi_V), "V"),
        "electron_density": (resample(n_cm3), "cm^-3"),
        "hole_density": (resample(p_cm3), "cm^-3"),
        "doping": (resample(C_phys), "cm^-3"),
    }
    out = {"dimensionality": np.array(3), "solved_bias": np.array(True),
          "axis_x": x, "axis_y": y, "axis_z": z,
          "result__schema": np.array(3)}
    for name, (arr, unit) in fields.items():
        out[f"field__{name}"] = arr
        out[f"unit__{name}"] = np.array(unit)

    tmp_path = out_path + ".tmp.npz"
    np.savez(tmp_path, **out)
    os.replace(tmp_path, out_path)
    print(f"Wrote a structured resample viewable in the native app's 3D viewer: {out_path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--view", metavar="PATH",
                       help="also write a structured-grid resample of the final "
                            "bias point to PATH (.npz), openable in the native "
                            "desktop app's 3D viewer (File > Open result)")
    args = parser.parse_args()

    Na, Nsd = 1e17, 1e19  # p-type body/channel, n+ source/drain (finfet3d.py's own SMALL fixture scale)
    tox_cm = 2.0e-7
    Vfb = flatband_voltage(-Na, tox_cm, gate="n+poly")
    T = 300.0
    VT = thermal_voltage(T)

    print("Building the curved FinFET geometry (gmsh OCC: tapered + filleted)...")
    (nodes, tets, boundary_tris, region_of_tet, gate_tris, source_tris,
     drain_tris) = build_curved_finfet_mesh3d()
    print(f"  {nodes.shape[0]} nodes, {tets.shape[0]} tets, "
         f"{gate_tris.shape[0]} gate triangles, {source_tris.shape[0]} source, "
         f"{drain_tris.shape[0]} drain")

    C_phys = evaluate_doping_at_nodes3d(
        nodes, tets, region_of_tet, {"source": Nsd, "gate": -Na, "drain": Nsd})

    edges, node_vols = build_unstructured_stencil3d(nodes, tets)
    edges, trans = build_edge_flux_geometry3d(nodes, tets, edges)
    contacts = {"source": source_tris, "drain": drain_tris}
    gates = {"gate": {"faces": gate_tris, "tox_cm": tox_cm, "Vfb": Vfb, "Vg": 0.0}}

    print("Solving equilibrium...")
    psi_eq, scale = solve_poisson_equilibrium3d(
        nodes, tets, edges, node_vols, trans, C_phys, contacts,
        material=SILICON, gates=gates)
    print(f"  equilibrium psi range [{psi_eq.min() * VT:+.3f}, {psi_eq.max() * VT:+.3f}] V")

    Ns = scale["Ns"]
    nie_s = SILICON.ni(T) / Ns
    C_s = C_phys / Ns
    n0 = np.where(C_s >= 0, 0.5 * (C_s + np.sqrt(C_s ** 2 + 4 * nie_s ** 2)),
                 nie_s ** 2 / np.maximum(0.5 * (-C_s + np.sqrt(C_s ** 2 + 4 * nie_s ** 2)), 1e-300))
    p0 = nie_s ** 2 / np.maximum(n0, 1e-300)
    state = {"psi": psi_eq, "n": n0, "p": p0}

    opts = NewtonOptions()
    Vds = 0.05
    print(f"Ramping Vds to {Vds} V at Vg=0...")
    for vd in np.linspace(0.0, Vds, 6)[1:]:
        psi, n, p, _scale, currents = solve_bias3d(
            nodes, tets, edges, node_vols, trans, C_phys, contacts,
            {"source": 0.0, "drain": vd}, material=SILICON, gates=gates,
            init=state, opts=opts)
        state = {"psi": psi, "n": n, "p": p}

    print("Id-Vg sweep (Vds fixed):")
    Vg_list = np.linspace(0.0, 0.8, 5)
    Id = []
    for vg in Vg_list:
        gates["gate"]["Vg"] = vg
        psi, n, p, _scale, currents = solve_bias3d(
            nodes, tets, edges, node_vols, trans, C_phys, contacts,
            {"source": 0.0, "drain": Vds, "gate": vg}, material=SILICON,
            gates=gates, init=state, opts=opts)
        state = {"psi": psi, "n": n, "p": p}
        I_drain = currents["drain"]
        I_source = currents["source"]
        Id.append(I_drain)
        print(f"  Vg = {vg:+.3f} V   Id = {I_drain:+.6e} A   "
             f"(source+drain = {I_source + I_drain:+.2e} A, charge conservation check)")

    Id = np.array(Id)
    on_off = Id[-1] / max(abs(Id[0]), 1e-30)
    print(f"\nOn/off current ratio over this Vg sweep: {on_off:.3e}")

    if args.view:
        _export_structured_view(nodes, state["psi"], state["n"], state["p"],
                                C_phys, scale, T, args.view)

    print("Curved FinFET example finished.")


if __name__ == "__main__":
    main()
