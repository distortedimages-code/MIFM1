# MIFM1

Une groovebox 4 pistes pour le M-VAVE FM-1 dont tout le son vient du code open source de Mutable Instruments, sur
la base de [fm1-base](https://github.com/charlesvestal/fm1-base) :

- **Plaits** sur chaque piste, ses 24 modèles au complet ;
- **deux séquenceurs au choix** (GLO) : **Marbles** (ses sorties T déclenchent les pistes, X et Y donnent les notes,
  DÉJÀ VU fige ou libère la boucle) ou un **séquenceur pas à pas** à la Elektron, 64 pas par piste, avec P-locks ;
- **Endless** sur chaque piste, comme sur l'OP-1 : on enregistre des notes et des silences un par un, ils tournent
  en boucle, une touche les transpose ;
- sur chaque trig : **probabilité**, **ratchets**, et un **aléatoire** sur n'importe quel réglage de la piste (à
  droite nouveau à chaque trig, à gauche en boucle) ;
- un **quantiseur global** (15 gammes et la tonique) : aucune fausse note, sur toutes les pistes et au clavier ;
- **deux Tides 2** comme modulateurs (8 sorties) ;
- une **matrice de câbles** : 15 sources (Tides A1-A4, B1-B4, Marbles X1-X3, Y, T1-T3) vers presque tous les
  paramètres, 8 câbles par piste et 8 globaux ;
- **Warps** entre deux pistes (ou une piste et son oscillateur interne) : ses 6 modulations croisées et son easter
  egg, le décaleur de fréquence (son vocodeur est laissé de côté : il coûtait autant que les quatre voix). Le module
  tourne à 96 kHz : ce qui en dépend (les filtres du décaleur, l'oscillateur) est refait pour les 44,1 kHz du FM-1
  par ses propres scripts, et les filtres du suréchantillonnage de ses modulations croisées par la recette écrite
  au-dessus d'eux (plus longs, pour rester plats jusqu'à 12 kHz) ;
- un **mixer simple** (un niveau et un pan par piste, à puissance constante : une piste garde le même volume où
  qu'elle soit dans l'image ; ses gains glissent échantillon par échantillon, un pan ou un niveau aléatoire ou locké
  ne claque pas) et un **départ réverbe** par piste vers la réverbe de Clouds
  (ses lignes de retard en flottants : la réverbe du module les stockait sur 12 bits, d'où un grain et un souffle).

Le granulaire de Clouds arrive ensuite, les scènes et les projets multiples en v0.3. Rings a été
retiré après le banc d'essai v0.0 : les modèles MODAL et STRING de Plaits en reprennent le code, en plus léger.

## Installer

À chaque push, GitHub compile le firmware et publie l'installeur sur
<https://distortedimages-code.github.io/MIFM1/>. Dans Chrome ou Edge, FM-1 branché en USB et à l'arrêt : installer,
laisser la page écrire les tables des modules après le firmware. Le firmware d'origine se remet de la même façon.

## Jouer

Un projet neuf joue déjà (PLAY) : piste 1 une grosse caisse sur chaque temps (T2), piste 2 une basse VA sur T1 et
X1 ouverte par Tides A, piste 3 un FM 2 opérateurs sur T3 et X2 modulé par Tides B, piste 4 un résonateur MODAL sur
T3 et X3, les pistes 3 et 4 envoyées dans la réverbe et placées un peu à gauche et un peu à droite. Marbles à 120 BPM, gamme mineure, DÉJÀ VU à midi sur 8 pas.

**Repartir de zéro (INIT)** : GLO, SELECT (la page PROJECT), puis **K1** choisit le projet et **REC tenu** le charge : *New* (4 pistes
Plaits sur VIRTUAL ANALOG, tous les potards à midi, jouées par t1, t2, t3 et t2 avec X1, X2, X3 et Y, aucun câble,
rien vers la réverbe, Marbles en do majeur), *The demo patch* (le patch ci-dessus) ou *The saved project* (le dernier
sauvegardé, pour annuler). Le transport s'arrête ; rien n'est sauvegardé tant que SAVE n'est pas tenu.

L'écran est noir, ses lumières aux couleurs de Mutable Instruments (bleu-vert, orange, rose, celles de leurs
icônes). En haut, l'icône du module dans un anneau de sa couleur, son nom en grand et une ligne sur son état : sur Plaits,
le seul logo du modèle et son nom (KICK, VIRTUAL ANALOG…), sur Tides la rampe, sur Marbles le modèle de *t* ou la
gamme. Puis les quatre potards côte à côte, dans l'ordre des KNOB 1 à 4 : un anneau fin allumé de son début (ou de son
milieu, avec « − + ») jusqu'à la valeur, la valeur dedans, un câble en pointillé et son point d'arrivée dans l'autre
couleur, la source écrite dessous. En bas les quatre pistes, une LED chacune allumée par sa gate dans la couleur de son
modèle, celle qu'on joue en défonce (un bloc de sa couleur, le texte en noir) ; sur les pages de Tides et de Marbles, leurs sorties, allumées à leur hauteur ; avec le séquenceur pas à pas,
les 16 pas de la piste (allumés : un trig ; un point : des locks ; cerclé : celui qui joue).

