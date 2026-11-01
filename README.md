# MIFM1

Une groovebox 4 pistes pour le M-VAVE FM-1 dont tout le son vient du code open source de Mutable Instruments, sur
la base de [fm1-base](https://github.com/charlesvestal/fm1-base) :

- **Plaits** sur chaque piste, ses 24 modèles au complet, et un **grit** par piste (drive doux, dur ou replié,
  réduction de fréquence d'échantillonnage et de bits) ;
- un **séquenceur pas à pas** à la Elektron, 64 pas par piste, avec P-locks, et deux modes de jeu : **KEY** (le
  clavier joue la piste) et **STEP** (le clavier est la grille, les touches noires choisissent la piste ou l'effet) ;
- **Endless** sur chaque piste, comme sur l'OP-1 : on enregistre des notes et des silences un par un, ils tournent
  en boucle, une touche les transpose ;
- sur chaque trig : **probabilité**, **ratchets**, **longueur**, et un **aléatoire** sur n'importe quel réglage de la
  piste (à droite nouveau à chaque trig, à gauche en boucle) ; un **swing** global ; des **rythmes euclidiens** sur
  chaque piste de pas ;
- un **quantiseur global** (15 gammes et la tonique) : aucune fausse note, sur toutes les pistes et au clavier ;
- **deux Tides 2** comme modulateurs (8 sorties), déclenchables par les trigs d'une piste ou calés sur le tempo ;
- une **matrice de câbles** : les 8 sorties de Tides vers presque tous les paramètres, 8 câbles par piste et 8 globaux ;
- **trois effets, chacun avec sa propre piste de pas** : un **delay** inspiré des potards du Zen Delay (temps en
  divisions du tempo, feedback, filtre passe-bas / passe-haut avec résonance, drive, mono ou ping-pong, une légère
  modulation de bande), la **réverbe de Clouds** et **Warps**. Sans pas, l'effet est toujours ouvert ; avec des pas,
  chaque trig l'ouvre et il se referme avec son DECAY (jusqu'à 1 − DEPTH), et ses P-locks changent ses réglages pas
  par pas (le temps du delay, l'algorithme de Warps…) ;
- **Warps** entre deux pistes (ou une piste et son oscillateur interne) : ses 6 modulations croisées et son easter
  egg, le décaleur de fréquence (son vocodeur est laissé de côté : il coûtait autant que les quatre voix). Le module
  tourne à 96 kHz : ce qui en dépend (les filtres du décaleur, l'oscillateur) est refait pour les 44,1 kHz du FM-1
  par ses propres scripts, et les filtres du suréchantillonnage de ses modulations croisées par la recette écrite
  au-dessus d'eux (plus longs, pour rester plats jusqu'à 12 kHz). Par défaut, Warps est en **Eco** : ses modulations
  croisées suréchantillonnées 3 fois au lieu de 6 (moitié moins de calcul, le même son à quelques repliements près
  dans l'extrême aigu des algorithmes les plus brillants, XOR surtout) ; **HQ** (GLO, SELECT, K3) remet le 6x du
  module ;
- un **mixer simple** (un niveau et un pan par piste, à puissance constante : une piste garde le même volume où
  qu'elle soit dans l'image ; ses gains glissent échantillon par échantillon, un pan ou un niveau aléatoire ou locké
  ne claque pas), un **EQ** par piste (un passe-bas ou un passe-haut raide sur un seul potard, pour lui faire de la
  place), un envoi par piste vers le delay et un vers la réverbe (ses lignes de retard en flottants : la
  réverbe du module les stockait sur 12 bits, d'où un grain et un souffle).
- un **duck** : T2, T3 et T4 baissent à chaque trig de T1 (sa profondeur par piste, un DECAY commun), avant leurs
  envois : ce qu'elles envoient au delay et à la réverbe baisse avec elles ;
- un **bus master** : un compresseur (seuil, ratio, release, gain) ou un mode **OTT** (trois bandes compressées vers
  le haut et vers le bas, d'après le preset d'Ableton que le plug-in de Xfer a rendu célèbre : DEPTH, TIME, DRIVE,
  LEVEL), puis le limiteur de stmlib (celui de Rings et d'Elements).

Marbles a été retiré en 0.3 (le séquenceur pas à pas fait tout, plus vite), Rings après le banc d'essai v0.0 (les
modèles MODAL et STRING de Plaits en reprennent le code, en plus léger).

## Installer

À chaque push, GitHub compile le firmware et publie l'installeur sur
<https://distortedimages-code.github.io/MIFM1/>. Dans Chrome ou Edge, FM-1 branché en USB et à l'arrêt : installer,
laisser la page écrire les tables des modules après le firmware. Le firmware d'origine se remet de la même façon.

## Jouer

Un projet neuf joue déjà (PLAY) : piste 1 une grosse caisse sur chaque temps, piste 2 une basse VA dont Tides A
(déclenché par ses notes) ouvre le MORPH, piste 3 un FM 2 opérateurs en mélodie aléatoire bouclée, jetée dans le delay
sur deux de ses notes (la piste de pas du delay), piste 4 un résonateur MODAL dans la réverbe. 120 BPM, do mineur.

**Repartir de zéro (INIT)** : GLO, SELECT (la page PROJECT), puis **K1** choisit le projet et **REC tenu** le
charge : *New* (4 pistes Plaits sur VIRTUAL ANALOG, tous les potards à midi, aucun pas, aucun câble, rien vers les
effets, do majeur), *The demo patch* (le patch ci-dessus) ou *The saved project* (le dernier sauvegardé, pour
annuler). Le transport s'arrête ; rien n'est sauvegardé tant que SAVE n'est pas tenu.

L'écran est noir, ses lumières aux couleurs de Mutable Instruments (bleu-vert, orange, rose, celles de leurs
icônes). En haut, l'icône du module dans un anneau de sa couleur, son nom en grand et une ligne sur son état. Puis
les quatre potards côte à côte, dans l'ordre des KNOB 1 à 4 : un anneau fin allumé de son début (ou de son milieu,
avec « − + ») jusqu'à la valeur, la valeur dedans, un câble en pointillé et son point d'arrivée dans l'autre couleur,
la source écrite dessous. En bas les quatre pistes, une LED chacune allumée par sa gate dans la couleur de son modèle,
celle qu'on joue en défonce (chaque case sous son potard, le modèle en 4 lettres : KICK, VA, 6OP1, WTBL…) ; sur les pages de Tides, ses sorties ; en mode STEP, sur les pages des pas et sur celles
des effets, les 16 pas de la piste (allumés : un trig ; un point : des locks ; cerclé : celui qui joue ; pour un effet,
à droite, à quel point il est ouvert). Quand on change de face dans un même module (SELECT), la nouvelle page glisse
depuis le côté vers lequel on tourne : les potards de la page suivante poussent les autres, l'icône du module
reste, et les noms (le module, le titre, la ligne dessous) se réécrivent, leurs lettres au hasard un instant avant
de retomber sur les bons. Les noms des potards sont courts (FREQ, HARMO, T.ENV, FDBK, RATCH…) pour qu'ils soient
écrits plus gros et que l'écran respire ; sur les pages des quatre pistes (niveaux, pans, envois), chaque potard
porte le numéro de sa piste dans sa couleur.

| Contrôle | Effet |
| --- | --- |
| **KNOB 1 à 4** | les 4 paramètres de la page affichée |
| **SELECT** | l'autre face du module (PLAITS 1/3, 2/3, 3/3…), en glissant |
| **ALGORITHM** | le modèle de Plaits, le mode de rampe de Tides ; sur Warps, d'un repère au suivant ; sur le delay, son temps |
| **PRESETS** | la piste active, de 1 à 4 |
| **EDIT** | Plaits de la piste active (appuis suivants : ses trois faces, puis GRIT) |
| **LFO** | Tides A, puis Tides B |
| **SEQ** | les pas de la piste (NOTE, PROB, RATCH, LENGTH ; puis RANDOM, puis EUCLID) ou de l'effet (DECAY, PROB, DEPTH, LENGTH) |
| **ARP** | Endless, celui de la piste (voir plus bas) |
| **HOME** | le mixer : les niveaux des 4 pistes, leurs pans, leurs filtres (EQ), le DUCK, puis le bus master (COMP, OTT) ; ALGORITHM sur COMP ou OTT : le mode du bus (OFF, COMP, OTT) |
| **FX** | les effets l'un après l'autre : le delay, la réverbe, Warps ; SELECT parcourt leurs faces (leurs potards, les envois des pistes, leurs pas) |
| **ENV** | l'enveloppe de la piste (PLAITS 2/3 : DECAY, LPG COLOUR, TIMBRE ENV, MORPH ENV) |
| **PLAY** | le séquenceur tourne ou s'arrête |
| **REC** | **KEY** ↔ **STEP** (voir plus bas) ; sur la page d'Endless, l'enregistrement |
| **clavier** | en KEY, joue la piste active, même à l'arrêt ; **OCT− / OCT+** pour l'octave (en STEP : la page de pas) |
| **SEL** | le mode MOD (voir plus bas) ; **tenu** : la matrice de la piste |
| **SAVE tenu** | sauvegarde le projet (à l'arrêt) |
| **GLO** | TEMPO, SCALE, ROOT, SWING ; SELECT : le projet (INIT) et la machine ; GLO encore : retour |
| **GLO tenu + potard** | **ALL** : le même réglage sur les 4 pistes (ex. DECAY) ; GLO tenu + ALGORITHM : le même modèle. Tant que GLO est tenu, l'écran passe en noir sur la couleur du thème. Les réglages restent ; pendant 4 s (cinq carrés jaunes en bas), **GLO** les remet comme avant ALL au début de la mesure suivante (GLO encore avant la mesure : ils restent) |
| **ENV tenu + potard** | **RND** : l'aléatoire de ce réglage (avec GLO tenu aussi : sur les 4 pistes) |
| **REC tenu** (pas) | efface la piste de pas affichée : ses trigs, ses locks, ses aléatoires (et les câbles d'une piste) ; avec **GLO tenu** : toutes, et les câbles globaux |

Les appuis tenus (SAVE, REC pour effacer ou charger, SEL pour la matrice) affichent en bas ce qu'ils vont faire et
cinq carrés qui se vident un à un : lâcher avant le dernier annule. Ceux qui effacent ou écrasent durent une seconde.

La charge de calcul est toujours affichée en haut à droite (le cœur le plus chargé, en %) ; GLO puis SELECT
détaille ce que coûte chaque piste, Warps et les effets (FX : le mixer, le delay, la réverbe et le bus master). Les
quatre voix, Warps et les effets se répartissent entre les deux cœurs selon ce que chacun coûte, mesuré bloc après
bloc (une voix MODAL ou STRING coûte dix fois une VIRTUAL ANALOG) : le mixer, les effets et le bus master jouent pour
cela un bloc derrière les voix (2,9 ms de latence en sortie), pendant que les voix jouent le bloc suivant. Une voix silencieuse depuis un moment, sa note finie, se repose jusqu'à son
trig suivant (elle ne coûte plus rien).

**Le mode STEP** (REC) : les 16 touches blanches sont les pas de la page (allumés : un trig, celui qui joue
clignote), les **7 premières touches noires choisissent la piste de pas** : 1 à 4 les pistes, 5 le delay, 6 la
réverbe, 7 Warps (l'écran passe sur la piste ou l'effet choisi) ; OCT− et OCT+ changent de page (16 pas chacune,
jusqu'à 64). Appuyer sur un pas pose un trig ; appuyer sur un trig l'enlève (avec ses locks). **Tenir un pas et
tourner un potard pose un P-lock** sur ce pas : pour une piste, sur n'importe quelle page qui est la sienne (Plaits,
son niveau, ses envois, son pan, sa note…) ; pour un effet, sur ses potards. Le potard montre alors la valeur du pas
(LOCK dessous). Tenir un pas et taper SEL efface ses locks. La piste de pas suit la page : aller sur le delay (FX)
montre les pas du delay. **REC** à nouveau : retour au mode KEY.

**Les pas d'une piste** (SEQ) : **NOTE**, **PROB** (probabilité), **RATCH** (les ratchets : 1 à 8 coups dans le pas) et
**LENGTH** (le nombre de pas) ; les trois premiers se lockent aussi pas par pas. PLAITS 3/3 donne le **P.ENV**
(l'enveloppe interne de Plaits sur la hauteur, pour des toms, des kicks qui plongent, des bulles), la **GATE** de la
note (5 à 95 % du pas, lockable : des notes courtes et longues dans le même pattern), la transposition et l'octave.

**Les pas d'un effet** (SEQ sur un effet, ou sa dernière face) : **DECAY** (10 ms à 5 s), **PROB**, **DEPTH** et
**LENGTH**. Sans aucun trig, l'effet est toujours là (mode hold : en bas, la rangée des pas est pleine, de la
couleur de l'effet, HOLD écrit dedans) ; dès qu'il y a des trigs, chaque trig ouvre l'effet
et il se referme avec DECAY jusqu'à 1 − DEPTH (DEPTH 100 % : fermé entre les trigs). Pour le delay et la réverbe,
c'est l'entrée qui s'ouvre (les répétitions et la queue continuent) : des échos jetés sur certaines notes seulement ;
pour Warps, c'est un fondu entre la piste telle quelle et la sortie de Warps. Ses P-locks changent ses réglages pas par
pas : le temps du delay (avec la glissade de bande), son feedback, l'algorithme et le TIMBRE de Warps, la taille de la
réverbe…

**Le delay** (FX) : **TIME** en divisions du tempo (1/32 à 1/1, triolets et pointées), **FEEDBACK** (au bout, il peut
s'emballer, le drive le retient), **FILTER** (à gauche passe-bas, à droite passe-haut, au milieu rien), **DRIVE** ;
puis **RESONANCE**, **MODE** (Mono ou Ping-pong : gauche au temps, droite au double), **MOD** (un pleurage lent) et
**RETURN**. Le filtre est celui des modules Mutable (le SVF de stmlib) et la saturation sa SoftClip, dans une ligne
de 1,2 s. Quand le temps change, la lecture glisse jusqu'au nouveau, comme une bande.

**L'EQ** (HOME, troisième face) : un potard par piste pour lui faire de la place dans le mix. Au milieu, rien
(Flat) ; à gauche, un passe-bas qui descend de 18 kHz à 125 Hz (on enlève les aigus) ; à droite, un passe-haut qui
monte de 20 Hz à 8 kHz (on enlève les graves), en 24 dB/octave (deux SVF de stmlib, Butterworth), une octave par
pas de potard sur toute la course : un peu de potard enlève un peu, au bout c'est un mur. Le filtre est avant le
duck et les envois (ce qui part au delay et à la réverbe est filtré aussi) ; il se locke pas par pas, il a son
aléatoire (ENV) et Tides peut le moduler. L'écran trace les quatre courbes, chacune dans la couleur de sa piste, la
piste active par-dessus.

**Le duck** (HOME, quatrième face) : DECAY (20 ms à 2,6 s), puis la profondeur de T2, T3 et T4. À chaque trig de
T1 (un pas, un ratchet, une touche), ces pistes baissent d'autant et remontent avec DECAY. Le duck agit sur la piste
avant ses envois : ses échos et sa réverbe partent baissés avec elle (les queues déjà dans les effets continuent).
Sa profondeur se locke pas par pas, comme le reste de la piste.

**Le bus master** (HOME, cinquième et sixième faces) : ALGORITHM choisit le mode, OFF, **COMP** ou **OTT** ; les
potards de la face qui ne joue pas sont grisés. COMP : THRESH (0 à −36 dB), RATIO (1:1 à 20:1), RELEASE (20 ms à 1,3 s),
GAIN (±12 dB) ; à droite, la réduction de gain. OTT : DEPTH (le mélange avec le son direct), TIME (les temps d'attaque
et de release, ×1 au milieu, de ×1/4 à ×4), DRIVE (l'entrée) et LEVEL (la sortie) ; à droite, ses trois bandes,
montées (rose) ou écrasées (bleu). Après les deux, toujours, le limiteur de stmlib.

Sur les pages des quatre pistes (niveaux, pans, EQ, envois, duck), le nom de la piste active est en défonce. Et en
haut à droite, toujours au même endroit, quatre carrés numérotés : celui de la piste que joue le clavier est rempli
de sa couleur (en mode STEP, la piste de pas éditée ; sur une piste d'effet, son nom à la place des carrés).

**Le mode MOD** (SEL) : le clavier devient une rangée de sources. On tient une touche et on tourne un potard : le
câble est posé, le sens et la quantité de rotation fixent la profondeur ; revenir à 0 le retire. Touches 1-4 :
Tides A1-A4 ; 5-8 : Tides B1-B4 ; 9 et 10 : Tides A ou B en éventail (A n sur le potard n). Les touches des sources
qui modulent la page s'allument.

**La matrice** (SEL tenu) : les 8 câbles de la piste puis les 8 globaux. K1 choisit le câble, K2 sa source, K3 sa
cible, K4 sa profondeur.

**Tides** : TRIGGER choisit ce qui le déclenche : Free, les trigs d'une piste (T1 à T4 : une enveloppe AD à chaque
note de la basse, par exemple) ou Clock (calé sur le tempo, FREQUENCY devient un rapport : 4 par temps à 1 pour 16
mesures).

**PLAITS 1/3** : FREQ, puis HARMONICS, TIMBRE et MORPH sous le nom (court) que leur donne le manuel de Plaits pour
le modèle choisi (le KICK : ATTACK, BRIGHT, DECAY ; le FM 2-OP : RATIO, INDEX, FDBK ; le WAVETABLE : BANK, ROW,
COLUMN…). **PLAITS 2/3** (ENV) : l'enveloppe. Les modèles qui ont la leur (STRING, MODAL, KICK, SNARE, HI-HAT, et les
6-OP FM dans leur preset) contournent le LPG de Plaits : LPG COLOUR est alors grisé, et pour STRING, MODAL et les trois
batteries, DECAY s'ajoute à leur decay (leur MORPH) : il marche sur toutes les pistes, avec ses locks et son
aléatoire. Un potard grisé tourne toujours, mais ne change rien au son dans l'état présent.

**Le grit** (EDIT, quatrième face de Plaits) : ce qui salit la voix de la piste, avant Warps et le mixer.
**DRIVE** monte le gain des petits signaux jusqu'à ×25 (la pleine échelle reste la pleine échelle) à travers une
**SHAPE** : Soft (une saturation douce), Hard (écrêtée) ou Fold (ce qui dépasse repart dans l'autre sens, comme un
wavefolder). **RATE** garde chaque échantillon un moment (un sample & hold : de 44,1 kHz jusqu'à 689 Hz, le repliement
voulu) et **BITS** arrondit les échantillons (de 16 bits à 2 : à fond, une voix discrète disparaît dans le premier
pas). Chacun n'agit que relevé ; le tout se locke pas par pas, a son aléatoire et Tides peut le moduler. L'écran
montre ce que les réglages font d'une sinusoïde.

**Les rythmes euclidiens** (SEQ, troisième face ; sur les effets, la dernière face) : **FILLS** coups répartis aussi
régulièrement que possible sur la longueur de la piste, **ROTATE** les décale ; LENGTH et PROB sont ceux de la piste
de pas. Tourner FILLS, ROTATE ou LENGTH **réécrit les trigs** de la piste (ses locks restent) ; on peut ensuite les
retoucher à la main. L'écran montre les pas en cercle, les trigs allumés, celui qui joue entouré.

**L'aléatoire** : la page RANDOM (SEQ, puis SELECT) a quatre potards, PAN, DECAY, TUNE et RATCHET. Tourné à droite,
une nouvelle valeur au hasard à chaque trig ; à gauche, des valeurs au hasard mais qui reviennent les mêmes à chaque
tour (une boucle) ; plus on tourne, plus l'écart est grand (100 : toute la course ; TUNE : une octave de part et
d'autre ; RATCHET : jusqu'à 7 coups de plus). Au centre, rien : le réglage retrouve exactement sa valeur. **ENV tenu
+ un potard** fait la même chose sur n'importe quel réglage d'une piste ou d'un effet.

**Le quantiseur et le swing** (GLO) : TEMPO, SCALE et ROOT (chaque note, des pas, de l'aléatoire ou du clavier, va
à la note de la gamme la plus proche : Off, Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Harmonic et
Melodic minor, Major et Minor pentatonic, Blues, Whole tone, Hirajoshi), SWING (50 % : droit ; 75 % : les
contretemps retardés d'une demi-double croche).

**Endless** (ARP, comme sur l'OP-1) : sur sa page, **REC** lance l'enregistrement (la frise passe en REC, le bouton
clignote) ; chaque touche ajoute une note (on l'entend), **SEL** ajoute un silence, jusqu'à 64 pas. **REC** à
nouveau : la piste joue ces notes en boucle sur le tempo, à la place de ses pas. Pendant qu'elle joue, **une
touche transpose** la boucle depuis sa première note (la transposition reste). Ses potards : RATE (1/32 à 1/4, avec
les triolets), GATE (la longueur des notes), DIRECTION (avant, arrière, aller-retour, au hasard), TRANSPOSE. **REC
tenu** efface la boucle (la piste retrouve ses pas). Probabilité, ratchets, aléatoire et quantiseur s'appliquent aussi
à ses notes. En bas, ses notes en barres, la note jouée en blanc.

**Warps** (FX, troisième effet) : le module de modulation croisée de Mutable, entre deux pistes. Sa deuxième page
(SELECT) le branche : **CARRIER**, la porteuse (Off : Warps éteint ; une piste T1-T4 ; ou son oscillateur interne,
trois formes), **MOD**, la piste modulante, **DRY** (On : on entend aussi la piste modulante telle quelle ;
Off, elle ne passe que par Warps) et **MODE** (Warps, ou Freq shift : l'easter egg du module, un décaleur de
fréquence). La sortie de Warps prend la place de la piste porteuse (ou de la modulante avec l'oscillateur), avec son
niveau, son pan et ses envois. Sa première page, ce sont les potards du module : **ALGORITHM** balaie ses six
modulations croisées (crossfade, cross-folding, anneau à diodes, anneau numérique, XOR, comparateur), en fondu de
l'une à l'autre comme sur le module ; le sélecteur ALGORITHM saute d'un repère au suivant. **TIMBRE**, **LEVEL 1** et
**LEVEL 2** comme sur le module : avec l'oscillateur interne, LEVEL 1 règle sa note (C0 à C5). En Freq shift :
ALGORITHM fixe le décalage (au centre, rien ; jusqu'à ±2 kHz), TIMBRE la bande, LEVEL 1 la réinjection, LEVEL 2 le
mélange. Sa piste de pas locke ALGORITHM, TIMBRE, LEVEL 1 et LEVEL 2 pas par pas (pas la porteuse ni la modulante)
et l'ouvre ou la ferme sur la piste. Warps joue un bloc derrière les voix (~4 ms de latence sur la piste qu'il
remplace).

**La sortie** (GLO, SELECT, K2) : Stereo, Mic headset ou Mono. **Mic headset** est pour un casque à micro (fiche à
4 contacts) branché à fond dans la prise du FM-1 (3 contacts) : ses écouteurs se retrouvent en série entre la
gauche et la droite et ne reçoivent que leur différence (le centre disparaît, ce qui est panoramiqué ressort). Le
canal droit inversé, ils reçoivent tout le mix, en mono (une oreille en opposition de phase : le son paraît un peu
« dans la tête »). Pour la stéréo, un casque sans micro ou un adaptateur casque/micro. Le réglage reste quand on
charge un autre projet.

**Les couleurs** (GLO, SELECT, K4) : sept thèmes, chacun un fond sombre et trois couleurs pour l'état des modules
(celles que les icônes et les anneaux prennent) et ALL : **Acid** (citron vert, violet, rouge orangé), **Ember**
(or, orange, cramoisi sur un noir chaud), **Neon** (menthe, lavande, rose sur indigo), **Ice** (cyan, pervenche,
magenta), **Rust** (vert-de-gris, rouille, jaune danger sur graphite), **Mono** (blanc, argent, rouge) et
**Mutable** (le teal, l'orange et le rose des modules, ceux des versions 0.1 à 0.3.6). Comme la sortie et la
qualité de Warps, le thème reste quand on charge un autre projet ; il est gardé avec le projet sauvegardé.

**Le clavier** relance la note à chaque touche, même jouée legato, et une touche tapée très vite sonne quand même.

Un projet sauvegardé avec les 0.1.4 à 0.2.6 se recharge : ses réglages, ses modèles, ses câbles (sauf ceux qui
partaient de Marbles), ses pas, ses locks et Endless ; le tempo de Marbles devient le tempo, et les effets nouveaux
démarrent neutres (rien vers le delay, aucune piste de pas d'effet). Ceux des 0.3 aussi (l'EQ y est à plat, le grit
coupé).

## Le dépôt

- `firmware/` : le firmware (fm1-base `tools/build.py ../firmware`)
  - `app.c` : l'écran et les contrôles ; `mi/engine.cc` : le son (les pistes, les pas et leurs trigs, Tides, la
    matrice, le quantiseur, les effets et leurs pistes de pas, le mixer ; les voix et Warps partagés entre les deux
    cœurs) ; `mi/mi_*.cc` : un pont C par module Mutable ; `mi/mi_reverb.h` : la réverbe de Clouds, ses lignes de
    retard en flottants ; `mi/mi_delay.h` : le delay (le SVF et la saturation de stmlib dans une ligne 16 bits)
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
