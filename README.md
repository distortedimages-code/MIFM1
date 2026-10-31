# MIFM1

Un firmware pour le M-VAVE FM-1 construit uniquement sur le code open source de Mutable Instruments (Plaits, Rings,
Clouds, Warps, Tides 2, Marbles), sur la base de [fm1-base](https://github.com/charlesvestal/fm1-base).

**Version 0.0 : le banc d'essai.** Avant d'écrire le moindre écran, on mesure ce que chaque module coûte vraiment
au FM-1 : le temps de calcul (sur un cœur, puis sur les deux), la RAM et la flash. C'est ce qui décidera de la
forme du vrai firmware.

## Un seul firmware, les six modules au complet

Tout tient dans une installation, sans retirer un seul algorithme : les 24 modèles de Plaits, les 6 de Rings, les
4 modes de Clouds, les algorithmes de Warps, Tides 2 et Marbles. Le code et les tables des modules ne tiennent pas
ensemble dans les 581 Ko de mémoire programme du FM-1 ; les trois plus grosses tables (les distributions de
Marbles, les tables d'ondes de Plaits, les courbes de Tides : 145 Ko) vivent donc dans la **zone de ressources**
de fm1-base, une partie de la zone de données que l'installeur écrit juste après le firmware. Le code de Mutable
n'est pas modifié : sur le FM-1, ces tables sont simplement lues à leur nouvelle adresse (`tools/split_tables.py`),
et `make test` vérifie que ce sont exactement les mêmes octets.

Si la zone n'est pas écrite (installation interrompue), le firmware démarre quand même et dit de réinstaller ;
aucune scène ne joue sans ses tables.

Les **mix** font tourner le cœur 0 (4 voix Plaits jouées par Marbles, 2 Tides en modulation) pendant que le
second cœur fait les effets (Rings, Clouds, Warps), comme dans la spec.

## Installer

À chaque push, GitHub compile le firmware et publie l'installeur sur
<https://distortedimages-code.github.io/MIFM1/>. Dans Chrome ou Edge, FM-1 branché en USB et à l'arrêt :
installer, laisser la page écrire les tables après le firmware, puis accepter m-bootloader (le démarrage USB de
secours). Le firmware d'origine (V15) se remet de la même façon.

## Le banc

| Contrôle | Effet |
| --- | --- |
| **REC** | le banc automatique : chaque scène 3 secondes (54 scènes, moins de 3 minutes). REC à nouveau : stop |
| **SELECT** | la scène (page LIVE) ou le défilement (page RÉSULTATS) |
| **PLAY** | le son, oui ou non (le calcul continue) |
| **HOME** | la page : LIVE, RÉSULTATS, MÉMOIRE |
| **SEL** | les mix sur deux cœurs ou un seul |

Les chiffres sont en **% d'un bloc audio** (128 échantillons, 2902 µs) : 100 % = le cœur est plein. **MOY** est la
moyenne, **PIC** le pire bloc. Pour les mix, la colonne **C1** est le second cœur, et l'astérisque veut dire qu'il
n'a pas démarré (tout a tourné sur le cœur 0). Un **!** signale des blocs en retard.

Les résultats du dernier banc restent gardés après extinction.

**Ce qu'il faut renvoyer** : à la fin du banc, la page RÉSULTATS en photos (quatre écrans, SELECT pour défiler) et
la page MÉMOIRE. Ou, plus simple : la page de sauvegarde du site (« backup ») les enregistre dans un fichier
`.mifm1`, que `tools/read_results.py` transforme en tableau.

À surveiller : si « MIX+RINGS » coûte bien plus que « MIX 4 VOIX » + « R MODAL », c'est que les nombres
dénormalisés (les résonances qui s'éteignent) sont lents sur ce processeur.

## Le dépôt

- `bench/` : le firmware (fm1-base `tools/build.py ../bench`)
  - `mi/` : un pont C par module (leurs en-têtes ne peuvent pas se croiser dans une même unité), les scènes et la
    mesure du temps
  - `third_party/` : le code Mutable Instruments utilisé, copié tel quel ; les quatre retouches sont dans
    `PATCHES.md`
  - `res/mi_tables.bin` : la zone de ressources (les tables déplacées), `mi/res_symbols.cc` leurs adresses
  - `shim/` : les en-têtes C et C++ que la chaîne JieLi n'a pas (pas de libm, pas de bibliothèque C++)
- `tools/` : la copie du code Mutable (`vendor_mi.sh`, `patch_mi.sh`, `split_tables.py`), la vérification « comme
  sur le FM-1 » avec le clang de l'ordinateur (`device_check.sh`), l'installeur, la lecture des résultats
  (`read_results.py`)
- `make test` : chaque scène jouée sur l'ordinateur, sans NaN ni silence (WAV dans `build/wav`), et la zone de
  ressources comparée octet par octet aux tables d'origine

## Licences

Le code de ce dépôt est sous MPL-2.0, comme fm1-base. Le code de Mutable Instruments (Émilie Gillet) est sous
licence MIT (`bench/third_party/LICENSE-eurorack`). Mutable Instruments est une marque déposée : ce projet ne
l'utilise pas comme nom et ne cite les modules que pour dire de qui est le code qu'il fait tourner.
