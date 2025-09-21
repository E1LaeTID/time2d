#include "RandomGenPolyShape.hpp"
#include "time2d_m2.h"   // pour t2d::Shape / Vec2 / Segment

#include <cmath>
#include <numeric>
#include <algorithm>
#include <numbers>                // C++20, inclus HORS namespace

// π (C++20)
static constexpr double kPI = std::numbers::pi_v<double>;

namespace { // utils privés à ce TU

    struct LCG {
        std::uint64_t s;
        explicit LCG(std::uint64_t seed = 0x9e3779b97f4a7c15ULL) : s(seed) {}
        std::uint32_t next() { s = 2862933555777941757ULL * s + 3037000493ULL; return (std::uint32_t)(s >> 32); }
        double uniform() { return (next() + 0.5) / 4294967296.0; } // [0,1)
        int    uniformInt(int lo, int hi) { if (hi <= lo) return lo; return lo + (int)std::floor(uniform() * (double)(hi - lo + 1)); }
        double angle() { return uniform() * 2.0 * kPI; }
    };

    inline int clampi(int v, int lo, int hi) { return std::min(std::max(v, lo), hi); }

} // namespace

namespace t2dgen {

    struct RandomGenPolyShape::Impl {
        Params P;
        LCG rng;

        // Représentation interne (indépendante des types t2d::)
        struct Vec2 { double x{}, y{}; };
        struct Segment { int a{ -1 }, b{ -1 }; };
        struct Poly { int v0{ 0 }, vcount{ 0 }; int e0{ 0 }, ecount{ 0 }; double radius{ 0 }; };

        std::vector<Vec2> V;
        std::vector<Segment> E;
        std::vector<int> drawOrder;
        std::vector<Poly> polys;

        int totalV{ 0 };
        int lastIterations{ 0 };           // <-- mémorise r (1..4) de la dernière génération

        explicit Impl(Params p) : P(p), rng(p.seed) {}

        void reset() { V.clear(); E.clear(); drawOrder.clear(); polys.clear(); totalV = 0; }

        void addRegularPolygon(const Vec2& center, double radius, int sides, double orientRad) {
            sides = clampi(sides, P.min_sides, P.max_sides);
            if (sides < 3 || radius <= 0.0) return;

            const int v0 = (int)V.size();
            const int e0 = (int)E.size();

            const double dtheta = 2.0 * kPI / (double)sides;
            for (int i = 0; i < sides; ++i) {
                double a = orientRad + i * dtheta;
                V.push_back({ center.x + radius * std::cos(a),
                              center.y + radius * std::sin(a) });
            }
            for (int i = 0; i < sides; ++i) {
                int a = v0 + i;
                int b = v0 + ((i + 1) % sides);
                E.push_back({ a,b });
                drawOrder.push_back((int)E.size() - 1);
            }
            polys.push_back(Poly{ v0, sides, e0, sides, radius });
        }

        void addBaseOctagon(double size) {
            // size ≈ diamètre → rayon = size/2
            addRegularPolygon({ 0.0,0.0 }, size * 0.5, 8, rng.angle());
        }

        t2d::Shape build() {
            reset();

            const int r = P.fixed_iterations ? clampi(*P.fixed_iterations, 1, 4)
                : rng.uniformInt(1, 4);
            lastIterations = r; // <-- stocké pour le getter

            // 1) polygone racine : octogone
            addBaseOctagon(P.base_size);

            // 2) itérations : pour chaque sommet de chaque polygone courant → un sous-polygone régulier
            std::vector<Poly> frontier; frontier.push_back(polys.back());
            for (int depth = 1; depth <= r; ++depth) {
                std::vector<Poly> next; next.reserve(frontier.size() * 8);
                for (const Poly& pr : frontier) {
                    const int v0 = pr.v0;
                    const int cnt = pr.vcount;
                    const double childR = pr.radius * P.child_scale;
                    for (int i = 0; i < cnt; ++i) {
                        const Vec2 center = V[v0 + i];
                        const int  sides = rng.uniformInt(P.min_sides, P.max_sides);
                        addRegularPolygon(center, childR, sides, rng.angle());
                        next.push_back(polys.back());
                    }
                }
                frontier.swap(next);
            }

            // 3) conversion → t2d::Shape
            t2d::Shape out;
            out.V.reserve(V.size());
            out.E.reserve(E.size());
            out.draw_order.reserve(drawOrder.size());

            for (const auto& p : V) out.V.push_back({ p.x, p.y });
            for (const auto& s : E) out.E.push_back({ s.a, s.b });
            for (int idx : drawOrder) out.draw_order.push_back(idx);

            totalV = (int)out.V.size();
            return out;
        }
    };

    // --- API publique ---

    RandomGenPolyShape::RandomGenPolyShape(Params p) : d_(new Impl(p)) {}
    RandomGenPolyShape::~RandomGenPolyShape() { delete d_; }

    t2d::Shape RandomGenPolyShape::generate() { return d_->build(); }
    int RandomGenPolyShape::totalVertices() const { return d_->totalV; }
    int RandomGenPolyShape::iterations()    const { return d_->lastIterations; }

    long long RandomGenPolyShape::theoreticalMaxVertices(int r) {
        r = clampi(r, 1, 4);
        long long sum = 0, p = 8; // 8^1 + 8^2 + ... + 8^r
        for (int d = 1; d <= r; ++d) { sum += p; p *= 8; }
        return 8 + 8 * sum; // 8 (base) + 8 * Σ(8^d)
    }

} // namespace t2dgen
