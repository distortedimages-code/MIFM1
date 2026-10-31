# MIFM1

Une groovebox 4 pistes pour le M-VAVE FM-1 dont tout le son vient du code open source de Mutable Instruments, sur
la base de [fm1-base](https://github.com/charlesvestal/fm1-base) :

- **Plaits** sur chaque piste, ses 24 modèles au complet ;
- **Marbles** comme seul séquenceur : ses sorties T déclenchent les pistes, X et Y donnent les notes, DÉJÀ VU fige
  ou libère la boucle ;
- **deux Tides 2** comme modulateurs (8 sorties) ;
- une **matrice de câbles** : 15 sources (Tides A1-A4, B1-B4, Marbles X1-X3, Y, T1-T3) vers presque tous les
  paramètres, 8 câbles par piste et 8 globaux ;
- un **mixer simple** (un niveau par piste) et un **départ réverbe** par piste vers la réverbe de Clouds.

Le granulaire de Clouds et Warps arrivent en v0.2 (sur le second cœur), les scènes et les projets multiples en v0.3. Rings a été
retiré après le banc d'essai v0.0 : les modèles MODAL et STRING de Plaits en reprennent le code, en plus léger.

## Installer

À chaque push, GitHub compile le firmware et publie l'installeur sur
<https://distortedimages-code.github.io/MIFM1/>. Dans Chrome ou Edge, FM-1 branché en USB et à l'arrêt : installer,
laisser la page écrire les tables des modules après le firmware. Le firmware d'origine se remet de la même façon.

## Jouer

Un projet neuf joue déjà (PLAY) : piste 1 une grosse caisse sur chaque temps (T2), piste 2 une basse VA sur T1 et
X1 ouverte par Tides A, piste 3 un FM 2 opérateurs sur T3 et X2 modulé par Tides B, piste 4 un résonateur MODAL sur
T3 et X3, les pistes 3 et 4 envoyées dans la réverbe. Marbles à 120 BPM, gamme mineure, DÉJÀ VU à midi sur 8 pas.

**Repartir de zéro (INIT)** : GLO, puis **K1** choisit le projet et **REC tenu** le charge : *New* (4 pistes
Plaits sur VIRTUAL ANALOG, tous les potards à midi, jouées par t1, t2, t3 et t2 avec X1, X2, X3 et Y, aucun câble,
rien vers la réverbe, Marbles en do majeur), *The demo patch* (le patch ci-dessus) ou *The saved project* (le dernier
sauvegardé, pour annuler). Le transport s'arrête ; rien n'est sauvegardé tant que SAVE n'est pas tenu.

L'écran est la façade du module affiché, dessinée comme Mutable Instruments dessinait les siennes (d'après les
fichiers des panneaux publiés avec leur code) : l'aluminium gris clair, la frise tissée en haut, les légendes noires en
capitales, les icônes rose et bleu-vert (et l'orange de Tides 2), des boutons blancs avec leurs « − + » quand ils vont
dans les deux sens, des pointillés pour ce qui est câblé, les sorties dans leur boîte gris foncé. En haut le nom du
module et son affichage : les 16 icônes de Plaits et sa rangée de LED (couchée), les modes et les quatre sorties de
Tides, le *t* et le *X* de Marbles avec leurs icônes et leurs sorties, l'étoile autour de RATE et SPREAD, le disque
gravé de Clouds et ses étiquettes rose et bleu-vert pour la réverbe. Puis les quatre potards côte à côte, dans l'ordre
des KNOB 1 à 4, avec les noms des panneaux d'origine ; en bas les quatre pistes comme quatre sorties, la LED de chaque
piste dans la couleur de son modèle. La frise devient rose en mode MOD, bleu-vert dans la matrice.

| Contrôle | Effet |
| --- | --- |
| **KNOB 1 à 4** | les 4 paramètres de la page affichée |
| **SELECT** | l'autre face du module (PLAITS 1/3, 2/3, 3/3…) |
| **ALGORITHM** | le modèle de Plaits, le mode de rampe de Tides, le modèle de T de Marbles, la gamme |
| **PRESETS** | la piste active, de 1 à 4 |
| **EDIT** | Plaits de la piste active (appuis suivants : 1/3, 2/3, 3/3) |
| **LFO** | Tides A, puis Tides B |
| **SEQ / ARP** | Marbles, rythme (T) / notes (X, Y) |
| **HOME** | le mixer : les niveaux des 4 pistes |
| **FX** | la réverbe : les envois des 4 pistes, puis ses réglages (Time, Damping, Diffusion, Return) |
| **PLAY** | l'horloge de Marbles tourne ou s'arrête |
| **clavier** | joue la piste active, même à l'arrêt ; **OCT− / OCT+** pour l'octave |
| **SEL** | le mode MOD (voir plus bas) ; **tenu** : la matrice de la piste |
| **SAVE tenu** | sauvegarde le projet (à l'arrêt) |
| **GLO** | la charge des deux cœurs, les tables, la version ; **K1** choisit un projet, **REC tenu** le charge (INIT) |

La charge de calcul est toujours affichée en haut à droite (le cœur le plus chargé, en %).

**Le mode MOD** (SEL) : le clavier devient une rangée de sources. On tient une touche et on tourne un potard : le
câble est posé, le sens et la quantité de rotation fixent la profondeur ; revenir à 0 le retire. Touches 1-4 :
Tides A1-A4 ; 5-8 : Tides B1-B4 ; 9-11 : Marbles X1-X3 ; 12 : Y ; 13-15 : T1-T3 ; 17 et 18 : Tides A ou B en éventail
(A n sur le potard n). Les touches des sources qui modulent la page s'allument.

**La matrice** (SEL tenu) : les 8 câbles de la piste puis les 8 globaux. K1 choisit le câble, K2 sa source, K3 sa
cible, K4 sa profondeur.

**PLAITS 3/3** choisit pour chaque piste sa source de gate (T1-T3, une sortie de Tides, ou le clavier seul) et de
note (X1-X3, Y ou fixe), la transposition et l'octave.

## Le dépôt

- `firmware/` : le firmware (fm1-base `tools/build.py ../firmware`)
  - `app.c` : l'écran et les contrôles ; `mi/engine.cc` : le son (les pistes, Marbles, Tides, la matrice, le
    mixer ; les pistes 3 et 4 sur le second cœur) ; `mi/mi_*.cc` : un pont C par module Mutable
  - `third_party/` : le code Mutable Instruments utilisé, copié tel quel ; les retouches sont dans `PATCHES.md`
  - `res/mi_tables.bin` : la zone de ressources (les plus grosses tables, écrites par l'installeur après le
    firmware), `mi/res_abs.cc` leurs adresses
  - `shim/` : les en-têtes C et C++ que la chaîne JieLi n'a pas (pas de libm, pas de bibliothèque C++)
  - `tests/` : le moteur sur l'ordinateur (`native.cc`) et le simulateur de fm1-base (`screens.txt`)
- `tools/` : la copie du code Mutable (`vendor_mi.sh`, `patch_mi.sh`, `split_tables.py`), les vérifications « comme
  sur le FM-1 » (`device_check.sh`, `check_res_links.py`, `no_static_init.py`), l'installeur ; `tools/bench/` : le
  banc d'essai v0.0
- `make test` : le patch par défaut joué 16 s sur l'ordinateur, chaque piste et chaque modèle entendus, sans NaN
  (WAV dans `build/wav`)

## Licences

Le code de ce dépôt est sous MPL-2.0, comme fm1-base. Le code de Mutable Instruments (Émilie Gillet) est sous
licence MIT (`firmware/third_party/LICENSE-eurorack`). Mutable Instruments est une marque déposée : ce projet ne
l'utilise pas comme nom et ne cite les modules que pour dire de qui est le code qu'il fait tourner.
