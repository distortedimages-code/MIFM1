# MIFM1

Un firmware pour le M-VAVE FM-1 construit uniquement sur le code open source de Mutable Instruments (Plaits, Rings,
Clouds, Warps, Tides 2, Marbles), sur la base de [fm1-base](https://github.com/charlesvestal/fm1-base).

**Version 0.0 : le banc d'essai.** Avant d'écrire le moindre écran, on mesure ce que chaque module coûte vraiment
au FM-1 : le temps de calcul (sur un cœur, puis sur les deux), la RAM et la flash. C'est ce qui décidera de la
forme du vrai firmware.

## Deux firmwares

Le code et les tables des six modules ne tiennent pas d'un coup dans les 581 Ko de flash du FM-1 (environ 540 Ko
pour les modules seuls, plus 80 Ko de plateforme). Le banc existe donc en deux variantes :

| Variante | Modules | Scènes |
| --- | --- | --- |
| **SONS** | Plaits, Rings, Tides 2, Marbles | les 24 modèles de Plaits, 9 réglages de Rings, Tides x2, Marbles, 2 mix |
| **EFFETS** | Clouds, Warps (et Plaits pour les mix) | 4 modes de Clouds, 9 algorithmes de Warps, 3 mix |

Les **mix** font tourner le cœur 0 (4 voix Plaits jouées par Marbles, 2 Tides en modulation) pendant que le
second cœur fait les effets (Rings, Clouds, Warps), comme dans la spec.

## Installer

Une seule fois, sur GitHub : **Settings > Pages > Source : GitHub Actions** pour ce dépôt.

Ensuite, à chaque push, GitHub compile les deux firmwares et publie les installeurs sur
<https://distortedimages-code.github.io/MIFM1/>. Dans Chrome ou Edge, FM-1 branché en USB : choisir SONS ou
EFFETS, installer, et accepter m-bootloader (le démarrage USB de secours). Le firmware d'origine (V15) se remet de
la même façon.

## Le banc

| Contrôle | Effet |
| --- | --- |
| **REC** | le banc automatique : chaque scène 3 secondes (2 minutes en tout). REC à nouveau : stop |
| **SELECT** | la scène (page LIVE) ou le défilement (page RÉSULTATS) |
| **PLAY** | le son, oui ou non (le calcul continue) |
| **HOME** | la page : LIVE, RÉSULTATS, MÉMOIRE |
| **SEL** | les mix sur deux cœurs ou un seul |

Les chiffres sont en **% d'un bloc audio** (128 échantillons, 2902 µs) : 100 % = le cœur est plein. **MOY** est la
moyenne, **PIC** le pire bloc. Pour les mix, la colonne **C1** est le second cœur, et l'astérisque veut dire qu'il
n'a pas démarré (tout a tourné sur le cœur 0). Un **!** signale des blocs en retard.

**Ce qu'il faut renvoyer** : à la fin du banc, la page RÉSULTATS en photos (trois écrans, SELECT pour défiler), et
la page MÉMOIRE, pour chacune des deux variantes.

À surveiller : si « MIX+RINGS » coûte bien plus que « MIX 4 VOIX » + « R MODAL », c'est que les nombres
dénormalisés (les résonances qui s'éteignent) sont lents sur ce processeur.

## Le dépôt

- `bench/` : le firmware (fm1-base `tools/build.py ../bench`, `MIFM1_VARIANT=1` ou `2`)
  - `mi/` : un pont C par module (leurs en-têtes ne peuvent pas se croiser dans une même unité), les scènes et la
    mesure du temps
  - `third_party/` : le code Mutable Instruments utilisé, copié tel quel ; les trois retouches sont dans
    `PATCHES.md`
  - `shim/` : les en-têtes C et C++ que la chaîne JieLi n'a pas (pas de libm, pas de bibliothèque C++)
- `tools/` : la copie du code Mutable (`vendor_mi.sh`, `patch_mi.sh`), la vérification « comme sur le FM-1 » avec
  le clang de l'ordinateur (`device_check.sh`), les installeurs
- `make test` : chaque scène jouée sur l'ordinateur, sans NaN ni silence (WAV dans `build/wav`)

## Licences

Le code de ce dépôt est sous MPL-2.0, comme fm1-base. Le code de Mutable Instruments (Émilie Gillet) est sous
licence MIT (`bench/third_party/LICENSE-eurorack`). Mutable Instruments est une marque déposée : ce projet ne
l'utilise pas comme nom et ne cite les modules que pour dire de qui est le code qu'il fait tourner.
