#pragma once
#include <cmath>
#include <algorithm>
#include <utility>
#include "time2d_i2.h"   // I2Plan / I2Params / generate_i2
#include "time2d_m2.h"   // M2Plan (déjà utilisé)
#include "time2d_w2.h"   // W2Plan (STRUCTURE)

namespace t2d {

    // -------------------------
    // Paramétrage d'affichage
    // -------------------------
    struct MacroParams {
        double edge_share{ 0.20 };          // part des bords (20%)
    };

    // Cibles pour la recherche des facteurs multiplicateurs
    struct LatencyTargets {
        int    target_mem_min{ 40 };        // au moins 40 “memorized”
        int    target_lost_exact{ -1 };     // ex. 269 (si négatif -> ignoré)
        double f_lo{ 0.10 };                // borne basse initiale de recherche
        double f_hi{ 10.0 };                // borne haute initiale
        int    max_iter{ 40 };              // itérations max (dichotomie + élargissement)
    };

    // -------------------------
    // W2 — Variables MACRO (ton vocabulaire exact)
    // -------------------------
    /*
      PROCESS_EXISTENCE_TIME        (VENT)
        -> Correspond au NOMBRE de rebonds "possibles" en fonction de la subdivision.
           Utilisé ici UNIQUEMENT pour décider d’un objectif de rebonds (entier).
      PROCESS_SUPPORT_TIME          (VENT)
        -> Contrôle l’état des empreintes (analogie : le vent efface les traces) ;
           décide combien d’empreintes demeurent ACTIVES (entier).
      ENVIRONNMENT_RECOVER_TIME     (BOIS)
        -> Sert d’étiquette/tag (container_id logique) pour distinguer le conteneur
           de ses empreintes. Valeur documentaire (double).
      ENVIRONNMENT_CORPSE_TIME      (BOIS)
        -> Temps nécessaire pour déplacer un container (taille & subdivisions de
           référence). S’il est >0, on peut maintenir une empreinte “présente”.
    */
    struct W2MacroControls {
        double PROCESS_EXISTENCE_TIME{ 0.0 };
        double PROCESS_SUPPORT_TIME{ 0.0 };
        double ENVIRONNMENT_RECOVER_TIME{ 0.0 };
        double ENVIRONNMENT_CORPSE_TIME{ 0.0 };
    };

    // -------------------------
    // Valeurs “macro” calculées dynamiquement
    // -------------------------
    struct Macros {
        // 1) Pourcentage de mémorisation (toujours positif) : 100 * mem / (mem+lost)
        double MEMORY_SPREAD_TIME_CONSTRAINT_pct{ 0.0 };

        // 2) Facteurs multiplicateurs min/max à appliquer au temps de vie (life_mean)
        //    pour atteindre les cibles demandées.
        double MEMORY_LATENCY_TIME_FACTOR_low{ 1.0 };   // pour “≥ target_mem_min”
        double MEMORY_LATENCY_TIME_FACTOR_high{ 1.0 };  // pour “lost == target_lost_exact” (ou proche)

        // 3) Capacité du conteneur (en grains)
        int    CONTAINER_RANGE_TIME{ 0 };

        // 4) Durée de vie collective pondérée centre/bords
        double CONTAINER_FLOW_TIME{ 0.0 };

        // ---------- AJOUTS W2 (agrégats) ----------
        int    W2_SUBDIVISION_LEVEL{ 1 };
        double W2_OFFSET_STEP{ 0.0 };
        int    W2_REBOUNDS_CAPACITY{ 0 }; // = max(0, subdivision_level-1)
        int    W2_REBOUNDS_TARGET{ 0 };   // décidé par PROCESS_EXISTENCE_TIME
        int    W2_ACTIVE{ 0 };            // décidé par PROCESS_SUPPORT_TIME (≤ target)
        int    W2_DISAPPEARED{ 0 };       // = target - active (≥0)
        double W2_ENVIRONNMENT_RECOVER_TIME_TAG{ 0.0 }; // documentaire
        double W2_ENVIRONNMENT_CORPSE_TIME{ 0.0 };      // documentaire
    };

    // ----- util interne : re-simuler I2 avec un facteur f sur life_mean -----
    inline I2Plan _simulate_with_factor(const M2Plan& m2, const I2Params& base, double f) {
        I2Params p = base;
        p.life_mean = std::max(1e-9, base.life_mean * f);
        // même seed => même tirages de jitter, seule l'échelle change (monotone)
        return generate_i2(m2, p);
    }

    // recherche du plus petit f tel que memorized >= goal
    inline double _find_min_factor_for_mem(const M2Plan& m2, const I2Params& base,
        int goal, double flo, double fhi, int max_iter) {
        // élargir le bracket si nécessaire
        I2Plan Plo = _simulate_with_factor(m2, base, flo);
        I2Plan Phi = _simulate_with_factor(m2, base, fhi);

        int safety = 0;
        while (Plo.grains_memorized >= goal && flo > 1e-6 && safety++ < 20) {
            fhi = flo; Phi = Plo;
            flo *= 0.5; if (flo < 1e-6) break;
            Plo = _simulate_with_factor(m2, base, flo);
        }
        safety = 0;
        while (Phi.grains_memorized < goal && fhi < 1e12 && safety++ < 20) {
            flo = fhi; Plo = Phi;
            fhi *= 2.0;
            Phi = _simulate_with_factor(m2, base, fhi);
        }
        // dichotomie
        for (int it = 0; it < max_iter; ++it) {
            double mid = 0.5 * (flo + fhi);
            I2Plan Pm = _simulate_with_factor(m2, base, mid);
            if (Pm.grains_memorized >= goal) { fhi = mid; Phi = Pm; }
            else { flo = mid; Plo = Pm; }
        }
        return fhi; // plus petit f atteignant la cible (approché)
    }

