#include <iostream>
#include <iomanip>
#include <random>
#include <cmath>

#include "RandomGenPolyShape.hpp"
#include "time2d_m2.h"
#include "time2d_i2.h"
#include "time2d_w2.h"
#include "time2d_macros.h"
#include "time2d_interface.h"  // UI = t2d::iface::{Inputs,W2Inputs}+sanitize

// --- utils ---
static uint64_t make_seed() {
    std::random_device rd;
    uint64_t a = ((uint64_t)rd() << 32) ^ (uint64_t)rd();
    return a ? a : 0xA5A5A5A5ULL;
}
static t2d::I2Plan simulate_with_factor(const t2d::M2Plan& m2, const t2d::I2Params& base, double f) {
    t2d::I2Params p = base;
    p.life_mean = std::max(1e-9, base.life_mean * f);
    return t2d::generate_i2(m2, p);
}

int main() {
    using namespace t2d;
    using namespace t2dgen;

    std::cout << std::fixed << std::setprecision(6);

    // =========================
    // PHASE 1 : Génération forme
    // =========================
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

    std::cout << "=== PHASE 1 : INIT (forme) ===\n";
    std::cout << "seed(shape)         = 0x" << std::hex << genP.seed << std::dec << "\n";
    std::cout << "Ordre d'iteration r = " << r << "\n";
    std::cout << "Nombre sommets N    = " << N << "\n";
    std::cout << "Segments E          = " << shape.E.size() << "\n\n";

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

    std::cout << "=== PHASE 2 : FOUDRE ===\n";
    std::cout << "seed(thunder)       = 0x" << std::hex << m2.seed << std::dec << "\n";
    std::cout << "Replicas k          = " << plan_m2.replicas_effective << "\n";
    std::cout << "Min gap (reel)      = " << plan_m2.thunder_min_gap << "\n";
    std::cout << "Tau (reel)          = " << plan_m2.thunder_tau << "\n\n";

    const double T = (plan_m2.replicas_effective > 0) ? double(N) / double(plan_m2.replicas_effective) : INFINITY;
    const double inv = (std::isfinite(T) && T > 0.0) ? 1.0 / T : 0.0;
    const long long FT = std::isfinite(T) ? (long long)std::floor(T) : 0;
    std::cout << "T = N/k              = " << T << "\n";
    std::cout << "Inverse(T)           = " << inv << "\n";
    std::cout << "Partie entiere de T  = " << FT << "\n\n";

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

    std::cout << "=== PHASE 3 : I2 (Terre+Glace) ===\n";
    std::cout << "Grains total (N)     = " << plan_i2.grains_total << "\n";
    std::cout << "Passage dimension    = " << plan_i2.passage_dimension << "\n";
    std::cout << "Throughput (gr/s)    = " << plan_i2.throughput << "\n";
    std::cout << "Service time         = " << plan_i2.service_time << "\n";
    std::cout << "Memorized            = " << plan_i2.grains_memorized << "\n";
    std::cout << "Lost (oubli)         = " << plan_i2.grains_lost << "\n";
    std::cout << "Rate memorized       = " << plan_i2.rate_memorized << "\n";
    std::cout << "Mean finish time     = " << plan_i2.mean_finish_time << "\n";

    std::cout << "\nEchantillon de grains:\n";
    for (const auto& g : plan_i2.samples) {
        std::cout << "  id=" << g.id
            << " life=" << g.life
            << " wait=" << g.wait_time
            << " pass=" << g.pass_time
            << " finish=" << g.finish_time
            << " -> " << (g.memorized ? "memo" : "oubli") << "\n";
    }

    // ==========================
    // PHASE 4 : UI (saisie)
    // ==========================
    t2d::iface::Inputs ui;
    std::cout << "\n--- Parametres interface ---\n";
    std::cout << "TIME_RETENTION_FACTOR (defaut=4) : ";
    if (!(std::cin >> ui.TIME_RETENTION_FACTOR)) return 0;
    std::cout << "CONTAINER_TIME_READ (defaut=10.0) : ";
    if (!(std::cin >> ui.CONTAINER_TIME_READ)) return 0;
    ui = t2d::iface::sanitize(ui);

    // W2 (structure & macros)
    t2d::iface::W2Inputs uiw2;
    std::cout << "\n--- Parametres W2 (VENT/BOIS) ---\n";
    std::cout << "subdivision_level (>=1, defaut=3) : ";
    if (!(std::cin >> uiw2.subdivision_level)) return 0;
    std::cout << "offset_step (defaut=0.15) : ";
    if (!(std::cin >> uiw2.offset_step)) return 0;
    std::cout << "PROCESS_EXISTENCE_TIME (nb de rebonds cibles, defaut=0) : ";
    if (!(std::cin >> uiw2.PROCESS_EXISTENCE_TIME)) return 0;
    std::cout << "PROCESS_SUPPORT_TIME (taux d'effacement, defaut=1.0) : ";
    if (!(std::cin >> uiw2.PROCESS_SUPPORT_TIME)) return 0;
    std::cout << "ENVIRONNMENT_CORPSE_TIME (maintien/rotation >=0, defaut=0.0) : ";
    if (!(std::cin >> uiw2.ENVIRONNMENT_CORPSE_TIME)) return 0;
    std::cout << "ENVIRONNMENT_RECOVER_TIME (tag libre, defaut=0.0) : ";
    if (!(std::cin >> uiw2.ENVIRONNMENT_RECOVER_TIME)) return 0;
    uiw2 = t2d::iface::sanitize(uiw2);

    // ==========================
    // CIBLES “glace” (interface)
    // ==========================
    const int lost_now = plan_i2.grains_lost;
    const int Ntot = plan_i2.grains_total;
    const int target_lost_exact = std::max(0, lost_now / ui.TIME_RETENTION_FACTOR);
    const int lost_remainder = lost_now - target_lost_exact * ui.TIME_RETENTION_FACTOR;
    const int target_mem_min = std::max(0, Ntot - target_lost_exact);

    t2d::LatencyTargets targets;
    targets.target_mem_min = target_mem_min;
    targets.target_lost_exact = target_lost_exact;
    targets.f_lo = 0.10; targets.f_hi = 10.0; targets.max_iter = 40;

    t2d::MacroParams mparams; // edge_share=0.20

    // ==========================
    // PHASE 5 : W2 structure + macros
    // ==========================
    t2d::W2Params w2p;
    w2p.subdivision_level = uiw2.subdivision_level;
    w2p.offset_step = uiw2.offset_step;
    t2d::W2Plan w2 = t2d::generate_w2_structure(plan_i2, w2p);

    t2d::W2MacroControls w2c;
    w2c.PROCESS_EXISTENCE_TIME = uiw2.PROCESS_EXISTENCE_TIME;
    w2c.PROCESS_SUPPORT_TIME = uiw2.PROCESS_SUPPORT_TIME;
    w2c.ENVIRONNMENT_CORPSE_TIME = uiw2.ENVIRONNMENT_CORPSE_TIME;
    w2c.ENVIRONNMENT_RECOVER_TIME = uiw2.ENVIRONNMENT_RECOVER_TIME;

    // Macros avec surcharge W2
    t2d::Macros MX = t2d::compute_macros(plan_i2, ip, plan_m2, w2, w2c, targets, mparams);

    // Projection (contrôle) avec facteur "high"
    t2d::I2Plan proj = simulate_with_factor(plan_m2, ip, MX.MEMORY_LATENCY_TIME_FACTOR_high);

    // ====== UNITÉS ======
    constexpr double TICK_SEC = 0.01;                       // 1 tick = 10 ms
    const double service_time_s = proj.service_time * TICK_SEC;

    // ====== CIBLE DE REBONDS AVEC TOLÉRANCE 8% ======
    int R; // nombre de rebonds souhaité
    std::cout << "\nObjectif: nombre de rebonds R (ex. 5) : ";
    if (!(std::cin >> R)) return 0;
    R = std::max(0, R);

    // --- STRUCTURE W2 : capacité >= R
    uiw2.subdivision_level = std::max(1, R + 1);
    uiw2.PROCESS_EXISTENCE_TIME = (double)R;   // cible
    uiw2.PROCESS_SUPPORT_TIME = (double)R;   // actives = R (borné par cible)

    // --- offset_step : pure géométrie (pas d'effet lisibilité) -> conserver la saisie précédente
    // uiw2.offset_step = uiw2.offset_step;

    // --- BOIS : échelle lisible (facultatif), en secondes
    uiw2.ENVIRONNMENT_CORPSE_TIME = std::max(0.0, 2.0 * service_time_s);

    // --- RELECTURE (fenêtre temps) : READ pour viser R avec +8%
    const double edge_share = mparams.edge_share;         // 0.20 par défaut
    const double center_share = 1.0 - edge_share;           // 0.80
    const double eps = 0.08;                       // 8%
    const double READ_needed = (R <= 0)
        ? 0.0
        : ((double)R * proj.service_time / center_share) * (1.0 + eps);

    ui.CONTAINER_TIME_READ = std::max(0.0, READ_needed);    // en ticks (coeur I2/M2)


    //const double center_share = 1.0 - mparams.edge_share;
    const int readable_capacity = (int)std::floor(ui.CONTAINER_TIME_READ / std::max(1e-12, proj.service_time));
    const int readable_center = (int)std::floor(readable_capacity * center_share);
    const int readable_effective = std::min(proj.grains_memorized, readable_center);

    // --- Sorties glace
    std::cout << "\n=== CIBLES (glace) ===\n";
    std::cout << "lost_now=" << lost_now
        << " -> target_lost=floor(lost/RET)=" << target_lost_exact
        << " (RET=" << ui.TIME_RETENTION_FACTOR << ", reste=" << lost_remainder << ")\n";
    std::cout << "target_mem_min = " << target_mem_min << " / N=" << Ntot << "\n";

    std::cout << "\n=== MACROS dynamiques ===\n";
    std::cout << "MEMORY_SPREAD_TIME_CONSTRAINT (%) = " << MX.MEMORY_SPREAD_TIME_CONSTRAINT_pct << "\n";
    std::cout << "MEMORY_LATENCY_TIME_FACTOR (low)  = " << MX.MEMORY_LATENCY_TIME_FACTOR_low << "\n";
    std::cout << "MEMORY_LATENCY_TIME_FACTOR (high) = " << MX.MEMORY_LATENCY_TIME_FACTOR_high << "\n";
    std::cout << "CONTAINER_RANGE_TIME (capacity)   = " << MX.CONTAINER_RANGE_TIME << "\n";
    std::cout << "CONTAINER_FLOW_TIME (collective)  = " << MX.CONTAINER_FLOW_TIME << "\n";

    std::cout << "\n=== PROJECTION (facteur = high) ===\n";
    std::cout << "proj mem=" << proj.grains_memorized
        << "  proj lost=" << proj.grains_lost
        << "  service_time=" << proj.service_time << "\n";
    std::cout << "CONTAINER_TIME_READ=" << ui.CONTAINER_TIME_READ
        << " -> readable_capacity=" << readable_capacity
        << " center_share=" << center_share
        << " readable_effective=" << readable_effective << "\n";

    // --- W2 récap ---
    std::cout << "\n=== W2 (VENT/BOIS) ===\n";
    std::cout << "subdiv_level=" << w2.subdivision_level
        << "  offset_step=" << w2.offset_step
        << "  rebounds_capacity=" << w2.rebounds_capacity << "\n";
    std::cout << "PROCESS_EXISTENCE_TIME=" << uiw2.PROCESS_EXISTENCE_TIME
        << "  PROCESS_SUPPORT_TIME=" << uiw2.PROCESS_SUPPORT_TIME << "\n";
    std::cout << "ENVIRONNMENT_CORPSE_TIME=" << uiw2.ENVIRONNMENT_CORPSE_TIME
        << "  ENVIRONNMENT_RECOVER_TIME=" << uiw2.ENVIRONNMENT_RECOVER_TIME << "\n";

    std::cout << "W2: target=" << MX.W2_REBOUNDS_TARGET
        << "  active=" << MX.W2_ACTIVE
        << "  disappeared=" << MX.W2_DISAPPEARED << "\n";

    // --- Empreintes actives (index, subdivisions, offset, life) ---
    std::cout << "\nEmpreintes actives (index, subdiv, offset, life):\n";
    for (int k = 1; k <= MX.W2_ACTIVE && k < (int)w2.slots.size(); ++k) {
        const auto& slot = w2.slots[k];              // k : 1..active
        const double atten = 1.0 / (1.0 + std::max(0.0, w2c.PROCESS_SUPPORT_TIME) * (double)k);
        const double life = std::max(0.0, w2c.ENVIRONNMENT_CORPSE_TIME * atten);
        std::cout << "  index=" << slot.index
            << "  subdiv=" << slot.subdivision
            << "  offset=" << slot.offset
            << "  life=" << life << "\n";
    }

    return 0;
}
