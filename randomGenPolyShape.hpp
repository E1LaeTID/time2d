#pragma once
#include <cstdint>
#include <vector>
#include <optional>

// Forward-declare uniquement : évite de dépendre du contenu de time2d_m2.h côté .hpp
namespace t2d { struct Shape; }

namespace t2dgen {

    class RandomGenPolyShape {
    public:
        struct Params {
            double              base_size{ 1.0 };          // "diamètre" approx de l'octogone racine
            double              child_scale{ 0.25 };       // 1/4 de la taille du parent
            int                 min_sides{ 3 };            // bornes des sous-polygones
            int                 max_sides{ 8 };
            std::optional<int>  fixed_iterations;        // sinon tirage aléatoire [1..4]
            std::uint64_t       seed{ 0xC0FFEEULL };       // reproductibilité
        };

        explicit RandomGenPolyShape(Params p = {});
        ~RandomGenPolyShape();

        // Génère la forme hiérarchique et la convertit en t2d::Shape (définie dans time2d_m2.h)
        t2d::Shape generate();

        // Métriques
        int totalVertices() const;                        // N effectif de la dernière génération
        int iterations()   const;                         // r tiré/effectif (1..4)
        static long long theoreticalMaxVertices(int iterations_1_to_4);

    private:
        // Implémentation cachée (PImpl)
        struct Impl;
        Impl* d_;
    };

} // namespace t2dgen