    // idem pour une cible “lost == L” -> mem >= N-L
    inline double _find_min_factor_for_lost(const M2Plan& m2, const I2Params& base,
        int lost_target, double flo, double fhi, int max_iter) {
        if (lost_target < 0) return 1.0;
        // cible en mémorisés :
        const int N = std::max(1, base.total_vertices_n > 0 ? base.total_vertices_n
            : (m2.replicas_effective > 0 ? m2.replicas_effective : 1));
        int mem_goal = std::max(0, N - lost_target);
        return _find_min_factor_for_mem(m2, base, mem_goal, flo, fhi, max_iter);
    }

    // -------------------------
    // Calcul principal des “macros” (VERSION ORIGINALE — inchangée)
    // -------------------------
    inline Macros compute_macros(const I2Plan& i2, const I2Params& ip,
        const M2Plan& m2, const LatencyTargets& tgt = {}, const MacroParams& mp = {}) {
        Macros out;

        const int total = std::max(1, i2.grains_memorized + i2.grains_lost);
        const double mem = (double)i2.grains_memorized;

        // (1) % de mémorisation (positif)
        out.MEMORY_SPREAD_TIME_CONSTRAINT_pct = 100.0 * mem / (double)total;

        // (2) FACTEURS multiplicateurs sur life_mean
        //     - low : facteur min pour atteindre “≥ target_mem_min”
        out.MEMORY_LATENCY_TIME_FACTOR_low =
            _find_min_factor_for_mem(m2, ip, tgt.target_mem_min, tgt.f_lo, tgt.f_hi, tgt.max_iter);

        //     - high : facteur min pour atteindre “lost == target_lost_exact”
        if (tgt.target_lost_exact >= 0) {
            out.MEMORY_LATENCY_TIME_FACTOR_high =
                _find_min_factor_for_lost(m2, ip, tgt.target_lost_exact, tgt.f_lo, tgt.f_hi, tgt.max_iter);
        }
        else {
            // pas de cible lost -> borne haute “x4 des memorized”
            const int goal4x = std::max(1, i2.grains_memorized * 4);
            out.MEMORY_LATENCY_TIME_FACTOR_high =
                _find_min_factor_for_mem(m2, ip, goal4x, tgt.f_lo, tgt.f_hi, tgt.max_iter);
        }

        // (3) capacité du conteneur : mémorisés projetés avec le facteur “low”
        {
            I2Plan proj = _simulate_with_factor(m2, ip, out.MEMORY_LATENCY_TIME_FACTOR_low);
            out.CONTAINER_RANGE_TIME = proj.grains_memorized;
        }

        // (4) durée de vie collective pondérée (centre/bords)
        {
            const double base_life = (i2.mean_finish_time > 0.0)
                ? 0.5 * (ip.life_mean + i2.mean_finish_time)
                : ip.life_mean;
            const double center_life = base_life * out.MEMORY_LATENCY_TIME_FACTOR_low;
            const double edge_life = center_life * 0.75; // -25% aux bords
            const double edge_share = std::clamp(mp.edge_share, 0.0, 1.0);
            out.CONTAINER_FLOW_TIME = center_life * (1.0 - edge_share) + edge_life * edge_share;
        }

        // Champs W2 laissés par défaut ici (0/1) — ils seront remplis par la surcharge ci-dessous.
        return out;
    }

    // -------------------------
    // Surcharge : compute_macros + W2 (VENT/BOIS via macros uniquement)
    // -------------------------
    inline Macros compute_macros(const I2Plan& i2, const I2Params& ip,
        const M2Plan& m2, const W2Plan& w2,
        const W2MacroControls& w2c,
        const LatencyTargets& tgt = {}, const MacroParams& mp = {}) {
        // 1) calcul “classique”
        Macros out = compute_macros(i2, ip, m2, tgt, mp);

        // 2) application des macros W2
        const int subdivisions = std::max(1, w2.subdivision_level);
        const int capacity = std::max(0, subdivisions - 1);

        // PROCESS_EXISTENCE_TIME (VENT) -> REBOUNDS_TARGET (entier, borné par capacity)
        int rebounds_target = (int)std::floor(std::max(0.0, w2c.PROCESS_EXISTENCE_TIME));
        rebounds_target = std::clamp(rebounds_target, 0, capacity);

        // PROCESS_SUPPORT_TIME (VENT) -> ACTIVE (entier, ≤ target)
        int active = (int)std::floor(std::max(0.0, w2c.PROCESS_SUPPORT_TIME));
        active = std::clamp(active, 0, rebounds_target);

        // ENVIRONNMENT_CORPSE_TIME (BOIS) > 0 => maintien minimal d’une empreinte
        if (w2c.ENVIRONNMENT_CORPSE_TIME > 0.0 && rebounds_target > 0 && active == 0)
            active = 1;

        // Remplissage des agrégats W2
        out.W2_SUBDIVISION_LEVEL = subdivisions;
        out.W2_OFFSET_STEP = w2.offset_step;
        out.W2_REBOUNDS_CAPACITY = capacity;
        out.W2_REBOUNDS_TARGET = rebounds_target;
        out.W2_ACTIVE = active;
        out.W2_DISAPPEARED = std::max(0, rebounds_target - active);
        out.W2_ENVIRONNMENT_RECOVER_TIME_TAG = w2c.ENVIRONNMENT_RECOVER_TIME;
        out.W2_ENVIRONNMENT_CORPSE_TIME = w2c.ENVIRONNMENT_CORPSE_TIME;

        return out;
    }

} // namespace t2d
