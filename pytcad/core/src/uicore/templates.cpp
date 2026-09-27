#include "tcad/uicore/templates.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <sstream>
#include <unordered_map>

namespace tcad::uicore {

namespace {

TemplateParamInfo P(std::string name, std::string label, std::string unit, double def,
                     bool has_lo = false, double lo = 0.0, bool has_hi = false, double hi = 0.0,
                     bool integer = false) {
    TemplateParamInfo p;
    p.name = std::move(name);
    p.label = std::move(label);
    p.unit = std::move(unit);
    p.default_value = def;
    p.has_lo = has_lo;
    p.lo = lo;
    p.has_hi = has_hi;
    p.hi = hi;
    p.integer = integer;
    return p;
}

// Shorthand matching templates.py's TemplateParam(name, label, unit,
// default, lo=..., hi=..., integer=...) call sites.
TemplateParamInfo PL(std::string name, std::string label, std::string unit, double def,
                      double lo) {
    return P(std::move(name), std::move(label), std::move(unit), def, true, lo);
}
TemplateParamInfo PLH(std::string name, std::string label, std::string unit, double def,
                       double lo, double hi) {
    return P(std::move(name), std::move(label), std::move(unit), def, true, lo, true, hi);
}
TemplateParamInfo PLHI(std::string name, std::string label, std::string unit, double def,
                        double lo, double hi) {
    return P(std::move(name), std::move(label), std::move(unit), def, true, lo, true, hi, true);
}

using Values = std::unordered_map<std::string, double>;

double v(const Values& v_, const char* name) { return v_.at(name); }
int vi(const Values& v_, const char* name) { return static_cast<int>(v(v_, name)); }

RegionOut region(std::string id, std::string name, double xmin, double xmax, double ymin,
                  double ymax, double doping, std::string material = "") {
    RegionOut r;
    r.id = std::move(id);
    r.name = std::move(name);
    r.x_min = xmin;
    r.x_max = xmax;
    r.y_min = ymin;
    r.y_max = ymax;
    r.doping_cm3 = doping;
    r.material = std::move(material);
    return r;
}

ContactOut ohmic(std::string id, std::string name, double V, std::string edge,
                  bool has_range = false, double lo = 0.0, double hi = 0.0) {
    ContactOut c;
    c.id = std::move(id);
    c.name = std::move(name);
    c.kind = "ohmic";
    c.V = V;
    c.has_boundary = true;
    c.boundary.edge = std::move(edge);
    if (has_range) {
        c.boundary.has_range_lo = true;
        c.boundary.range_lo = lo;
        c.boundary.has_range_hi = true;
        c.boundary.range_hi = hi;
    }
    return c;
}

ContactOut gate(std::string id, std::string name, double V, std::string edge, double tox_cm,
                std::string vfb_mode, bool has_vfb_manual, double vfb_manual,
                bool has_range = false, double lo = 0.0, double hi = 0.0) {
    ContactOut c;
    c.id = std::move(id);
    c.name = std::move(name);
    c.kind = "gate";
    c.V = V;
    c.has_boundary = true;
    c.boundary.edge = std::move(edge);
    if (has_range) {
        c.boundary.has_range_lo = true;
        c.boundary.range_lo = lo;
        c.boundary.has_range_hi = true;
        c.boundary.range_hi = hi;
    }
    c.has_tox_cm = true;
    c.tox_cm = tox_cm;
    c.gate_type = "n+poly";
    c.vfb_mode = std::move(vfb_mode);
    c.has_vfb_manual = has_vfb_manual;
    c.vfb_manual = vfb_manual;
    return c;
}

// ------------------------------------------------------------------
//  builders -- one per template, mirroring templates.py's _build_*
// ------------------------------------------------------------------
DeviceOut build_resistor(const Values& p) {
    double w = v(p, "length_cm"), h = v(p, "height_cm");
    DeviceOut d;
    d.id = "resistor";
    d.name = "Resistor";
    d.width_cm = w;
    d.height_cm = h;
    d.mesh_nx = vi(p, "nx");
    d.mesh_ny = vi(p, "ny");
    d.regions = {region("body", "Body", 0.0, w, 0.0, h, v(p, "doping_cm3"))};
    d.contacts = {ohmic("left_c", "left", v(p, "v_left"), "left"),
                  ohmic("right_c", "right", v(p, "v_right"), "right")};
    return d;
}

DeviceOut build_pn_diode(const Values& p) {
    double w = v(p, "length_cm"), h = v(p, "height_cm"), mid = w / 2.0;
    DeviceOut d;
    d.id = "pn_diode";
    d.name = "P-N diode";
    d.width_cm = w;
    d.height_cm = h;
    d.mesh_nx = vi(p, "nx");
    d.mesh_ny = vi(p, "ny");
    d.regions = {region("p_side", "P side", 0.0, mid, 0.0, h, v(p, "na_cm3")),
                 region("n_side", "N side", mid, w, 0.0, h, v(p, "nd_cm3"))};
    d.contacts = {ohmic("p_c", "p", v(p, "v_p"), "left"), ohmic("n_c", "n", v(p, "v_n"), "right")};
    return d;
}

DeviceOut build_pin_diode(const Values& p) {
    double wp = v(p, "p_width_cm"), wi_ = v(p, "i_width_cm"), wn = v(p, "n_width_cm"),
           h = v(p, "height_cm");
    double w = wp + wi_ + wn;
    double x1 = wp, x2 = wp + wi_;
    DeviceOut d;
    d.id = "pin_diode";
    d.name = "P-i-N diode";
    d.width_cm = w;
    d.height_cm = h;
    d.mesh_nx = vi(p, "nx");
    d.mesh_ny = vi(p, "ny");
    d.regions = {region("p_side", "P+ side", 0.0, x1, 0.0, h, v(p, "na_cm3")),
                 region("intrinsic", "Intrinsic", x1, x2, 0.0, h, v(p, "ni_cm3")),
                 region("n_side", "N+ side", x2, w, 0.0, h, v(p, "nd_cm3"))};
    d.contacts = {ohmic("p_c", "p", v(p, "v_p"), "left"), ohmic("n_c", "n", v(p, "v_n"), "right")};
    return d;
}

DeviceOut build_mos_capacitor(const Values& p) {
    double w = v(p, "length_cm"), h = v(p, "height_cm");
    DeviceOut d;
    d.id = "mos_capacitor";
    d.name = "MOS capacitor";
    d.width_cm = w;
    d.height_cm = h;
    d.mesh_nx = vi(p, "nx");
    d.mesh_ny = vi(p, "ny");
    d.regions = {region("sub", "Substrate", 0.0, w, 0.0, h, v(p, "na_cm3"))};
    d.contacts = {ohmic("body_c", "body", 0.0, "bottom"),
                  gate("gate", "gate", 0.0, "top", v(p, "tox_cm"), "computed", false, 0.0)};
    return d;
}

DeviceOut build_nmos(const Values& p) {
    double lsd = v(p, "lsd_cm"), lg = v(p, "lg_cm"), h = v(p, "depth_cm");
    double w = 2.0 * lsd + lg;
    DeviceOut d;
    d.id = "nmos";
    d.name = "NMOS transistor";
    d.has_material = true;
    d.material = "Silicon";
    d.width_cm = w;
    d.height_cm = h;
    d.mesh_nx = 80;
    d.mesh_ny = 40;
    d.regions = {region("channel", "Channel", 0.0, w, 0.0, h, v(p, "na_channel_cm3")),
                 region("source", "Source", 0.0, lsd, 0.0, h, v(p, "nsd_cm3")),
                 region("drain", "Drain", lsd + lg, w, 0.0, h, v(p, "nsd_cm3"))};
    d.contacts = {
        ohmic("source_c", "source", 0.0, "top", true, 0.0, lsd),
        ohmic("drain_c", "drain", 0.05, "top", true, lsd + lg, w),
        ohmic("body_c", "body", 0.0, "bottom"),
        gate("gate", "gate", 1.0, "top", v(p, "tox_cm"), "computed", false, 0.0, true, lsd,
             lsd + lg)};
    return d;
}

DeviceOut build_hemt(const Values& p) {
    double w = v(p, "width_cm");
    double tb = v(p, "t_buffer_cm"), tc = v(p, "t_channel_cm"), tbar = v(p, "t_barrier_cm");
    double h = tb + tc + tbar;
    double y_ch_lo = tb, y_ch_hi = tb + tc;
    double lg = v(p, "lg_cm");
    double ls = std::max((w - lg) / 2.0, 0.0);
    DeviceOut d;
    d.id = "hemt";
    d.name = "AlGaAs/GaAs HEMT";
    d.width_cm = w;
    d.height_cm = h;
    d.mesh_nx = vi(p, "nx");
    d.mesh_ny = vi(p, "ny");
    d.regions = {
        region("buffer", "GaAs buffer", 0.0, w, 0.0, tb, v(p, "nd_buffer_cm3"), "GAAS"),
        region("channel", "GaAs channel", 0.0, w, y_ch_lo, y_ch_hi, v(p, "nd_channel_cm3"),
               "GAAS"),
        region("barrier", "Al0.3Ga0.7As barrier", 0.0, w, y_ch_hi, h, v(p, "nd_barrier_cm3"),
               "AL0.3GA0.7AS")};
    d.contacts = {ohmic("source_c", "source", 0.0, "top", true, 0.0, ls),
                  ohmic("drain_c", "drain", v(p, "v_ds"), "top", true, ls + lg, w),
                  gate("gate_c", "gate", 0.0, "top", v(p, "tox_cm"), "manual", true, -0.8, true,
                       ls, ls + lg),
                  ohmic("substrate_c", "substrate", 0.0, "bottom")};
    return d;
}

DeviceOut build_hbt(const Values& p) {
    double w = v(p, "width_cm");
    double te = v(p, "t_emitter_cm"), tb = v(p, "t_base_cm"), tc = v(p, "t_collector_cm");
    double h = te + tb + tc;
    double y_base_lo = tc, y_base_hi = tc + tb;
    double we = std::min(std::max(w * 0.5, 0.0), w);
    double x_e_lo = (w - we) / 2.0, x_e_hi = (w + we) / 2.0;
    DeviceOut d;
    d.id = "hbt";
    d.name = "AlGaAs/GaAs HBT";
    d.width_cm = w;
    d.height_cm = h;
    d.mesh_nx = vi(p, "nx");
    d.mesh_ny = vi(p, "ny");
    d.regions = {
        region("collector", "GaAs collector", 0.0, w, 0.0, tc, v(p, "nd_collector_cm3"), "GAAS"),
        region("base", "GaAs base", 0.0, w, y_base_lo, y_base_hi, -std::fabs(v(p, "na_base_cm3")),
               "GAAS"),
        region("emitter", "Al0.3Ga0.7As emitter", x_e_lo, x_e_hi, y_base_hi, h,
               std::fabs(v(p, "nd_emitter_cm3")), "AL0.3GA0.7AS")};
    d.contacts = {
        ohmic("emitter_c", "emitter", 0.0, "top", true, x_e_lo, x_e_hi),
        ohmic("base_c", "base", 0.0, "left", true, y_base_lo, y_base_hi),
        ohmic("collector_c", "collector", 0.0, "bottom")};
    return d;
}

// ------------------------------------------------------------------
//  registry
// ------------------------------------------------------------------
struct Entry {
    TemplateInfo info;
    std::function<DeviceOut(const Values&)> build;
};

const std::map<std::string, Entry>& registry() {
    static const std::map<std::string, Entry> reg = [] {
        std::map<std::string, Entry> r;
        r["resistor"] = {
            TemplateInfo{"resistor", "Resistor",
                         "A single uniformly-doped bar between two ohmic contacts -- "
                         "no junction, no gate. The simplest drift-diffusion device, "
                         "for recovering Ohm's law from first principles.",
                         {PL("length_cm", "Length", "cm", 1e-4, 1e-7),
                          PL("height_cm", "Height", "cm", 2e-5, 1e-7),
                          PLH("doping_cm3", "Doping (signed: + Nd / - Na)", "cm^-3", 1e17, -1e21,
                              1e21),
                          PLH("v_left", "Left contact bias", "V", 0.0, -50, 50),
                          PLH("v_right", "Right contact bias", "V", 0.1, -50, 50),
                          PLHI("nx", "Mesh nx", "nodes", 40, 8, 400),
                          PLHI("ny", "Mesh ny", "nodes", 10, 6, 400)}},
            build_resistor};
        r["pn_diode"] = {
            TemplateInfo{"pn_diode", "P-N diode",
                         "Abrupt junction formed by two uniformly doped rectangles.",
                         {PL("length_cm", "Length", "cm", 1e-4, 1e-7),
                          PL("height_cm", "Height", "cm", 2e-5, 1e-7),
                          PLH("na_cm3", "P-side doping (Na)", "cm^-3", -1e18, -1e21, 1e21),
                          PLH("nd_cm3", "N-side doping (Nd)", "cm^-3", 1e18, -1e21, 1e21),
                          PLH("v_p", "P contact bias", "V", 0.0, -50, 50),
                          PLH("v_n", "N contact bias", "V", 0.0, -50, 50),
                          PLHI("nx", "Mesh nx", "nodes", 40, 8, 400),
                          PLHI("ny", "Mesh ny", "nodes", 10, 6, 400)}},
            build_pn_diode};
        r["pin_diode"] = {
            TemplateInfo{"pin_diode", "P-i-N diode",
                         "P+ and n+ ohmics separated by a wide, nominally intrinsic "
                         "layer -- the depletion region spans the intrinsic layer "
                         "directly, giving a more linear C-V response and much higher "
                         "reverse breakdown than an abrupt p-n junction of the same "
                         "doping.",
                         {PL("p_width_cm", "P+ width", "cm", 2e-5, 1e-7),
                          PL("i_width_cm", "Intrinsic width", "cm", 4e-5, 1e-7),
                          PL("n_width_cm", "N+ width", "cm", 2e-5, 1e-7),
                          PL("height_cm", "Height", "cm", 2e-5, 1e-7),
                          PLH("na_cm3", "P+ doping (Na)", "cm^-3", -1e19, -1e21, 1e21),
                          PLH("nd_cm3", "N+ doping (Nd)", "cm^-3", 1e19, -1e21, 1e21),
                          PLH("ni_cm3", "Intrinsic layer net doping", "cm^-3", 1e13, -1e17, 1e17),
                          PLH("v_p", "P contact bias", "V", 0.0, -50, 50),
                          PLH("v_n", "N contact bias", "V", 0.0, -50, 50),
                          PLHI("nx", "Mesh nx", "nodes", 60, 8, 400),
                          PLHI("ny", "Mesh ny", "nodes", 10, 6, 400)}},
            build_pin_diode};
        r["mos_capacitor"] = {
            TemplateInfo{"mos_capacitor", "MOS capacitor",
                         "Uniform substrate with a poly gate over oxide; the classic C-V "
                         "teaching structure.",
                         {PL("length_cm", "Length", "cm", 1e-4, 1e-7),
                          PL("height_cm", "Height", "cm", 2e-5, 1e-7),
                          PLH("na_cm3", "Substrate doping (Na)", "cm^-3", -1e16, -1e21, 1e21),
                          PLH("tox_cm", "Oxide thickness", "cm", 1e-6, 1e-9, 1e-4),
                          PLHI("nx", "Mesh nx", "nodes", 40, 8, 400),
                          PLHI("ny", "Mesh ny", "nodes", 20, 6, 400)}},
            build_mos_capacitor};
        r["nmos"] = {
            TemplateInfo{"nmos", "NMOS transistor",
                         "Source / gated channel / drain with body contact; matches the "
                         "shipped MOSFET example at default parameters.",
                         {PL("lsd_cm", "Source/drain length", "cm", 3e-5, 1e-7),
                          PL("lg_cm", "Gate length", "cm", 6e-5, 1e-7),
                          PL("depth_cm", "Junction depth", "cm", 2e-5, 1e-7),
                          PLH("na_channel_cm3", "Channel doping", "cm^-3", -1e17, -1e21, 1e21),
                          PLH("nsd_cm3", "S/D doping", "cm^-3", 1e19, -1e21, 1e21),
                          PLH("tox_cm", "Oxide thickness", "cm", 5e-7, 1e-9, 1e-4)}},
            build_nmos};
        r["hemt"] = {
            TemplateInfo{
                "hemt", "AlGaAs/GaAs HEMT",
                "GaAs buffer / channel / Al0.3Ga0.7As barrier stack with a "
                "Schottky gate between top-surface source/drain ohmics; the "
                "2DEG emerges from the conduction-band step at the interface.",
                {PL("width_cm", "Device width", "cm", 5e-5, 1e-6),
                 PL("t_buffer_cm", "Buffer thickness", "cm", 5e-6, 1e-8),
                 PL("t_channel_cm", "Channel thickness", "cm", 2e-6, 1e-8),
                 PL("t_barrier_cm", "Barrier thickness", "cm", 3e-6, 1e-8),
                 PL("lg_cm", "Gate length", "cm", 1.5e-5, 1e-6),
                 PLH("nd_buffer_cm3", "Buffer doping (Nd)", "cm^-3", 1e14, -1e21, 1e21),
                 PLH("nd_channel_cm3", "Channel doping (Nd)", "cm^-3", 1e15, -1e21, 1e21),
                 PLH("nd_barrier_cm3", "Barrier doping (Nd)", "cm^-3", 1e18, -1e21, 1e21),
                 PLH("tox_cm", "Gate oxide thickness", "cm", 2e-6, 1e-9, 1e-4),
                 PLH("v_ds", "Drain bias", "V", 0.0, -20, 20),
                 PLHI("nx", "Mesh nx", "nodes", 40, 8, 400),
                 PLHI("ny", "Mesh ny", "nodes", 30, 8, 400)}},
            build_hemt};
        r["hbt"] = {
            TemplateInfo{
                "hbt", "AlGaAs/GaAs HBT",
                "Wide-gap n-AlGaAs emitter over a thin p+ GaAs base and an n "
                "GaAs collector; base ohmic on the left edge of the base layer.",
                {PL("width_cm", "Device width", "cm", 6e-5, 1e-6),
                 PL("t_emitter_cm", "Emitter thickness", "cm", 4e-6, 1e-8),
                 PL("t_base_cm", "Base thickness", "cm", 1.5e-6, 1e-8),
                 PL("t_collector_cm", "Collector thickness", "cm", 6e-6, 1e-8),
                 PLH("nd_emitter_cm3", "Emitter doping (Nd)", "cm^-3", 5e17, -1e21, 1e21),
                 PLH("na_base_cm3", "Base doping magnitude (Na)", "cm^-3", 5e18, 0.0, 1e21),
                 PLH("nd_collector_cm3", "Collector doping (Nd)", "cm^-3", 1e16, -1e21, 1e21),
                 PLHI("nx", "Mesh nx", "nodes", 26, 8, 400),
                 PLHI("ny", "Mesh ny", "nodes", 24, 8, 400)}},
            build_hbt};
        return r;
    }();
    return reg;
}

}  // namespace

std::vector<std::string> TemplateCatalog::list() {
    std::vector<std::string> ids;
    ids.reserve(registry().size());
    for (const auto& [id, entry] : registry()) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    return ids;
}

const TemplateInfo& TemplateCatalog::describe(const std::string& id) {
    auto it = registry().find(id);
    if (it == registry().end()) {
        std::string known;
        for (const auto& k : list()) {
            if (!known.empty()) known += ", ";
            known += k;
        }
        throw TemplateError(TemplateError::Kind::UnknownTemplate,
                            "unknown device template '" + id + "' (available: " + known + ")");
    }
    return it->second.info;
}

DeviceOut TemplateCatalog::build(const std::string& id,
                                  const std::vector<std::pair<std::string, double>>& values) {
    const TemplateInfo& info = describe(id);  // throws UnknownTemplate

    std::vector<std::string> known;
    known.reserve(info.params.size());
    for (const auto& p : info.params) known.push_back(p.name);

    std::vector<std::string> unknown;
    for (const auto& [name, val] : values) {
        (void)val;
        if (std::find(known.begin(), known.end(), name) == known.end()) unknown.push_back(name);
    }
    if (!unknown.empty()) {
        std::sort(unknown.begin(), unknown.end());
        std::sort(known.begin(), known.end());
        std::ostringstream msg;
        msg << "unknown parameter(s) [";
        for (size_t i = 0; i < unknown.size(); ++i) msg << (i ? ", " : "") << "'" << unknown[i]
                                                          << "'";
        msg << "] for template '" << id << "' (known: [";
        for (size_t i = 0; i < known.size(); ++i) msg << (i ? ", " : "") << "'" << known[i] << "'";
        msg << "])";
        throw TemplateError(TemplateError::Kind::InvalidConfig, msg.str());
    }

    Values supplied;
    for (const auto& [name, val] : values) supplied[name] = val;

    Values merged;
    for (const auto& p : info.params) {
        double val = supplied.count(p.name) ? supplied.at(p.name) : p.default_value;
        if (p.integer && std::floor(val) != val) {
            std::ostringstream msg;
            msg << "parameter '" << p.name << "' must be a whole number, got " << val;
            throw TemplateError(TemplateError::Kind::InvalidConfig, msg.str());
        }
        if (p.has_lo && val < p.lo) {
            std::ostringstream msg;
            msg << "parameter '" << p.name << "' must be >= " << p.lo << ", got " << val;
            throw TemplateError(TemplateError::Kind::InvalidConfig, msg.str());
        }
        if (p.has_hi && val > p.hi) {
            std::ostringstream msg;
            msg << "parameter '" << p.name << "' must be <= " << p.hi << ", got " << val;
            throw TemplateError(TemplateError::Kind::InvalidConfig, msg.str());
        }
        merged[p.name] = val;
    }

    return registry().at(id).build(merged);
}

}  // namespace tcad::uicore