| Contrôle | Effet |
| --- | --- |
| **KNOB 1 à 4** | les 4 paramètres de la page affichée |
| **SELECT** | l'autre face du module (PLAITS 1/3, 2/3, 3/3…) |
| **ALGORITHM** | le modèle de Plaits, le mode de rampe de Tides, le modèle de T de Marbles, la gamme |
| **PRESETS** | la piste active, de 1 à 4 |
| **EDIT** | Plaits de la piste active (appuis suivants : 1/3, 2/3, 3/3) |
| **LFO** | Tides A, puis Tides B |
| **SEQ** | les séquenceurs, celui qui joue d'abord : les pas de la piste (TRIG, RANDOM), Marbles t, Marbles X |
| **ARP** | Endless, celui de la piste (voir plus bas) |
| **HOME** | le mixer : les niveaux des 4 pistes, puis leurs pans |
| **FX** | les envois des 4 pistes, la réverbe (Time, Damping, Diffusion, Return), puis Warps (ses deux pages) |
| **ENV** | l'enveloppe de la piste (PLAITS 2/3 : DECAY, LPG COLOUR, TIMBRE ENV, MORPH ENV) |
| **PLAY** | l'horloge de Marbles tourne ou s'arrête |
| **clavier** | joue la piste active, même à l'arrêt ; **OCT− / OCT+** pour l'octave |
| **SEL** | le mode MOD (voir plus bas) ; **tenu** : la matrice de la piste |
| **SAVE tenu** | sauvegarde le projet (à l'arrêt) |
| **GLO** | SEQUENCER (Marbles ou Steps), TEMPO, SCALE, ROOT ; SELECT : le projet (INIT) et la machine ; GLO encore : retour |
| **GLO tenu + potard** | **ALL** : le même réglage sur les 4 pistes (ex. DECAY) ; GLO tenu + ALGORITHM : le même modèle. Tant que GLO est tenu, l'écran passe en jaune et noir. Les réglages restent ; pendant 4 s (cinq carrés jaunes en bas), **GLO** les remet comme avant ALL au début de la mesure suivante (GLO encore avant la mesure : ils restent) |
| **ENV tenu + potard** | **RND** : l'aléatoire de ce réglage sur la piste (avec GLO tenu aussi : sur les 4) |
| **REC** | avec STEPS : les touches blanches deviennent les 16 pas, les 4 premières noires les pages |
| **REC tenu** (pas à pas) | efface la piste : ses trigs, ses locks, ses aléatoires et ses câbles ; avec **GLO tenu** : les 4 pistes et les câbles globaux (Warps reste) |

Les appuis tenus (SAVE, REC pour effacer ou charger, SEL pour la matrice) affichent en bas ce qu'ils vont faire et
cinq carrés qui s'éteignent un à un : lâcher avant le dernier annule. Ceux qui effacent ou écrasent durent une seconde.

La charge de calcul est toujours affichée en haut à droite (le cœur le plus chargé, en %) ; GLO puis SELECT
détaille ce que coûte chaque piste et Warps. Les quatre voix et Warps
se répartissent entre les deux cœurs selon ce que chacun coûte, mesuré bloc après bloc (une voix MODAL ou STRING coûte
dix fois une VIRTUAL ANALOG).

**Le mode MOD** (SEL) : le clavier devient une rangée de sources. On tient une touche et on tourne un potard : le
câble est posé, le sens et la quantité de rotation fixent la profondeur ; revenir à 0 le retire. Touches 1-4 :
Tides A1-A4 ; 5-8 : Tides B1-B4 ; 9-11 : Marbles X1-X3 ; 12 : Y ; 13-15 : T1-T3 ; 17 et 18 : Tides A ou B en éventail
(A n sur le potard n). Les touches des sources qui modulent la page s'allument.

**La matrice** (SEL tenu) : les 8 câbles de la piste puis les 8 globaux. K1 choisit le câble, K2 sa source, K3 sa
cible, K4 sa profondeur.

**PLAITS 1/3** : FREQUENCY, puis HARMONICS, TIMBRE et MORPH sous le nom que leur donne le manuel de Plaits pour le
modèle choisi (le KICK : ATTACK, BRIGHTNESS, DECAY ; le FM 2-OP : RATIO, INDEX, FEEDBACK ; le WAVETABLE : BANK, ROW,
COLUMN…). **PLAITS 2/3** (ENV) : l'enveloppe. Les modèles qui ont la leur (STRING, MODAL, KICK, SNARE, HI-HAT, et les
6-OP FM dans leur preset) contournent le LPG de Plaits : LPG COLOUR est alors grisé, et pour STRING, MODAL et les trois
batteries, DECAY s'ajoute à leur decay (leur MORPH) : il marche sur toutes les pistes, avec ses locks et son
aléatoire.

