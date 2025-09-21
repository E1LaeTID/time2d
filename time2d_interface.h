#pragma once
/*
  time2d — Interface publique (niveau UI)
  ---------------------------------------
  Contrat minimal pour piloter le coeur m2/i2 via deux paramètres exposés :
  - TIME_RETENTION_FACTOR : diviseur des pertes (glace)
  - CONTAINER_TIME_READ   : budget de lecture du conteneur (temps fini)

  + Paramètres d'interface W2 (VENT/BOIS) exposés à l'utilisateur :
    - PROCESS_SUPPORT_TIME        (VENT)      : taux/rythme de rafraîchissement (≥0)
    - ENVIRONNMENT_CORPSE_TIME    (BOIS)      : “coût de rotation / déplacement” (≥0)

  ⚠️ Aucune exposition des headers internes w2/f2 ici.
*/

#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

namespace t2d {
    namespace iface {

        struct Version {
            static constexpr int   major = 1;
            static constexpr int   minor = 1;
            static constexpr int   patch = 0;
            static constexpr const char* tag = "time2d-iface/v1.1";
        };

        /* -----------------------------
           PARAMÈTRES UI (exposés)
           ----------------------------- */
        struct Inputs {
            int    TIME_RETENTION_FACTOR{ 4 };   // conseillé [1..32] en dev
            double CONTAINER_TIME_READ{ 10.0 };  // conseillé [0.1..300] en dev
        };

        /* -----------------------------
           LIMITES & SANITIZATION (m2/i2)
           ----------------------------- */
        struct Limits {
            int    min_retention{ 1 };
            int    max_retention{ 32 };
            double min_read{ 0.1 };
            double max_read{ 300.0 };

            // Doc (r max = 4)
            static constexpr int MAX_ITERATION = 4;
            static constexpr int MAX_THEORETICAL_VERTICES_R4 = 37448; // 8 + 8*(8+8^2+8^3+8^4)
            static constexpr int RECOMMENDED_N_CAP_DEV = 12000;       // cap pratique conseillé
        };

        inline Inputs sanitize(const Inputs& in, const Limits& lim = {}) {
            Inputs out = in;
            if (out.TIME_RETENTION_FACTOR < lim.min_retention) out.TIME_RETENTION_FACTOR = lim.min_retention;
            if (out.TIME_RETENTION_FACTOR > lim.max_retention) out.TIME_RETENTION_FACTOR = lim.max_retention;
            if (!(out.CONTAINER_TIME_READ > 0.0)) out.CONTAINER_TIME_READ = lim.min_read;
            if (out.CONTAINER_TIME_READ < lim.min_read) out.CONTAINER_TIME_READ = lim.min_read;
            if (out.CONTAINER_TIME_READ > lim.max_read) out.CONTAINER_TIME_READ = lim.max_read;
            return out;
        }

        /* -----------------------------
           PARAMÈTRES UI W2 (VENT/BOIS)
           ----------------------------- */
        struct W2Inputs {
            // VENT (structure visible côté UI)
            int    subdivision_level{ 3 };   // >=1 ; capacité = level-1
            double offset_step{ 0.15 };      // peut être négatif, nul ou positif

            // VENT (macros)
            double PROCESS_SUPPORT_TIME{ 1.0 };      // >=0 (taux de rafraîchissement)
            double PROCESS_EXISTENCE_TIME{ 0.0 };    // >=0 (nb de rebonds ciblés)

            // BOIS (macros)
            double ENVIRONNMENT_CORPSE_TIME{ 0.0 };  // >=0 (coût / rotation)
            double ENVIRONNMENT_RECOVER_TIME{ 0.0 }; // tag documentaire (id logique)
        };

        struct W2Limits {
            // structure
            int    min_subdiv{ 1 };
            int    max_subdiv{ 64 };
            double min_offset{ -1000.0 };
            double max_offset{ 1000.0 };

            // macros vent/bois
            double min_support{ 0.0 };
            double max_support{ 100.0 };
            double min_existence{ 0.0 };
            double max_existence{ 1000.0 };
            double min_corpse{ 0.0 };
            double max_corpse{ 1e6 };
            double min_recover{ -1e12 };    // on autorise un tag libre
            double max_recover{ 1e12 };
        };

