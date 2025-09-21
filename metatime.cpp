#include <iostream>
#include <iomanip>
#include <random>
#include <cmath>
#include <cstring>    // strcmp
#include <string>
#include <sstream>

#include "RandomGenPolyShape.hpp"
#include "time2d_m2.h"
#include "time2d_i2.h"
#include "time2d_w2.h"
#include "time2d_macros.h"
#include "time2d_interface.h"  // t2d::iface::*

// -------- seeds util --------
static uint64_t make_seed() {
    std::random_device rd;
    uint64_t a = ((uint64_t)rd() << 32) ^ (uint64_t)rd();
    return a ? a : 0xA5A5A5A5ULL; // fallback
}

// ---- util: re-simuler i2 avec un facteur multiplicateur sur life_mean ----
static t2d::I2Plan simulate_with_factor(const t2d::M2Plan& m2, const t2d::I2Params& base, double f) {
    t2d::I2Params p = base;
    p.life_mean = std::max(1e-9, base.life_mean * f);
    return t2d::generate_i2(m2, p);
}

// ---- parse helper ----
static bool starts_with(const char* s, const char* pref) { return std::strncmp(s, pref, std::strlen(pref)) == 0; }
static double parse_double_arg(const char* arg, const char* key, double defv) {
    if (!starts_with(arg, key)) return defv;
    const char* p = arg + std::strlen(key);
    return std::atof(p);
}
static long long parse_ll_arg(const char* arg, const char* key, long long defv) {
    if (!starts_with(arg, key)) return defv;
    const char* p = arg + std::strlen(key);
    return std::strtoll(p, nullptr, 0);
}
static int parse_int_arg(const char* arg, const char* key, int defv) {
    if (!starts_with(arg, key)) return defv;
    const char* p = arg + std::strlen(key);
    return std::atoi(p);
}

