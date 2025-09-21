# time2d (C++20)

> Un moteur “canonique” du temps fondé sur la composition d’éléments (Foudre/Magmat, Terre/Glace, Vent/Bois) et une base **polyfractale**.  
> **Langage** : C++20 • **Build** : CMake ≥ 3.20 / Visual Studio 2022 • **Plateformes** : Win64 (testé)

---

## Sommaire

- [1. Idée générale](#1-idée-générale)  
- [2. Pipeline fractal (n=0 → n=2)](#2-pipeline-fractal-n0--n2)  
- [3. API C++ (surfaces publiques)](#3-api-c-surfaces-publiques)  
- [4. Démarrage rapide (CMake & Visual Studio)](#4-démarrage-rapide-cmake--visual-studio)  
- [5. Exemple minimal (console)](#5-exemple-minimal-console)  
- [6. Paramétrages conseillés](#6-paramétrages-conseillés)  
- [7. Intégration dans d’autres projets](#7-intégration-dans-dautres-projets)  
- [8. Notes conceptuelles & sécurité](#8-notes-conceptuelles--sécurité)  
- [Licence](#licence)

---

## 1. Idée générale

time2d ne “mesure” pas un temps linéaire : il **construit** des **objets temporels** par composition hiérarchique (fractal).

- **Base** : octogone racine (8 segments) ; à chaque itération, chaque sommet engendre un sous-polygone (3..8 côtés). Itérations limitées à **1..4**.  
- **M2 (Foudre/Magmat)** : génère un **plan d’événements** sur un axe **réel** (ticks **négatifs** en *Magmat*). Foudre crée de la multiplicité aléatoire et groupe par **τ** (percentile d’écarts).  
- **I2 (Terre/Glace)** : écoulement **granulaire** avec **durées de vie** ; comptabilise “mémorisé” vs “perdu” via une règle unique (`life >= wait + service`).  
- **W2 (Vent/Bois)** : **structure** (subdivisions, offsets, capacité de rebonds potentiels). L’état effectif (actifs/disparus) est décidé par les **Macros**.  
- **Macros** : cibles (min memorized, exact lost), facteurs multiplicatifs sur `life_mean`, agrégats W2.

---

## 2. Pipeline fractal (n=0 → n=2)

### n = 0 — forme & instantané

- Génération d’une forme **polyfractale** → `Shape {V,E,draw_order}`  
- M2 construit les événements : **Init** (≥0), **Foudre** (multiplicité, percentile τ), **Magmat** (**ticks négatifs** pour la reconstitution).

### n = 1 — granularité + vie (Terre/Glace)

- Écoulement FIFO avec temps de service constant `service_time = 1/throughput`.  
- Chaque grain a une **durée de vie** (glace). Il est **mémorisé** si `life >= wait + service`.

### n = 2 — structure & empreintes (Vent/Bois)

- **Structure W2** = slots : `0` (container), `1..capacity` (empreintes potentielles).  
- **Macros** (Vent/Bois) décident du **nombre cible** de rebonds et du **nombre actif** (effaçable par le vent, maintenu si `CORPSE_TIME>0`).

---

## 3. API C++ (surfaces publiques)

### Générateur polyfractale

`engine/include/randomGenPolyShape.hpp`
```cpp
namespace t2dgen {
class RandomGenPolyShape {
public:
  struct Params {
    double       base_size = 1.0;
    double       child_scale = 0.25;
    int          min_sides = 3;
    int          max_sides = 8;
    std::uint64_t seed = 0;            // 0 => seed par défaut
    int          fixed_iterations = 0; // 0 => aléatoire, sinon clampé [1..4]
  };
  explicit RandomGenPolyShape(const Params& = Params{});
  ~RandomGenPolyShape();
  t2d::Shape generate() const;   // {V,E,draw_order}
  int  totalVertices() const;    // N
  int  iterations() const;       // r ∈ [1..4]
  static long long theoreticalMaxVertices(int r);
};
}

# time2d — Unités, calibration et exemples rapides

## 1) Unités (référence claire)

- **1 tick = 10 ms = 0.01 s**  
- Tous les temps internes (événements M2, `service_time` I2, etc.) sont en **ticks**.

Conversions pour l’API/chrono :
- `seconds = ticks * 0.01`
- `milliseconds = ticks * 10.0`

---

## 2) Calibration automatique (objectif : R rebonds, tolérance 8 %)

But : faire en sorte que la quantité **lisible** dans la fenêtre de lecture corresponde à la cible **R** :

- `readable_effective ≈ R` avec `|readable_effective - R| / R ≤ 0.08` (±8 %)

Hypothèses W2 (cohérentes avec les macros) :
- Capacité structurelle : `capacity = subdivision_level - 1`
- Cible de rebonds : `floor(PROCESS_EXISTENCE_TIME)` (bornée par la capacité)
- Empreintes actives : `floor(PROCESS_SUPPORT_TIME)` (bornée par la cible)  
  👉 Pour **toutes** les empreintes actives : mettre `PROCESS_SUPPORT_TIME ≥ R`.

Calibration recommandée :
- **Structure** : `subdivision_level = max(R + 1, 1)` (garantit une capacité ≥ R)
- **Macros Vent** :
  - `PROCESS_EXISTENCE_TIME = R`
  - `PROCESS_SUPPORT_TIME  = R`
- **Fenêtre de lecture** (`CONTAINER_TIME_READ`, côté interface) :  
  On veut `readable_effective = min(proj.grains_memorized, floor(center_share * READ / service_time)) ≈ R`  
  avec `center_share = 1 - edge_share` (par défaut **0.8** si `edge_share = 0.2`).

Donc :

avec `ε = 0.08` (8 %).

**BOIS** : `ENVIRONNMENT_CORPSE_TIME` module la “life” affichée des empreintes (règle : `life_k = CORPSE / (1 + SUPPORT*k)`).  
Pour une échelle lisible, prenez un ordre de grandeur de **quelques** `service_time` (ex. `≈ 2 × service_time_seconds`).  
> N’influence ni `active` (sauf le cas minimal) ni la lecture : c’est **métadonnée temporelle**.

---

## 3) Exemple prêt à tester : R = 24 rebonds

### Paramètres UI (`t2d::iface::Inputs`)
- `TIME_RETENTION_FACTOR = 4` (défaut – agit surtout sur la glace/projections)
- `CONTAINER_TIME_READ ≈ 324 ticks`  
  - En secondes : `324 × 0.01 = 3.24 s`  
  - En ms : `3240 ms`

*Calcul utilisé (avec `center_share = 0.8`) :*  
`READ = R * service_time / 0.8 * 1.08`.  
Si `proj.service_time ≈ 10 ticks` (≈ 0.1 s), alors `READ_needed ≈ 24 * 10 / 0.8 * 1.08 ≈ 324 ticks`.

### Paramètres W2 (`t2d::iface::W2Inputs`)
- `subdivision_level = 25`  (capacité = 24)
- `offset_step = 0.15`  (par défaut, ajustable)
- `PROCESS_EXISTENCE_TIME = 24`  (cible = nb rebonds)
- `PROCESS_SUPPORT_TIME  = 24`  (tous actifs)
- `ENVIRONNMENT_CORPSE_TIME ≈ 0.20 s` (≈ 20 ticks ~ `2 × service_time_s`)
- `ENVIRONNMENT_RECOVER_TIME = 0.0` (tag documentaire libre)

---

## 4) Résumé des règles

**Échelle** : `1 tick = 10 ms`. Les API externes convertissent en secondes via `ticks * 0.01`.

**W2** :
- `subdivision_level = R + 1`
- `PROCESS_EXISTENCE_TIME = R`
- `PROCESS_SUPPORT_TIME  = R`
- `ENVIRONNMENT_CORPSE_TIME ≈ 2 × service_time_seconds` (indicatif)

**Lecture** :

(avec `edge_share = 0.20` → `1 - edge_share = 0.8`)

**Tolérance** : viser `readable_effective ≈ R` avec `ε = 0.08` (8 %).

---

## 5) Exemple « reste à lire » : 240 rebonds, fenêtre = 500 ms

Rappels :
- `capacity_brute = READ / service_time`
- `partie_lisible_centre = capacity_brute * (1 - edge_share)` avec `edge_share = 0.20` → facteur **0.8**
- `rebonds_lus = floor(capacity_brute * 0.8)`
- `rebonds_restants = max(0, 240 - rebonds_lus)`

Ici `READ = 500 ms = 0.5 s`. Il faut votre `service_time` (sortie I2). À défaut, cas typiques :

| service_time | lus dans 0.5 s | restants sur 240 |
|---:|---:|---:|
| 5 ms   | `floor(0.5/0.005 * 0.8) = 80` | 160 |
| 10 ms  | 40  | 200 |
| 20 ms  | 20  | 220 |
| 50 ms  | 8   | 232 |
| 100 ms | 4   | 236 |

### Snippet C++ (calcul générique)
```cpp
// 1 tick = 10 ms
inline double ticks_to_seconds(double ticks){ return ticks * 0.01; }
inline double seconds_to_ticks(double sec){ return sec * 100.0; }

struct ReadCalc {
  int rebonds_lus;
  int rebonds_restants;
};

ReadCalc calc_rebonds(double read_seconds, double service_time_seconds,
                      int total_rebonds, double edge_share = 0.20)
{
  if (service_time_seconds <= 0.0) service_time_seconds = 1e-9;
  const double capacity = read_seconds / service_time_seconds;     // brut
  const double center_share = 1.0 - edge_share;                    // typ. 0.8
  const int rebonds_lus = (int)std::floor(capacity * center_share);
  const int restants = std::max(0, total_rebonds - rebonds_lus);
  return { rebonds_lus, restants };
}

// Exemple: fenêtre = 0.5s, total = 240, service_time = 0.01s (10 ms)
auto r = calc_rebonds(0.5, 0.01, 240); // r.rebonds_lus=40, r.rebonds_restants=200