        inline W2Inputs sanitize(const W2Inputs& in, const W2Limits& lim = {}) {
            W2Inputs o = in;

            // structure
            if (o.subdivision_level < lim.min_subdiv) o.subdivision_level = lim.min_subdiv;
            if (o.subdivision_level > lim.max_subdiv) o.subdivision_level = lim.max_subdiv;
            if (o.offset_step < lim.min_offset) o.offset_step = lim.min_offset;
            if (o.offset_step > lim.max_offset) o.offset_step = lim.max_offset;

            // macros vent
            if (!(o.PROCESS_SUPPORT_TIME >= 0.0)) o.PROCESS_SUPPORT_TIME = lim.min_support;
            o.PROCESS_SUPPORT_TIME = std::clamp(o.PROCESS_SUPPORT_TIME, lim.min_support, lim.max_support);

            if (!(o.PROCESS_EXISTENCE_TIME >= 0.0)) o.PROCESS_EXISTENCE_TIME = lim.min_existence;
            o.PROCESS_EXISTENCE_TIME = std::clamp(o.PROCESS_EXISTENCE_TIME, lim.min_existence, lim.max_existence);

            // macros bois
            if (!(o.ENVIRONNMENT_CORPSE_TIME >= 0.0)) o.ENVIRONNMENT_CORPSE_TIME = lim.min_corpse;
            o.ENVIRONNMENT_CORPSE_TIME = std::clamp(o.ENVIRONNMENT_CORPSE_TIME, lim.min_corpse, lim.max_corpse);

            o.ENVIRONNMENT_RECOVER_TIME = std::clamp(o.ENVIRONNMENT_RECOVER_TIME, lim.min_recover, lim.max_recover);

            return o;
        }

        /* -----------------------------
           SORTIES (pour UI/console)
           ----------------------------- */
        struct Counters {
            int N{ 0 };
            int memorized{ 0 };
            int lost{ 0 };
        };

        struct Targets {
            int retention_factor{ 4 };
            int target_lost{ 0 };        // floor(lost / factor)
            int target_mem_min{ 0 };     // N - target_lost
            int lost_remainder{ 0 };     // lost - target_lost*factor
        };

        // ⚠️ Renommage : Macros -> macros_interface
        struct macros_interface {
            double MEMORY_SPREAD_TIME_CONSTRAINT_pct{ 0.0 };
            double MEMORY_LATENCY_TIME_FACTOR_low{ 1.0 };
            double MEMORY_LATENCY_TIME_FACTOR_high{ 1.0 };
            int    CONTAINER_RANGE_TIME{ 0 };
            double CONTAINER_FLOW_TIME{ 0.0 };
        };

        // Écho facultatif des entrées W2 côté sortie (utile pour logs / JSON)
        struct W2Echo {
            double process_support_time{ 0.0 };
            double environment_corpse_time{ 0.0 };
        };

        struct TimeBudget {
            std::string unit{ "tick" };  // préciser l’unité côté service
            double total{ 0.0 };         // temps total “théorique”
            double used{ 0.0 };          // temps réellement employé
            double gap() const { return (total > used) ? (total - used) : 0.0; }
            double used_pct() const {
                const double d = (total > 0.0) ? total : 1.0;
                return (used / d) * 100.0;
            }
        };

        struct Projection {
            int    projected_memorized{ 0 };
            int    projected_lost{ 0 };
            double projected_service_time{ 0.0 };
            int    readable_effective{ 0 }; // combien “lisibles” dans CONTAINER_TIME_READ (partie centrale)
        };

        struct Outputs {
            std::string       version{ Version::tag };
            Counters          counters{};
            Targets           targets{};
            macros_interface  macros{};            // <-- renommé ici
            double            container_time_read{ 0.0 };
            W2Echo            w2{};
            TimeBudget        time_budget{};
            Projection        projection_high{};
            std::vector<std::string> notes;
        };

    } // namespace iface
} // namespace t2d