int main(int argc, char** argv) {
    using namespace t2d;
    using namespace t2dgen;
    using t2d::iface::Inputs;
    using t2d::iface::W2Inputs;

    bool want_json = false;

    // --------- defaults (UI) ----------
    t2d::iface::Inputs ui_in;          // RET 4, READ 10.0 par défaut
    t2d::iface::W2Inputs uiw2_in;      // subdiv=3, offset=0.15, etc.

    // --------- parse CLI ----------
    for (int i=1;i<argc;++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--json")==0)                     want_json = true;
        else if (starts_with(a, "--ret="))                   ui_in.TIME_RETENTION_FACTOR = parse_int_arg(a, "--ret=", ui_in.TIME_RETENTION_FACTOR);
        else if (starts_with(a, "--read="))                  ui_in.CONTAINER_TIME_READ = parse_double_arg(a, "--read=", ui_in.CONTAINER_TIME_READ);
        else if (starts_with(a, "--support="))               uiw2_in.PROCESS_SUPPORT_TIME = parse_double_arg(a, "--support=", uiw2_in.PROCESS_SUPPORT_TIME);
        else if (starts_with(a, "--corpse="))                uiw2_in.ENVIRONNMENT_CORPSE_TIME = parse_double_arg(a, "--corpse=", uiw2_in.ENVIRONNMENT_CORPSE_TIME);
        else if (starts_with(a, "--subdiv="))                uiw2_in.subdivision_level = parse_int_arg(a, "--subdiv=", uiw2_in.subdivision_level);
        else if (starts_with(a, "--offset="))                uiw2_in.offset_step = parse_double_arg(a, "--offset=", uiw2_in.offset_step);
        else if (starts_with(a, "--exist="))                 uiw2_in.PROCESS_EXISTENCE_TIME = parse_double_arg(a, "--exist=", uiw2_in.PROCESS_EXISTENCE_TIME);
        else if (starts_with(a, "--recover="))               uiw2_in.ENVIRONNMENT_RECOVER_TIME = parse_double_arg(a, "--recover=", uiw2_in.ENVIRONNMENT_RECOVER_TIME);
        // facultatif : seed source
        // (tu peux aussi exposer genP.seed/m2.seed/ip.seed si tu veux une reproductibilité totale)
    }

    // sanitize UI
    t2d::iface::Inputs ui = t2d::iface::sanitize(ui_in);
    t2d::iface::W2Inputs uiw2 = t2d::iface::sanitize(uiw2_in);

    // =========================
    // PHASE 1 : Génération forme
    // =========================
    std::cout << std::fixed << std::setprecision(6);
    RandomGenPolyShape::Params genP;
    genP.base_size = 1.0;
    genP.child_scale = 0.25;
    genP.min_sides = 3;
    genP.max_sides = 8;
    genP.seed = make_seed();

    RandomGenPolyShape gen(genP);
    Shape shape = gen.generate();
    const int r = gen.iterations();
    const int N = gen.totalVertices();

    // ====================
    // PHASE 2 : M2 (foudre)
    // ====================
    M2Params m2;
    m2.replicas_k = std::min(std::max(1, N / 20), std::max(1, N - 1));
    m2.thunder_span = 24;
    m2.thunder_jitter = 0.8;
    m2.replica_rate = 0.6;
    m2.magmat_span = 60;
    m2.seed = make_seed();
    M2Plan plan_m2 = generate_m2(shape, m2);

    const double T = (plan_m2.replicas_effective > 0) ? double(N)/double(plan_m2.replicas_effective) : INFINITY;
    const double inv = (std::isfinite(T)&&T>0.0) ? 1.0/T : 0.0;
    const long long FT = std::isfinite(T) ? (long long)std::floor(T) : 0;

    // ==========================
    // PHASE 3 : I2 (terre+glace)
    // ==========================
    I2Params ip;
    ip.total_vertices_n = N;
    ip.iterations_inherited = r;
    ip.force_rate = 0.05;
    ip.life_mean = 10.0;
    ip.life_jitter = 0.20;
    ip.sample_max = 10;
    ip.seed = make_seed();
    I2Plan plan_i2 = generate_i2(plan_m2, ip);

    // ==========================
    // CIBLES “glace” (divisibilité)
    // ==========================
    const int lost_now = plan_i2.grains_lost;
    const int Ntot     = plan_i2.grains_total;
    const int target_lost_exact = std::max(0, lost_now / ui.TIME_RETENTION_FACTOR);
    const int lost_remainder    = lost_now - target_lost_exact * ui.TIME_RETENTION_FACTOR;
    const int target_mem_min    = std::max(0, Ntot - target_lost_exact);

    LatencyTargets targets;
    targets.target_mem_min    = target_mem_min;
    targets.target_lost_exact = target_lost_exact;
    targets.f_lo = 0.10; targets.f_hi = 10.0; targets.max_iter = 40;

    MacroParams mparams; // edge_share=0.20

    // ==========================
    // W2 : structure VENT
    // ==========================
    W2Params w2p;
    w2p.subdivision_level = uiw2.subdivision_level;
    w2p.offset_step       = uiw2.offset_step;
    W2Plan w2 = generate_w2_structure(plan_i2, w2p);

    // W2 : macros (VENT/BOIS)
    W2MacroControls w2c;
    w2c.PROCESS_SUPPORT_TIME      = uiw2.PROCESS_SUPPORT_TIME;
    w2c.PROCESS_EXISTENCE_TIME    = uiw2.PROCESS_EXISTENCE_TIME;
    w2c.ENVIRONNMENT_CORPSE_TIME  = uiw2.ENVIRONNMENT_CORPSE_TIME;
    w2c.ENVIRONNMENT_RECOVER_TIME = uiw2.ENVIRONNMENT_RECOVER_TIME;

    // ==========================
    // MACROS (avec W2 overload)
    // ==========================
    t2d::Macros MX = t2d::compute_macros(plan_i2, ip, plan_m2, w2, w2c, targets, mparams);

    // ==========================
    // Projection & lisibilité
    // ==========================
    I2Plan proj = simulate_with_factor(plan_m2, ip, MX.MEMORY_LATENCY_TIME_FACTOR_high);
    const double center_share         = 1.0 - mparams.edge_share;
    const int    readable_capacity    = (int)std::floor(ui.CONTAINER_TIME_READ / std::max(1e-12, proj.service_time));
    const int    readable_center      = (int)std::floor(readable_capacity * center_share);
    const int    readable_effective   = std::min(proj.grains_memorized, readable_center);

    // Durées de vie des empreintes actives (modèle console)
    // life_k = CORPSE / (1 + SUPPORT * k)
    std::vector<double> active_lives;
    active_lives.reserve(MX.W2_ACTIVE);
    for (int k=1; k<=MX.W2_ACTIVE && k<(int)w2.slots.size(); ++k) {
        const double atten = 1.0 / (1.0 + std::max(0.0, w2c.PROCESS_SUPPORT_TIME) * (double)k);
        const double life  = std::max(0.0, w2c.ENVIRONNMENT_CORPSE_TIME * atten);
        active_lives.push_back(life);
    }

    if (want_json) {
        // -------- JSON compact (sans dépendance) --------
        auto q = [](const std::string& s){ std::ostringstream o; o<<'\"'<<s<<'\"'; return o.str(); };

        std::ostringstream out;
        out << "{";

        // version
        out << q("version") << ":" << q(t2d::iface::Version::tag) << ",";

        // inputs
        out << q("inputs") << ":{"
            << q("time_retention_factor") << ":" << ui.TIME_RETENTION_FACTOR << ","
            << q("container_time_read")   << ":" << ui.CONTAINER_TIME_READ   << ","
            << q("subdivision_level")     << ":" << uiw2.subdivision_level   << ","
            << q("offset_step")           << ":" << uiw2.offset_step         << ","
            << q("process_support_time")  << ":" << uiw2.PROCESS_SUPPORT_TIME<< ","
            << q("process_existence_time")<< ":" << uiw2.PROCESS_EXISTENCE_TIME<< ","
            << q("environment_corpse_time")<<":"<< uiw2.ENVIRONNMENT_CORPSE_TIME<< ","
            << q("environment_recover_time")<<":"<< uiw2.ENVIRONNMENT_RECOVER_TIME
            << "},";

        // counters
        out << q("counters") << ":{"
            << q("N")         << ":" << plan_i2.grains_total     << ","
            << q("memorized") << ":" << plan_i2.grains_memorized << ","
            << q("lost")      << ":" << plan_i2.grains_lost
            << "},";

        // targets
        out << q("targets") << ":{"
            << q("retention_factor") << ":" << ui.TIME_RETENTION_FACTOR << ","
            << q("target_lost")      << ":" << target_lost_exact        << ","
            << q("target_mem_min")   << ":" << target_mem_min           << ","
            << q("lost_remainder")   << ":" << lost_remainder
            << "},";

        // macros (engine)
        out << q("macros") << ":{"
            << q("MEMORY_SPREAD_TIME_CONSTRAINT_pct") << ":" << MX.MEMORY_SPREAD_TIME_CONSTRAINT_pct << ","
            << q("MEMORY_LATENCY_TIME_FACTOR_low")    << ":" << MX.MEMORY_LATENCY_TIME_FACTOR_low    << ","
            << q("MEMORY_LATENCY_TIME_FACTOR_high")   << ":" << MX.MEMORY_LATENCY_TIME_FACTOR_high   << ","
            << q("CONTAINER_RANGE_TIME")              << ":" << MX.CONTAINER_RANGE_TIME              << ","
            << q("CONTAINER_FLOW_TIME")               << ":" << MX.CONTAINER_FLOW_TIME               << ","
            << q("W2_SUBDIVISION_LEVEL")              << ":" << MX.W2_SUBDIVISION_LEVEL              << ","
            << q("W2_OFFSET_STEP")                    << ":" << MX.W2_OFFSET_STEP                    << ","
            << q("W2_REBOUNDS_CAPACITY")              << ":" << MX.W2_REBOUNDS_CAPACITY              << ","
            << q("W2_REBOUNDS_TARGET")                << ":" << MX.W2_REBOUNDS_TARGET                << ","
            << q("W2_ACTIVE")                          << ":" << MX.W2_ACTIVE                         << ","
            << q("W2_DISAPPEARED")                    << ":" << MX.W2_DISAPPEARED
            << "},";

        // projection
        out << q("projection_high") << ":{"
            << q("projected_memorized")     << ":" << proj.grains_memorized << ","
            << q("projected_lost")          << ":" << proj.grains_lost      << ","
            << q("projected_service_time")  << ":" << proj.service_time     << ","
            << q("container_time_read")     << ":" << ui.CONTAINER_TIME_READ<< ","
            << q("readable_capacity")       << ":" << readable_capacity     << ","
            << q("readable_effective")      << ":" << readable_effective
            << "},";

        // active footprints
        out << q("active_footprints") << ":[";
        for (size_t i=0;i<active_lives.size();++i) {
            const int k = (int)i+1;
            const auto& slot = w2.slots[k];
            out << "{"
                << q("k")        << ":" << k << ","
                << q("subdiv")   << ":" << slot.subdivision << ","
                << q("offset")   << ":" << slot.offset      << ","
                << q("life")     << ":" << active_lives[i]
                << "}";
            if (i+1<active_lives.size()) out << ",";
        }
        out << "]";

        out << "}";
        std::cout << out.str() << std::endl;
        return 0;
    }

    // ---------- mode console “humain” (inchangé dans l’esprit) ----------
    std::cout << "=== PHASE 1 : INIT (forme initiale) ===\n";
    std::cout << "seed(shape)         = 0x" << std::hex << genP.seed << std::dec << "\n";
    std::cout << "Ordre d'iteration r = " << r << "\n";
    std::cout << "Nombre sommets N    = " << N << "\n";
    std::cout << "Segments E          = " << shape.E.size() << "\n\n";

    std::cout << "=== PHASE 2 : FOUDRE ===\n";
    std::cout << "Replicas k          = " << plan_m2.replicas_effective << "\n";
    std::cout << "Min gap (reel)      = " << plan_m2.thunder_min_gap << "\n";
    std::cout << "Tau (reel)          = " << plan_m2.thunder_tau << "\n\n";
    std::cout << "T = N/k              = " << T  << "\n";
    std::cout << "Inverse(T)           = " << inv<< "\n";
    std::cout << "Partie entiere de T  = " << FT << "\n\n";

    std::cout << "=== PHASE 3 : I2 (Terre+Glace) ===\n";
    std::cout << "Grains total (N)     = " << plan_i2.grains_total << "\n";
    std::cout << "Passage dimension    = " << plan_i2.passage_dimension << "\n";
    std::cout << "Throughput (gr/s)    = " << plan_i2.throughput << "\n";
    std::cout << "Service time         = " << plan_i2.service_time << "\n";
    std::cout << "Memorized            = " << plan_i2.grains_memorized << "\n";
    std::cout << "Lost (oubli)         = " << plan_i2.grains_lost << "\n\n";

    std::cout << "=== CIBLES (glace) ===\n";
    std::cout << "lost_now=" << lost_now
              << " -> target_lost=floor(lost/RET)=" << target_lost_exact
              << " (RET=" << ui.TIME_RETENTION_FACTOR << ", reste=" << lost_remainder << ")\n";
    std::cout << "target_mem_min = " << target_mem_min << " / N=" << Ntot << "\n\n";

    std::cout << "=== W2 (interface) ===\n";
    std::cout << "subdiv_level=" << w2.subdivision_level
              << " offset_step=" << w2.offset_step
              << " capacity="   << w2.rebounds_capacity << "\n";
    std::cout << "PROCESS_SUPPORT_TIME=" << uiw2.PROCESS_SUPPORT_TIME
              << "  PROCESS_EXISTENCE_TIME=" << uiw2.PROCESS_EXISTENCE_TIME << "\n";
    std::cout << "ENVIRONNEMENT_CORPSE_TIME=" << uiw2.ENVIRONNMENT_CORPSE_TIME
              << "  ENVIRONNEMENT_RECOVER_TIME="<< uiw2.ENVIRONNMENT_RECOVER_TIME << "\n\n";

    std::cout << "=== MACROS dynamiques ===\n";
    std::cout << "MEMORY_LATENCY_TIME_FACTOR (low)="  << MX.MEMORY_LATENCY_TIME_FACTOR_low  << "\n";
    std::cout << "MEMORY_LATENCY_TIME_FACTOR (high)=" << MX.MEMORY_LATENCY_TIME_FACTOR_high << "\n";
    std::cout << "W2: capacity=" << MX.W2_REBOUNDS_CAPACITY
              << " target=" << MX.W2_REBOUNDS_TARGET
              << " actifs=" << MX.W2_ACTIVE
              << " disparus=" << MX.W2_DISAPPEARED << "\n\n";

    std::cout << "Empreintes actives (k, subdiv, offset, life):\n";
    for (int k=1; k<=MX.W2_ACTIVE && k<(int)w2.slots.size(); ++k) {
        const auto& slot = w2.slots[k];
        const double atten = 1.0 / (1.0 + std::max(0.0, w2c.PROCESS_SUPPORT_TIME) * (double)k);
        const double life  = std::max(0.0, w2c.ENVIRONNMENT_CORPSE_TIME * atten);
        std::cout << "  k=" << k
                  << "  subdiv=" << slot.subdivision
                  << "  offset=" << slot.offset
                  << "  life=" << life << "\n";
    }
    return 0;
}
#include <iostream>
#include <iomanip>
#include <random>
#include <cmath>
#include <cstring>    // strcmp
#include <string>
#include <sstream>

