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
