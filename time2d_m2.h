#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace t2d {

    // ---------- RNG minimal, déterministe ----------
    struct LCG {
        uint64_t s; explicit LCG(uint64_t seed = 0x9e3779b97f4a7c15ULL) : s(seed) {}
        uint32_t next() { s = 2862933555777941757ULL * s + 3037000493ULL; return (uint32_t)(s >> 32); }
        double uniform() { return (next() + 0.5) / 4294967296.0; } // [0,1)
    };

    // ---------- Géométrie : forme initiale ----------
    struct Vec2 { double x{}, y{}; };
    struct Segment { int a{ -1 }, b{ -1 }; };
    struct Shape {
        std::vector<Vec2>    V;          // sommets
        std::vector<Segment> E;          // segments
        std::vector<int>     draw_order; // ordre unique (indices dans E)
    };

    // ---------- Evénements (ticks réels) ----------
    enum class Op {
        InitTrace,        // phase INIT: tracer segment de référence
        ThunderBreak,     // phase FOUDRE: brisure/impulsion au sommet
        ThunderReplica,   // phase FOUDRE: réplique quasi-instantanée
        MagmatHeal,       // phase MAGMAT: retrace segment (ticks négatifs)
        PhaseMark
    };

    struct EventTick {
        double tick{};     // **réel** (peut être négatif en phase MAGMAT)
        Op  op{};
        int vertex{ -1 };    // pour Thunder*
        int edge{ -1 };      // pour InitTrace/MagmatHeal
        int cluster{ -1 };   // id de cluster FOUDRE (si pertinent)
    };

    // ---------- Paramétrage M2 (sans granularité) ----------
    struct M2Params {
        // INIT
        int    init_span{ 32 };            // “longueur” de la fenêtre INIT (unités abstraites)

        // FOUDRE (k répliques sur N sommets)
        int    replicas_k{ 8 };            // k < N (corrigé si besoin)
        int    thunder_span{ 24 };         // largeur de la fenêtre FOUDRE (>0)
        double thunder_jitter{ 0.75 };     // jitter **réel** (±j)
        double replica_rate{ 0.5 };        // probabilité d’une réplique (même tick ou +ε)

        // MAGMAT (reconstitution en ticks négatifs)
        int    magmat_span{ 128 };         // |ticks| de la phase MAGMAT
        bool   heal_by_draw_order{ true }; // retrace selon draw_order inverse

        // Clustering (regroupement statistique des instants FOUDRE)
        double cluster_percentile{ 0.25 }; // τ = percentile des gaps (réels)
        uint64_t seed{ 0xC0FFEEULL };
    };

    // ---------- Résultat ----------
    struct M2Plan {
        std::vector<EventTick> events; // triés par tick croissant
        double tick_init_end{};        // dernier tick INIT (>=0)
        double tick_thunder_end{};     // dernier tick FOUDRE (>= tick_init_end)
        double tick_magmat_start{};    // premier tick MAGMAT (<= -epsilon)

        // métriques exposées (réelles)
        double thunder_min_gap{ 0.0 };   // plus petit écart mesuré entre deux instants FOUDRE
        double thunder_tau{ 1.0 };       // seuil de regroupement (τ) calculé
        int    replicas_effective{ 0 };  // k effectif utilisé (toujours < N)
    };

    // ---------- utilitaires internes ----------
    inline int    clampi(int v, int lo, int hi) { return std::min(std::max(v, lo), hi); }
    inline double clampd(double v, double lo, double hi) { return std::min(std::max(v, lo), hi); }

    inline int percentile_index(int n, double p) { // p in [0..1]
        if (n <= 0) return 0;
        int i = (int)std::floor(p * (n - 1));
        return clampi(i, 0, n - 1);
    }

    // ---------- génération principale ----------
    inline M2Plan generate_m2(const Shape& S, const M2Params& P) {
        M2Plan out;
        out.events.reserve(P.init_span + P.thunder_span + P.magmat_span + 64);

        const int N = (int)S.V.size();
        const int E = (int)S.E.size();
        if (N == 0 || E == 0) return out;

        // -------- PHASE 1 : INIT (ticks réels >= 0) --------
        std::vector<int> order = S.draw_order.empty()
            ? [&]() { std::vector<int> v(E); std::iota(v.begin(), v.end(), 0); return v; }()
            : S.draw_order;

        const int stepsI = (int)order.size();
        for (int i = 0; i < stepsI; ++i) {
            // réparti sur [0, init_span) en double
            double t = (P.init_span > 0)
                ? ((double)i / std::max(1, stepsI)) * (double)P.init_span
                : 0.0;
            out.events.push_back({ t, Op::InitTrace, -1, order[i], -1 });
            out.tick_init_end = std::max(out.tick_init_end, t);
        }
        out.events.push_back({ 0.0, Op::PhaseMark, -1, -1, -1 }); // INIT start

        // -------- PHASE 2 : FOUDRE (ticks réels > tick_init_end) --------
        const double thunder_start = out.tick_init_end + 1.0;
        const double thunder_end = thunder_start + std::max(1, P.thunder_span);
        out.events.push_back({ thunder_start, Op::PhaseMark, -1, -1, -1 }); // THUNDER start

        LCG rng{ P.seed };
        const int K = std::min(std::max(0, P.replicas_k), std::max(0, N - 1)); // k < N
        out.replicas_effective = K;

        // tirage sans remise de K sommets distincts
        std::vector<int> chosen_vertices; chosen_vertices.reserve(K);
        std::vector<int> pool(N); std::iota(pool.begin(), pool.end(), 0);
        for (int i = 0; i < K && !pool.empty(); ++i) {
            int j = (int)std::floor(rng.uniform() * (double)pool.size());
            chosen_vertices.push_back(pool[j]);
            pool.erase(pool.begin() + j);
        }

        // instants réels avec jitter réel
        std::vector<double> thunder_times; thunder_times.reserve(K);
        for (int i = 0; i < K; ++i) {
            double base = thunder_start + rng.uniform() * std::max(1, P.thunder_span);
            double jitter = (rng.uniform() * 2.0 - 1.0) * P.thunder_jitter;
            double tt = clampd(base + jitter, thunder_start, thunder_end - std::numeric_limits<double>::epsilon());
            thunder_times.push_back(tt);
        }

        // τ : percentile des gaps réels
        std::sort(thunder_times.begin(), thunder_times.end());
        std::vector<double> gaps;
        for (size_t i = 1; i < thunder_times.size(); ++i) gaps.push_back(thunder_times[i] - thunder_times[i - 1]);
        double tau = 1.0;
        if (!gaps.empty()) {
            std::sort(gaps.begin(), gaps.end());
            tau = std::max(1.0, gaps[percentile_index((int)gaps.size(), P.cluster_percentile)]);
            out.thunder_min_gap = gaps.front();
        }
        else {
            out.thunder_min_gap = 0.0;
        }
        out.thunder_tau = tau;

        // clustering par gap <= τ
        int cluster_id = 0;
        for (int i = 0; i < K; ++i) {
            if (i > 0 && (thunder_times[i] - thunder_times[i - 1]) > tau) ++cluster_id;
            double t = thunder_times[i];
            int    v = chosen_vertices[i];
            out.events.push_back({ t, Op::ThunderBreak, v, -1, cluster_id });

            // éventuelle réplique quasi-instantanée (même t ou +ε)
            if (rng.uniform() < P.replica_rate) {
                double eps = (rng.uniform() < 0.5 ? 0.0 : std::max(1e-6, 0.01 * tau)); // petit décalage
                double t2 = clampd(t + eps, thunder_start, thunder_end - std::numeric_limits<double>::epsilon());
                out.events.push_back({ t2, Op::ThunderReplica, v, -1, cluster_id });
            }
        }
        out.tick_thunder_end = thunder_end;

        // -------- PHASE 3 : MAGMAT (ticks réels < 0) --------
        std::reverse(order.begin(), order.end());
        out.tick_magmat_start = -(double)P.magmat_span;   // ex. [-span .. -ε]
        out.events.push_back({ out.tick_magmat_start, Op::PhaseMark, -1, -1, -1 }); // MAGMAT start

        const int stepsM = (int)order.size();
        for (int k = 0; k < stepsM; ++k) {
            // répartir uniformément sur [-span .. 0[
            double tn = out.tick_magmat_start + ((double)(k + 1) / (double)stepsM) * (double)P.magmat_span;
            tn = std::min(tn, -std::numeric_limits<double>::epsilon());
            out.events.push_back({ tn, Op::MagmatHeal, -1, order[k], -1 });
        }

        // -------- tri global (tick réel) --------
        std::sort(out.events.begin(), out.events.end(),
            [](const EventTick& a, const EventTick& b) {
                if (a.tick != b.tick) return a.tick < b.tick;
                auto rank = [](Op op) {
                    switch (op) {
                    case Op::PhaseMark:      return 0;
                    case Op::InitTrace:      return 1;
                    case Op::ThunderBreak:   return 2;
                    case Op::ThunderReplica: return 3;
                    case Op::MagmatHeal:     return 4;
                    } return 5;
                    };
                return rank(a.op) < rank(b.op);
            });

        return out;
    }

} // namespace t2d