#include "RandomGenPolyShape.hpp"
#include "time2d_m2.h"
#include "time2d_i2.h"
#include "time2d_w2.h"
#include "time2d_macros.h"
#include "time2d_interface.h"  // t2d::iface::*

// -------- seeds util --------
static uint64_t make_seed() {
    std::random_device rd;
    uint64_t a = ((uint64_t)rd() << 32) ^ (uint64_t)rd();
    return a ? a : 0xA5A5A5A5ULL; // fallback
}

// ---- util: re-simuler i2 avec un facteur multiplicateur sur life_mean ----
static t2d::I2Plan simulate_with_factor(const t2d::M2Plan& m2, const t2d::I2Params& base, double f) {
    t2d::I2Params p = base;
    p.life_mean = std::max(1e-9, base.life_mean * f);
    return t2d::generate_i2(m2, p);
}

// ---- parse helper ----
static bool starts_with(const char* s, const char* pref) { return std::strncmp(s, pref, std::strlen(pref)) == 0; }
static double parse_double_arg(const char* arg, const char* key, double defv) {
    if (!starts_with(arg, key)) return defv;
    const char* p = arg + std::strlen(key);
    return std::atof(p);
}
static long long parse_ll_arg(const char* arg, const char* key, long long defv) {
    if (!starts_with(arg, key)) return defv;
    const char* p = arg + std::strlen(key);
    return std::strtoll(p, nullptr, 0);
}
static int parse_int_arg(const char* arg, const char* key, int defv) {
    if (!starts_with(arg, key)) return defv;
    const char* p = arg + std::strlen(key);
    return std::atoi(p);
}

