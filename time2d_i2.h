#pragma once
#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include "time2d_m2.h"   // M2Plan, clampi/clampd, LCG dispo dans namespace t2d

namespace t2d {

    // -----------------------------
    // i2 : Terre (granularité) + Glace (divisibilité / durée de vie)
    // -----------------------------

    // Paramètres pour la couche i2 (enfant logique de m2)
    struct I2Params {
        // HÉRITAGE LOGIQUE (obligatoire pour bien calibrer i2) :
        // - soit vous donnez N (nb total de sommets de la forme),
        // - soit vous donnez inverse_ratio = k/N ; à défaut on ne peut pas déduire N.
        int     total_vertices_n{ 0 };     // N (si connu). S'il est 0 et inverse_ratio>0, on prendra N = k / inverse_ratio.
        double  inverse_ratio{ 0.0 };      // k/N. S'il est >0 et N==0, on utilisera N = k / inverse_ratio.

        int     iterations_inherited{ 1 }; // r (ordre d’itération hérité de la génération de forme) ∈ [1..4]

        // TERRE (granularité)
        // Débit à travers l'ouverture (force uniforme) :
        //   throughput = force_rate * passage_dimension
        // où passage_dimension = grains_total * pow(0.25, r)
        double  force_rate{ 0.05 };        // grains par unité de passage (coefficient)

        // GLACE (durée de vie des grains, “glace fondante”)
        double  life_mean{ 10.0 };         // durée de vie moyenne d’un granule
        double  life_jitter{ 0.20 };       // ±20% de variation (U[1-j, 1+j])

        // Option : borne max d’événements retournés (échantillon)
        int     sample_max{ 64 };

        // RNG
        uint64_t seed{ 0x1BADB002ULL };
    };

    // Un granule i2 (résultat)
    struct I2GrainSample {
        int     id{};           // index du granule
        double  life{};         // durée de vie tirée (glace)
        double  wait_time{};    // temps d'attente avant de passer
        double  pass_time{};    // durée de passage (service)
        double  finish_time{};  // fin (attente + passage)
        bool    memorized{};    // true = comptabilisé/mémoire ; false = perdu/oubli
    };

    // Résultats agrégés pour i2
    struct I2Plan {
        // Rappel/héritage
        int     replicas_k{};         // k (depuis M2Plan)
        int     total_vertices_n{};   // N (dérivé ou fourni)
        int     iterations_inherited{}; // r
        double  inverse_ratio{};      // k/N

        // Terre
        int     grains_total{};       // quantité de granulés (héritée) : par défaut N
        double  passage_dimension{};  // grains_total * (1/4)^r
        double  throughput{};         // force_rate * passage_dimension (grains / unité de temps)
        double  service_time{};       // 1 / throughput (temps pour “servir” un grain)

        // Glace
        int     grains_memorized{};   // passés avant d’expirer
        int     grains_lost{};        // expirés avant de passer
        double  rate_memorized{};     // ratio mémorisé
        double  mean_finish_time{};   // moyenne des finish_time pour les mémorisés

        // Pour introspection / debug
        std::vector<I2GrainSample> samples; // échantillon de quelques grains
    };

    // util interne : tirage d’un facteur U[1-j, 1+j] borné >= 0
    inline double _jitter_factor(double j, t2d::LCG& rng) {
        j = std::clamp(j, 0.0, 0.99); // eviter négatif
        const double u = rng.uniform();          // [0,1)
        const double f = 1.0 + (2.0 * u - 1.0) * j;  // [1-j, 1+j]
        return std::max(0.0, f);
    }

    // Génération i2 à partir d’un plan M2 (déjà calculé) + paramètres i2.
    // Hypothèses :
    // - Tous les grains sont “dans le réservoir haut” au temps 0 et passent par UNE ouverture.
    // - Débit constant (force uniforme) ⇒ file FIFO avec temps de service constant (= 1/throughput).
    // - Chaque grain a une durée de vie tirée (glace). S’il n’atteint pas la fin du passage avant d’expirer ⇒ perdu (oubli).
    inline I2Plan generate_i2(const M2Plan& m2, const I2Params& P) {
        I2Plan out;
        out.replicas_k = m2.replicas_effective;
        out.iterations_inherited = std::max(1, P.iterations_inherited);

        // 1) Déterminer N et l’inverse k/N
        int N = P.total_vertices_n;
        double inv = P.inverse_ratio;
        if (N <= 0) {
            if (inv > 0.0) {
                N = (int)std::llround((double)out.replicas_k / inv); // ta formule : N = k / (k/N)
            }
            else {
                // fallback : si on n’a ni N ni inverse ⇒ on approx avec k (au pire).
                N = std::max(1, out.replicas_k);
            }
        }
        if (inv <= 0.0) {
            inv = (N > 0) ? (double)out.replicas_k / (double)N : 0.0; // k/N
        }
        out.total_vertices_n = N;
        out.inverse_ratio = inv;

        // 2) Quantité de granulés (héritée) : on prend N par défaut.
        out.grains_total = std::max(1, N);

        // 3) Dimension de passage : grains_total * (1/4)^r
        const double reduction = std::pow(0.25, (double)out.iterations_inherited);
        out.passage_dimension = (double)out.grains_total * reduction;

        // 4) Débit / temps de service
        out.throughput = std::max(1e-9, P.force_rate * out.passage_dimension);
        out.service_time = 1.0 / out.throughput;

        // 5) Simulation de l’écoulement + glace (durée de vie)
        t2d::LCG rng{ P.seed };
        int mem = 0, lost = 0;
        double sum_finish_mem = 0.0;

        // on échantillonne quelques grains seulement (pour debug/inspection)
        out.samples.clear(); out.samples.reserve(std::min(out.grains_total, P.sample_max));

        for (int i = 0; i < out.grains_total; ++i) {
            // File FIFO M/M/1 déterministe (service constant) : chaque grain attend (i)*service_time
            const double wait = (double)i * out.service_time;
            const double finish = wait + out.service_time;

            // Glace : durée de vie tirée autour de life_mean
            const double life = P.life_mean * _jitter_factor(P.life_jitter, rng);

            const bool ok = (life >= finish); // opérabilité : converge vers une même valeur finale (ici, franchit l’ouverture)
            if (ok) { ++mem; sum_finish_mem += finish; }
            else { ++lost; }

            if ((int)out.samples.size() < P.sample_max) {
                out.samples.push_back(I2GrainSample{
                  .id = i,
                  .life = life,
                  .wait_time = wait,
                  .pass_time = out.service_time,
                  .finish_time = finish,
                  .memorized = ok
                    });
            }
        }

        out.grains_memorized = mem;
        out.grains_lost = lost;
        out.rate_memorized = (double)mem / (double)out.grains_total;
        out.mean_finish_time = (mem > 0) ? (sum_finish_mem / (double)mem) : 0.0;

        return out;
    }

} // namespace t2d