**PLAITS 3/3** choisit pour chaque piste sa source de gate (T1-T3, une sortie de Tides, ou le clavier seul) et de
note (X1-X3, Y ou fixe), la transposition et l'octave (avec Marbles ; avec STEPS, les pas jouent la piste : GATE et
NOTE sont alors grisés, comme BIAS, JITTER, T RANGE et DEJA VU t de Marbles, dont les sorties T ne servent plus que de
sources de modulation ; RATE reste le tempo).

Un potard grisé tourne toujours, mais ne change rien au son dans l'état présent.

**Le séquenceur pas à pas** (GLO, K1 sur STEPS ; le tempo est celui de Marbles, K2 sur GLO) : chaque piste a son
pattern, jusqu'à 64 pas (LENGTH). **REC** : les 16 touches blanches sont les pas de la page (allumés : un trig, celui
qui joue clignote), les 4 premières touches noires choisissent la page. Appuyer sur un pas pose un trig ; appuyer sur
un trig l'enlève (avec ses locks). **Tenir un pas et tourner un potard pose un P-lock** sur ce pas, sur n'importe
quelle page de la piste (Plaits, son niveau, son envoi, son pan, sa note…) ; le potard montre alors la valeur du pas
(LOCK dessous). Tenir un pas et taper SEL efface ses locks. La page SEQ de la piste : **NOTE**, **PROB**
(probabilité), **RATCHET** (1 à 8 coups dans le pas) et **LENGTH** ; les trois premiers se lockent aussi pas par pas.

**L'aléatoire** : la page RANDOM (SEQ, puis SELECT) a quatre potards, PAN, DECAY, TUNE et RATCHET. Tourné à droite,
une nouvelle valeur au hasard à chaque trig ; à gauche, des valeurs au hasard mais qui reviennent les mêmes à chaque
tour (une boucle) ; plus on tourne, plus l'écart est grand (100 : toute la course ; TUNE : une octave de part et
d'autre ; RATCHET : jusqu'à 7 coups de plus). Au centre, rien : le réglage retrouve exactement sa valeur. **ENV tenu
+ un potard** fait la même chose sur n'importe quel réglage de la piste (le TIMBRE, le MORPH, le niveau…). Les deux
marchent aussi avec Marbles (la boucle suit alors la LENGTH de Marbles).

**Le quantiseur** (GLO, SCALE et ROOT) : chaque note, de Marbles, des pas, de l'aléatoire ou du clavier, va à la note
de la gamme la plus proche. Off, Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Harmonic et Melodic
minor, Major et Minor pentatonic, Blues, Whole tone, Hirajoshi.