int main(int argc, char** argv) {
    using namespace t2d;
    using namespace t2dgen;
    using t2d::iface::Inputs;
    using t2d::iface::W2Inputs;

    bool want_json = false;

    // --------- defaults (UI) ----------
    t2d::iface::Inputs ui_in;          // RET 4, READ 10.0 par défaut
    t2d::iface::W2Inputs uiw2_in;      // subdiv=3, offset=0.15, etc.

    // --------- parse CLI ----------
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--json") == 0)                     want_json = true;
        else if (starts_with(a, "--ret="))                   ui_in.TIME_RETENTION_FACTOR = parse_int_arg(a, "--ret=", ui_in.TIME_RETENTION_FACTOR);
        else if (starts_with(a, "--read="))                  ui_in.CONTAINER_TIME_READ = parse_double_arg(a, "--read=", ui_in.CONTAINER_TIME_READ);
        else if (starts_with(a, "--support="))               uiw2_in.PROCESS_SUPPORT_TIME = parse_double_arg(a, "--support=", uiw2_in.PROCESS_SUPPORT_TIME);
        else if (starts_with(a, "--corpse="))                uiw2_in.ENVIRONNMENT_CORPSE_TIME = parse_double_arg(a, "--corpse=", uiw2_in.ENVIRONNMENT_CORPSE_TIME);
        else if (starts_with(a, "--subdiv="))                uiw2_in.subdivision_level = parse_int_arg(a, "--subdiv=", uiw2_in.subdivision_level);
        else if (starts_with(a, "--offset="))                uiw2_in.offset_step = parse_double_arg(a, "--offset=", uiw2_in.offset_step);
        else if (starts_with(a, "--exist="))                 uiw2_in.PROCESS_EXISTENCE_TIME = parse_double_arg(a, "--exist=", uiw2_in.PROCESS_EXISTENCE_TIME);
        else if (starts_with(a, "--recover="))               uiw2_in.ENVIRONNMENT_RECOVER_TIME = parse_double_arg(a, "--recover=", uiw2_in.ENVIRONNMENT_RECOVER_TIME);
        // facultatif : seed source
        // (tu peux aussi exposer genP.seed/m2.seed/ip.seed si tu veux une reproductibilité totale)
    }

    // sanitize UI
    t2d::iface::Inputs ui = t2d::iface::sanitize(ui_in);
    t2d::iface::W2Inputs uiw2 = t2d::iface::sanitize(uiw2_in);

    // =========================
    // PHASE 1 : Génération forme
    // =========================
    std::cout << std::fixed << std::setprecision(6);
    RandomGenPolyShape::Params genP;
    genP.base_size = 1.0;
    genP.child_scale = 0.25;
    genP.min_sides = 3;
    genP.max_sides = 8;
    genP.seed = make_seed();

    RandomGenPolyShape gen(genP);
    Shape shape = gen.generate();
    const int r = gen.iterations();
    const int N = gen.totalVertices();

    // ====================
    // PHASE 2 : M2 (foudre)
    // ====================
    M2Params m2;
    m2.replicas_k = std::min(std::max(1, N / 20), std::max(1, N - 1));
    m2.thunder_span = 24;
    m2.thunder_jitter = 0.8;
    m2.replica_rate = 0.6;
    m2.magmat_span = 60;
    m2.seed = make_seed();
    M2Plan plan_m2 = generate_m2(shape, m2);

    const double T = (plan_m2.replicas_effective > 0) ? double(N) / double(plan_m2.replicas_effective) : INFINITY;
    const double inv = (std::isfinite(T) && T > 0.0) ? 1.0 / T : 0.0;
    const long long FT = std::isfinite(T) ? (long long)std::floor(T) : 0;

    // ==========================
    // PHASE 3 : I2 (terre+glace)
    // ==========================
    I2Params ip;
    ip.total_vertices_n = N;
    ip.iterations_inherited = r;
    ip.force_rate = 0.05;
    ip.life_mean = 10.0;
    ip.life_jitter = 0.20;
    ip.sample_max = 10;
    ip.seed = make_seed();
    I2Plan plan_i2 = generate_i2(plan_m2, ip);

    // ==========================
    // CIBLES “glace” (divisibilité)
    // ==========================
    const int lost_now = plan_i2.grains_lost;
    const int Ntot = plan_i2.grains_total;
    const int target_lost_exact = std::max(0, lost_now / ui.TIME_RETENTION_FACTOR);
    const int lost_remainder = lost_now - target_lost_exact * ui.TIME_RETENTION_FACTOR;
    const int target_mem_min = std::max(0, Ntot - target_lost_exact);

    LatencyTargets targets;
    targets.target_mem_min = target_mem_min;
    targets.target_lost_exact = target_lost_exact;
    targets.f_lo = 0.10; targets.f_hi = 10.0; targets.max_iter = 40;

    MacroParams mparams; // edge_share=0.20

    // ==========================
    // W2 : structure VENT
    // ==========================
    W2Params w2p;
    w2p.subdivision_level = uiw2.subdivision_level;
    w2p.offset_step = uiw2.offset_step;
    W2Plan w2 = generate_w2_structure(plan_i2, w2p);

    // W2 : macros (VENT/BOIS)
    W2MacroControls w2c;
    w2c.PROCESS_SUPPORT_TIME = uiw2.PROCESS_SUPPORT_TIME;
    w2c.PROCESS_EXISTENCE_TIME = uiw2.PROCESS_EXISTENCE_TIME;
    w2c.ENVIRONNMENT_CORPSE_TIME = uiw2.ENVIRONNMENT_CORPSE_TIME;
    w2c.ENVIRONNMENT_RECOVER_TIME = uiw2.ENVIRONNMENT_RECOVER_TIME;

    // ==========================
    // MACROS (avec W2 overload)
    // ==========================
    t2d::Macros MX = t2d::compute_macros(plan_i2, ip, plan_m2, w2, w2c, targets, mparams);

    // ==========================
    // Projection & lisibilité
    // ==========================
    I2Plan proj = simulate_with_factor(plan_m2, ip, MX.MEMORY_LATENCY_TIME_FACTOR_high);
    const double center_share = 1.0 - mparams.edge_share;
    const int    readable_capacity = (int)std::floor(ui.CONTAINER_TIME_READ / std::max(1e-12, proj.service_time));
    const int    readable_center = (int)std::floor(readable_capacity * center_share);
    const int    readable_effective = std::min(proj.grains_memorized, readable_center);

    // Durées de vie des empreintes actives (modèle console)
    // life_k = CORPSE / (1 + SUPPORT * k)
    std::vector<double> active_lives;
    active_lives.reserve(MX.W2_ACTIVE);
    for (int k = 1; k <= MX.W2_ACTIVE && k < (int)w2.slots.size(); ++k) {
        const double atten = 1.0 / (1.0 + std::max(0.0, w2c.PROCESS_SUPPORT_TIME) * (double)k);
        const double life = std::max(0.0, w2c.ENVIRONNMENT_CORPSE_TIME * atten);
        active_lives.push_back(life);
    }

    if (want_json) {
        // -------- JSON compact (sans dépendance) --------
        auto q = [](const std::string& s) { std::ostringstream o; o << '\"' << s << '\"'; return o.str(); };

        std::ostringstream out;
        out << "{";

        // version
        out << q("version") << ":" << q(t2d::iface::Version::tag) << ",";

        // inputs
        out << q("inputs") << ":{"
            << q("time_retention_factor") << ":" << ui.TIME_RETENTION_FACTOR << ","
            << q("container_time_read") << ":" << ui.CONTAINER_TIME_READ << ","
            << q("subdivision_level") << ":" << uiw2.subdivision_level << ","
            << q("offset_step") << ":" << uiw2.offset_step << ","
            << q("process_support_time") << ":" << uiw2.PROCESS_SUPPORT_TIME << ","
            << q("process_existence_time") << ":" << uiw2.PROCESS_EXISTENCE_TIME << ","
            << q("environment_corpse_time") << ":" << uiw2.ENVIRONNMENT_CORPSE_TIME << ","
            << q("environment_recover_time") << ":" << uiw2.ENVIRONNMENT_RECOVER_TIME
            << "},";

        // counters
        out << q("counters") << ":{"
            << q("N") << ":" << plan_i2.grains_total << ","
            << q("memorized") << ":" << plan_i2.grains_memorized << ","
            << q("lost") << ":" << plan_i2.grains_lost
            << "},";

        // targets
        out << q("targets") << ":{"
            << q("retention_factor") << ":" << ui.TIME_RETENTION_FACTOR << ","
            << q("target_lost") << ":" << target_lost_exact << ","
            << q("target_mem_min") << ":" << target_mem_min << ","
            << q("lost_remainder") << ":" << lost_remainder
            << "},";

        // macros (engine)
        out << q("macros") << ":{"
            << q("MEMORY_SPREAD_TIME_CONSTRAINT_pct") << ":" << MX.MEMORY_SPREAD_TIME_CONSTRAINT_pct << ","
            << q("MEMORY_LATENCY_TIME_FACTOR_low") << ":" << MX.MEMORY_LATENCY_TIME_FACTOR_low << ","
            << q("MEMORY_LATENCY_TIME_FACTOR_high") << ":" << MX.MEMORY_LATENCY_TIME_FACTOR_high << ","
            << q("CONTAINER_RANGE_TIME") << ":" << MX.CONTAINER_RANGE_TIME << ","
            << q("CONTAINER_FLOW_TIME") << ":" << MX.CONTAINER_FLOW_TIME << ","
            << q("W2_SUBDIVISION_LEVEL") << ":" << MX.W2_SUBDIVISION_LEVEL << ","
            << q("W2_OFFSET_STEP") << ":" << MX.W2_OFFSET_STEP << ","
            << q("W2_REBOUNDS_CAPACITY") << ":" << MX.W2_REBOUNDS_CAPACITY << ","
            << q("W2_REBOUNDS_TARGET") << ":" << MX.W2_REBOUNDS_TARGET << ","
            << q("W2_ACTIVE") << ":" << MX.W2_ACTIVE << ","
            << q("W2_DISAPPEARED") << ":" << MX.W2_DISAPPEARED
            << "},";

        // projection
        out << q("projection_high") << ":{"
            << q("projected_memorized") << ":" << proj.grains_memorized << ","
            << q("projected_lost") << ":" << proj.grains_lost << ","
            << q("projected_service_time") << ":" << proj.service_time << ","
            << q("container_time_read") << ":" << ui.CONTAINER_TIME_READ << ","
            << q("readable_capacity") << ":" << readable_capacity << ","
            << q("readable_effective") << ":" << readable_effective
            << "},";

        // active footprints
        out << q("active_footprints") << ":[";
        for (size_t i = 0; i < active_lives.size(); ++i) {
            const int k = (int)i + 1;
            const auto& slot = w2.slots[k];
            out << "{"
                << q("k") << ":" << k << ","
                << q("subdiv") << ":" << slot.subdivision << ","
                << q("offset") << ":" << slot.offset << ","
                << q("life") << ":" << active_lives[i]
                << "}";
            if (i + 1 < active_lives.size()) out << ",";
        }
        out << "]";

        out << "}";
        std::cout << out.str() << std::endl;
        return 0;
    }

    // ---------- mode console “humain” (inchangé dans l’esprit) ----------
    std::cout << "=== PHASE 1 : INIT (forme initiale) ===\n";
    std::cout << "seed(shape)         = 0x" << std::hex << genP.seed << std::dec << "\n";
    std::cout << "Ordre d'iteration r = " << r << "\n";
    std::cout << "Nombre sommets N    = " << N << "\n";
    std::cout << "Segments E          = " << shape.E.size() << "\n\n";

    std::cout << "=== PHASE 2 : FOUDRE ===\n";
    std::cout << "Replicas k          = " << plan_m2.replicas_effective << "\n";
    std::cout << "Min gap (reel)      = " << plan_m2.thunder_min_gap << "\n";
    std::cout << "Tau (reel)          = " << plan_m2.thunder_tau << "\n\n";
    std::cout << "T = N/k              = " << T << "\n";
    std::cout << "Inverse(T)           = " << inv << "\n";
    std::cout << "Partie entiere de T  = " << FT << "\n\n";

    std::cout << "=== PHASE 3 : I2 (Terre+Glace) ===\n";
    std::cout << "Grains total (N)     = " << plan_i2.grains_total << "\n";
    std::cout << "Passage dimension    = " << plan_i2.passage_dimension << "\n";
    std::cout << "Throughput (gr/s)    = " << plan_i2.throughput << "\n";
    std::cout << "Service time         = " << plan_i2.service_time << "\n";
    std::cout << "Memorized            = " << plan_i2.grains_memorized << "\n";
    std::cout << "Lost (oubli)         = " << plan_i2.grains_lost << "\n\n";

    std::cout << "=== CIBLES (glace) ===\n";
    std::cout << "lost_now=" << lost_now
        << " -> target_lost=floor(lost/RET)=" << target_lost_exact
        << " (RET=" << ui.TIME_RETENTION_FACTOR << ", reste=" << lost_remainder << ")\n";
    std::cout << "target_mem_min = " << target_mem_min << " / N=" << Ntot << "\n\n";

    std::cout << "=== W2 (interface) ===\n";
    std::cout << "subdiv_level=" << w2.subdivision_level
        << " offset_step=" << w2.offset_step
        << " capacity=" << w2.rebounds_capacity << "\n";
    std::cout << "PROCESS_SUPPORT_TIME=" << uiw2.PROCESS_SUPPORT_TIME
        << "  PROCESS_EXISTENCE_TIME=" << uiw2.PROCESS_EXISTENCE_TIME << "\n";
    std::cout << "ENVIRONNEMENT_CORPSE_TIME=" << uiw2.ENVIRONNMENT_CORPSE_TIME
        << "  ENVIRONNEMENT_RECOVER_TIME=" << uiw2.ENVIRONNMENT_RECOVER_TIME << "\n\n";

    std::cout << "=== MACROS dynamiques ===\n";
    std::cout << "MEMORY_LATENCY_TIME_FACTOR (low)=" << MX.MEMORY_LATENCY_TIME_FACTOR_low << "\n";
    std::cout << "MEMORY_LATENCY_TIME_FACTOR (high)=" << MX.MEMORY_LATENCY_TIME_FACTOR_high << "\n";
    std::cout << "W2: capacity=" << MX.W2_REBOUNDS_CAPACITY
        << " target=" << MX.W2_REBOUNDS_TARGET
        << " actifs=" << MX.W2_ACTIVE
        << " disparus=" << MX.W2_DISAPPEARED << "\n\n";

    std::cout << "Empreintes actives (k, subdiv, offset, life):\n";
    for (int k = 1; k <= MX.W2_ACTIVE && k < (int)w2.slots.size(); ++k) {
        const auto& slot = w2.slots[k];
        const double atten = 1.0 / (1.0 + std::max(0.0, w2c.PROCESS_SUPPORT_TIME) * (double)k);
        const double life = std::max(0.0, w2c.ENVIRONNMENT_CORPSE_TIME * atten);
        std::cout << "  k=" << k
            << "  subdiv=" << slot.subdivision
            << "  offset=" << slot.offset
            << "  life=" << life << "\n";
    }
    return 0;
}