**Endless** (ARP, comme sur l'OP-1) : sur sa page, **REC** lance l'enregistrement (la frise passe en REC, le bouton
clignote) ; chaque touche ajoute une note (on l'entend), **SEL** ajoute un silence, jusqu'à 64 pas. **REC** à
nouveau : la piste joue ces notes en boucle sur le tempo, par-dessus ses pas ou Marbles. Pendant qu'elle joue, **une
touche transpose** la boucle depuis sa première note (la transposition reste). Ses potards : RATE (1/32 à 1/4, avec
les triolets), GATE (la longueur des notes), DIRECTION (avant, arrière, aller-retour, au hasard), TRANSPOSE. **REC
tenu** efface la boucle (la piste retrouve ses pas ou Marbles). Probabilité, ratchets, aléatoire et quantiseur
s'appliquent aussi à ses notes. En bas, ses notes en barres, la note jouée en blanc.

**Warps** (FX, troisième appui) : le module de modulation croisée de Mutable, entre deux pistes. Sa deuxième page (FX à nouveau, ou SELECT)
le branche : **CARRIER**, la porteuse (Off : Warps éteint ; une piste T1-T4 ; ou son oscillateur interne, trois formes),
**MODULATOR**, la piste modulante, **MOD DRY** (On : on entend aussi la piste modulante telle quelle ; Off, elle ne
passe que par Warps) et **MODE** (Warps, ou Freq shift : l'easter egg du module, un décaleur de fréquence). La sortie
de Warps prend la place de la piste porteuse (ou de la modulante avec l'oscillateur), avec son niveau, son pan et son
envoi. Sa première page, ce sont les potards du module : **ALGORITHM** balaie ses six modulations croisées
(crossfade, cross-folding, anneau à diodes, anneau numérique, XOR, comparateur), en fondu de l'une à l'autre comme
sur le module (sa course jusqu'au comparateur ; le vocodeur, au-delà, n'y est pas) ; le sélecteur ALGORITHM saute
d'un repère au suivant. **TIMBRE**, **LEVEL 1** et **LEVEL 2** comme sur le module : avec l'oscillateur interne, LEVEL 1
règle sa note (C0 à C5), comme le potard du module. En Freq shift : ALGORITHM fixe le décalage (au centre, rien ;
jusqu'à ±2 kHz), TIMBRE la bande (vers le haut, vers le bas), LEVEL 1 la réinjection, LEVEL 2 le mélange. Les câbles
de la matrice vont aussi sur ses potards (Tides sur ALGORITHM…). Warps joue un bloc derrière les voix (~4 ms de
latence sur la piste qu'il remplace). 

**La sortie** (GLO, SELECT, K2) : Stereo, Mic headset ou Mono. **Mic headset** est pour un casque à micro (fiche à
4 contacts) branché à fond dans la prise du FM-1 (3 contacts) : ses écouteurs se retrouvent en série entre la
gauche et la droite et ne reçoivent que leur différence (le centre disparaît, ce qui est panoramiqué ressort). Le
canal droit inversé, ils reçoivent tout le mix, en mono (une oreille en opposition de phase : le son paraît un peu
« dans la tête »). Pour la stéréo, un casque sans micro ou un adaptateur casque/micro. Le réglage reste quand on
charge un autre projet.

**Le clavier** relance la note à chaque touche, même jouée legato, et une touche tapée très vite sonne quand même.

Un projet sauvegardé avec la 0.1, la 0.1.3 ou les 0.1.4 à 0.1.9 se recharge tel quel (ses réglages, ses modèles, ses câbles) ; le séquenceur pas à
pas y arrive avec les patterns de démo, sur Marbles par défaut.

## Le dépôt

- `firmware/` : le firmware (fm1-base `tools/build.py ../firmware`)
  - `app.c` : l'écran et les contrôles ; `mi/engine.cc` : le son (les pistes, Marbles ou les pas, les trigs, Tides,
    la matrice, le quantiseur, Warps, le mixer ; les voix et Warps partagés entre les deux cœurs) ; `mi/mi_*.cc` : un pont C par module
    Mutable ; `mi/mi_reverb.h` : la réverbe de Clouds, ses lignes de retard en flottants
  - `third_party/` : le code Mutable Instruments utilisé, copié tel quel ; les retouches sont dans `PATCHES.md`
  - `res/mi_tables.bin` : la zone de ressources (les plus grosses tables, écrites par l'installeur après le
    firmware), `mi/res_abs.cc` leurs adresses
  - `shim/` : les en-têtes C et C++ que la chaîne JieLi n'a pas (pas de libm, pas de bibliothèque C++)
  - `tests/` : le moteur sur l'ordinateur (`native.cc`) et le simulateur de fm1-base (`screens.txt`)
- `tools/` : la copie du code Mutable (`vendor_mi.sh`, `patch_mi.sh`, `split_tables.py`), les vérifications « comme
  sur le FM-1 » (`device_check.sh`, `check_res_links.py`, `no_static_init.py`), l'installeur ; `tools/bench/` : le
  banc d'essai v0.0 (il se construit depuis le tag `bench-0.0` : les ponts vers les modules ont changé depuis)
- `make test` : le patch par défaut joué 16 s sur l'ordinateur, chaque piste et chaque modèle entendus, sans NaN
  (WAV dans `build/wav`)

## Licences

Le code de ce dépôt est sous MPL-2.0, comme fm1-base. Le code de Mutable Instruments (Émilie Gillet) est sous
licence MIT (`firmware/third_party/LICENSE-eurorack`). Mutable Instruments est une marque déposée : ce projet ne
l'utilise pas comme nom et ne cite les modules que pour dire de qui est le code qu'il fait tourner.
