# MIFM1

Une groovebox 4 pistes pour le M-VAVE FM-1 dont tout le son vient de code open source, celui de Mutable Instruments
et les voix de batterie du firmware de l'Erica Synths LXR-02, sur la base de
[fm1-base](https://github.com/charlesvestal/fm1-base) :

- un **filtre** par piste, quatre modèles d'après les meilleurs modèles virtuels analogiques ouverts : **LADDER**
  (l'échelle Moog), **MS-20** (Korg 35, la résonance qui drive), **SEM** (Oberheim) et **VOX** (des formants), en LP,
  BP ou HP, avec du bruit coloré dedans, une enveloppe à vactrol, un **ping** et une **FM** par sa propre sortie ;
- **Plaits** sur chaque piste, ses 24 modèles au complet, et un **circuit bending** par piste (0.7, à la place du
  grit) : ses **bits** à la manière du Biscuit d'OTO (une horloge calée sur la note, la résolution, les huit bits du mot
  allumés, inversés ou coupés, des formes numériques) et son **glitch** à la manière du Data Bender de Qu-Bit (des
  répétitions calées au tempo, ralenties ou accélérées, des accidents) ;
- **quatre batteries FM** comme modèles de piste en plus : les voix du **LXR-02** (son firmware est open source,
  GPL-3.0), grosse caisse et toms, caisse claire, cymbale et charleston, leur code tel quel (voir plus bas) ;
- un **séquenceur pas à pas** à la Elektron, 64 pas par piste, avec P-locks, et deux modes de jeu : **KEY** (le
  clavier joue la piste) et **STEP** (le clavier est la grille, les touches noires choisissent la piste ou l'effet) ;
- **Endless** sur chaque piste, comme sur l'OP-1 : on enregistre des notes et des silences un par un, ils tournent
  en boucle, une touche les transpose ;
- sur chaque trig : **probabilité**, **ratchets**, **longueur**, et un **aléatoire** sur n'importe quel réglage de la
  piste (à droite nouveau à chaque trig, à gauche en boucle) ; un **swing** global ; des **rythmes euclidiens** sur
  chaque piste de pas ;
- un **quantiseur global** (15 gammes et la tonique) : aucune fausse note, sur toutes les pistes et au clavier, FREQ,
  la transposition et l'oscillateur de Warps compris ;
- **trois LFO** comme modulateurs (sinus, triangle, dent de scie, rampe, carré, S&H, aléatoire lissé), chacun libre,
  calé sur le tempo ou relancé par les trigs d'une piste, avec un **slew** ;
- une **matrice de câbles** : les 3 LFO vers presque tous les paramètres, 8 câbles par piste et 8 globaux, posés en
  tenant la touche d'un LFO et en tournant un potard (en atténuverteur) ;
- une **dose d'aléatoire** cachée (OCT− tenu, SELECT), sur son propre écran, qui relance tout le son à chaque trig
  de chaque piste (hauteurs toujours dans la gamme, timbres, bits et glitch, filtres, pans, un envoi d'effet de temps en
  temps), discrète au début de la course et folle au bout, le rend tel quel à 0, et dans son dernier quart ajoute des
  **fills** en fin de mesure ;
- **trois effets, chacun avec sa propre piste de pas** : un **delay** inspiré des potards du Zen Delay (temps en
  divisions du tempo ou libre en millisecondes, feedback, filtre passe-bas / passe-haut avec résonance, drive, mono ou ping-pong, une légère
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

## Dans le navigateur

<https://distortedimages-code.github.io/MIFM1/emu/> : **le firmware tel quel, compilé pour le navigateur**
(ordinateur, téléphone, tablette), avec le panneau du FM-1 dessiné autour de son écran. Le même code que sur la
machine (le même moteur, les mêmes modules, les mêmes calculs en flottants simples), le même écran, les mêmes
pages ; « Allumer et lancer la démo » démarre le son et PLAY. Autour de la machine, un mini guide : la prise en main
en quatre gestes, le chemin du son (un schéma), le panneau (chaque numéro allume son repère sur la machine ;
« Repères » les montre tous), les 28 moteurs avec les icônes de l'écran (`tools/web/icons_svg.py` les reprend d'`app.c`)
et ce qu'ils modélisent, les quatre filtres avec leur réponse, le schéma de leur circuit et leur histoire, le circuit bending,
les effets, le séquenceur, la dose d'aléatoire, les spécifications, et des jauges de la charge du FM-1. « Essayer »,
sur un moteur ou un filtre, le charge sur la piste jouée (`firmware/web.c` web_try). Pour écouter MIFM1 sur un autre système (une
voiture, des enceintes de studio) ou montrer la machine sans elle. Les potards se tournent en glissant vers le haut
ou le bas ; sur un ordinateur, des raccourcis clavier jouent les touches. SAVE tenu garde le projet dans le
navigateur. L'écran du téléphone reste allumé tant que la page joue ; sur iPhone, le son passe même en mode
silencieux.

**La charge affichée est celle du FM-1, estimée.** Un navigateur ne peut pas mesurer le temps que le FM-1 mettrait (son
horloge avance d'un bloc à la fois, et sa vitesse n'est pas celle du FM-1) : dans cette version, l'horloge du moteur
est un modèle, où chaque partie avance celle du cœur qui la joue de ce qu'elle coûte au FM-1, et le second cœur est
simulé avec la sienne. L'en-tête, la page machine (GLO, SELECT), le CPU des filtres et la répartition entre les deux
cœurs lisent donc des temps de FM-1, et une ligne sous le panneau les résume. Le coût de chaque partie
(`firmware/mi/web_costs.h`) vient des **instructions** qu'elle exécute, comptées par valgrind (`tools/web/calibrate.py`) :
chaque modèle de Plaits et chaque batterie FM seuls, chaque filtre (avec et sans FM, avec et sans bruit), les bits,
Warps (ses algorithmes, Eco et HQ), les effets avec chaque mode du bus, l'EQ, le duck, la préparation (séquenceur,
LFO, câbles). Des instructions plutôt que des temps : ceux d'un ordinateur ne disent rien de ceux du FM-1 (le stress
test de 0.5.8, mesuré sur les deux : de 9 à 89 fois plus lent selon la piste, l'ordinateur vectorisant certaines
boucles et s'effondrant sur les nombres sous-normaux), alors que le temps du FM-1 suit le nombre d'instructions :
6,75 µs pour mille dans les voix, 6,8 dans Warps, 5,4 dans les effets, 17 dans la préparation (son code est plein de
branches et dispersé : le cache du FM-1 le rate plus souvent). Le stress test retombe ainsi sur ses chiffres du FM-1
(cœurs 78 et 77 %, pic 87 % ; sur la machine 79, 77 et 91) ; ailleurs, compter ±20 % sur une piste : le modèle ne
connaît pas la taille de code d'un modèle (qui passe ou non dans le cache) ni ses réglages (le nombre de voix d'un
CHORD, la queue d'un STRING).

La construire : `make web` (avec `pip install ziglang` ; dans `firmware/build/emu/`, à servir en HTTP) :
`tools/web/emcc` passe les commandes d'Emscripten que fm1-base donne au clang de Zig
(wasm32-wasi), et la page de MIFM1 (`firmware/web/emu/`, en français) remplace celle de fm1-base. GitHub la publie à
chaque push dans `emu/`. Après un changement qui pèse sur le CPU (un module, un filtre), `python3
tools/web/calibrate.py` recompte et réécrit la table.

## Enregistrer dans un DAW (USB)

Branché en USB, le FM-1 est aussi une **carte son multipiste** (class compliant : aucun pilote, sur Mac, Windows,
Linux, iPad), nommée « MIFM1 FM-1 » : **10 entrées**, 16 bits à 44,1 kHz, une paire stéréo par piste et une pour les
effets.

| Entrées | Ce qu'elles reçoivent |
| --- | --- |
| 1-2 | T1 |
| 3-4 | T2 |
| 5-6 | T3 |
| 7-8 | T4 |
| 9-10 | le retour des effets : delay et réverbe |

Chaque piste sort après son niveau, son pan, son EQ et le duck, avec son circuit bending et Warps (quand Warps prend la place
d'une piste, c'est lui qu'on entend sur elle), mais sans delay ni réverbe : ce sont des bus partagés, leur retour
arrive sur 9-10. **La somme des 10 entrées, c'est le master avant son bus** (compresseur ou OTT, limiteur), à refaire
dans le DAW si on le veut ; le master avec son bus n'est pas sur l'USB (la sortie casque l'a).

Dans le DAW : 5 pistes stéréo, une par paire d'entrées, armées ensemble ; le MIDI passe par le même câble. Le
niveau ne dépend pas de MASTER (le potard ne règle que le haut-parleur et le casque : on peut couper le FM-1 et
écouter par le DAW). Projet en 44,1 kHz (le FM-1 ne propose que ce taux). Latence : environ 7 ms. Sur Windows, les
DAW en ASIO passent par FlexASIO ou ASIO4ALL (ou leur mode WASAPI). Le sens inverse (l'ordinateur qui joue dans le
FM-1) n'est pas activé.

## Jouer

**À l'allumage**, l'écran reste noir pendant que la machine démarre, puis le nom MIFM1 s'écrit en Syne (ses lettres
passent par des signes au hasard dans les couleurs du thème avant de se poser, de gauche à droite), un trait de la
troisième couleur se tire dessous, puis « 4-TRACK SYNTHESIS MACHINE » et la version : 1,8 s, qu'un bouton passe.

Un FM-1 sans projet sauvegardé joue **la démo** (PLAY), un groove : piste 1 un kick sur les temps (
un double à la fin de la mesure) qui fait respirer (le duck) une basse Moog en piste 2, VIRTUAL ANALOG dans le filtre
LADDER (son enveloppe à chaque note, un LFO triangle d'une mesure sur la coupure), qui roule sur les contretemps avec
un riff de locks et un ratchet de temps en temps ; piste 3 le charleston FM sur toutes les doubles croches (decay et
pan tirés à chaque coup, ouvert sur les contretemps, 10 % de coups laissés) ; piste 4 des accords dub, CHORD dans le
filtre SEM (un S&H sur leur voicing, un hasard lisse sur la coupure, un souffle de bruit), jetés dans un delay
ping-pong à la croche pointée et dans la réverbe ; un compresseur doux sur le bus. Fa mineur, 124 BPM. Elle est
légère exprès : environ 45 % de chaque cœur (pic vers 60 %), de la place pour ce qu'on y ajoute (Warps en prend 20).

**Repartir de zéro (INIT)** : GLO, SELECT (la page PROJECT), puis **K1** choisit le projet et **REC tenu** le
charge : *New* (4 pistes Plaits sur VIRTUAL ANALOG, tous les potards à midi, aucun pas, aucun câble, rien vers les
effets, do majeur), *The demo groove* (la démo ci-dessus) ou *The saved project* (le dernier sauvegardé, pour
annuler), *Stress test* ou *Stress test, on the 4 FM drums*. Le transport s'arrête ; rien n'est sauvegardé tant que SAVE n'est pas tenu.

**Le stress test** (GLO, SELECT, K1 sur *Stress test*, REC tenu) : un patch fixe, le même à chaque fois, qui démarre
tout seul et laisse sous les yeux la page machine (la charge des cœurs, le pic, ce que coûte chaque piste) : pour
comparer les versions entre elles. Une grosse caisse et une basse VA VCF passées aux bits (et la piste 4 au glitch), des accords (CHORD) et un
résonateur MODAL, chacun dans un des quatre filtres (depuis 0.5.8 : LADDER, MS-20, SEM avec sa FM, VOX pingé ; les
chiffres montent donc par rapport aux versions d'avant), Warps (anneau à diodes, T3 par T2), OTT sur le bus, le delay et la réverbe nourris, le duck, les
filtres, les trois LFO sur 20 câbles, des aléatoires, des locks et des ratchets. La sortie, la qualité de Warps (Eco
ou HQ) et le thème restent ceux de la machine : comparer avec le même réglage de Warps. *Stress test, on the 4 FM
drums* (le cinquième choix) joue le même patch, les mêmes câbles et les mêmes effets, mais les quatre pistes sur les
quatre batteries FM (chacune sur son kit, à sa hauteur) : ce que coûtent les voix du LXR-02 à côté de celles de
Plaits, piste par piste sur la page machine. Sur l'ordinateur, il prend un quart du temps du stress test.

**Les batteries FM** (ALGORITHM après HI-HAT, la banque FM DRUMS) : **FM DRUM** (la voix grosse caisse et toms du
LXR-02 : un oscillateur et un modulateur en FM, une enveloppe de hauteur), **FM SNARE** (un oscillateur et un bruit
filtré), **FM CYMBAL** et **FM HI-HAT** (un oscillateur modulé en FM par deux autres). Choisir l'une d'elles sur une
piste met ses huit réglages à ceux de son kit (le kit INIT du LXR-02, retouché pour sonner tout de suite), sur les
potards qui suivent FREQ, sur les trois pages de leur son (EDIT, puis SELECT) : les trois de la première, les quatre
de la deuxième, et le premier de la troisième (à la place de GATE). ENV n'a alors qu'une page : l'enveloppe du
filtre (F.ENV, F.ATK, F.DECAY, PING). Ils sont écrits de 0 à 127 comme sur le LXR-02, WAVE par son nom, et chaque cran
de potard passe à la valeur suivante (WAVE : à la forme d'onde suivante) :

| | 1/3 : 2 | 1/3 : 3 | 1/3 : 4 | 2/3 : 1 | 2/3 : 2 | 2/3 : 3 | 2/3 : 4 | 3/3 : 1 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **FM DRUM** | FM (l'index) | MODFRQ (le modulateur) | P.DEC (l'enveloppe de hauteur) | DECAY | WAVE | CLICK (le snap) | DRIVE | P.AMT (la profondeur de l'enveloppe de hauteur) |
| **FM SNARE** | NOISE (le mélange) | NOISF (la fréquence du bruit) | P.DEC | DECAY | FILTER (sur le bruit) | RESO | DRIVE | P.AMT |
| **FM CYMBAL** | MOD 1 | FRQ 1 | MOD 2 | DECAY | FILTER (passe-haut) | FRQ 2 | WAVE (l'oscillateur) | DRIVE |
| **FM HI-HAT** | MOD 1 | FRQ 1 | MOD 2 | DECAY | FILTER (passe-haut) | FRQ 2 | WAVE (l'oscillateur) | DRIVE |

Les autres réglages des voix restent ceux du kit. FREQ, la note, TRANSP et OCTAVE déplacent leurs oscillateurs comme
une note MIDI le fait sur le LXR-02 (un pas en do 3, octave 0, joue la hauteur du kit ; ce qui dépasse le demi-ton va
sur leur accord fin). Choisir une batterie FM remet l'OCTAVE de la piste à 0 (une piste de basse à −1 la jouerait une
octave sous son kit). Un trig relance la voix (à pleine vélocité), son enveloppe fait le reste : GATE n'y change
rien. Le charleston ouvert est un DECAY plus long : un lock sur DECAY. Les câbles, les locks, les aléatoires, la dose
et le circuit bending marchent sur elles comme sur Plaits. Leur code est celui du LXR-02, compilé tel quel (ce qui change pour le
FM-1 est listé dans `firmware/third_party/lxr/MIFM1.md`) : le LXR-02 calcule une partie en double précision, que le
FM-1 n'a pas ; leur enveloppe d'amplitude garde exactement la valeur du double (vérifiée pas à pas), le reste passe
en float, et `make lxr-check` compare les deux : 99,99 % des échantillons identiques à ceux du LXR-02, les autres à
quelques pas de 16 bits (-111 dB).

L'écran est noir, ses lumières aux couleurs de Mutable Instruments (bleu-vert, orange, rose, celles de leurs
icônes). En haut, l'icône du module dans un anneau de sa couleur, son nom en grand et une ligne sur son état. Puis
les quatre potards côte à côte, dans l'ordre des KNOB 1 à 4 : un anneau fin allumé de son début (ou de son milieu,
avec « − + ») jusqu'à la valeur, la valeur dedans, un câble en pointillé et son point d'arrivée dans l'autre couleur,
la source écrite dessous. En bas les quatre pistes, une LED chacune allumée par sa gate dans la couleur de son modèle,
celle qu'on joue en défonce (chaque case sous son potard, le modèle en 4 lettres : KICK, VA, 6OP1, WTBL…) ; sur les pages des LFO, les trois
LFO, allumés à leur hauteur ; en mode STEP, sur les pages des pas et sur celles
des effets, les 16 pas de la piste (allumés : un trig ; un point : des locks ; cerclé : celui qui joue ; pour un effet,
à droite, à quel point il est ouvert). Quand on change de face dans un même module (SELECT), la nouvelle page glisse
depuis le côté vers lequel on tourne : les potards de la page suivante poussent les autres, l'icône du module
reste, et les noms (le module, le titre, la ligne dessous) se réécrivent, leurs lettres au hasard un instant avant
de retomber sur les bons. Les noms des potards sont courts (FREQ, HARMO, T.ENV, FDBK, RATCH…) pour qu'ils soient
écrits plus gros et que l'écran respire ; sur les pages des quatre pistes (niveaux, pans, envois), chaque potard
porte le numéro de sa piste dans sa couleur.

En haut, à côté du nom du module, deux repères disent ce que font les deux encodeurs sur la page : **les pages du
module**, une LED chacune (celle qu'on regarde allumée) entre deux petites flèches, quand il y en a plus d'une
(SELECT les parcourt) ; et un **A cerclé** quand **ALGORITHM** change quelque chose ici : le modèle de la piste sur son
son, le modèle du filtre sur son filtre, le mode du delay, du bus master, les algorithmes de Warps. Pas de A sur le
circuit bending ni sur les enveloppes : ALGORITHM n'y fait rien. Les potards d'un filtre OFF sont grisés.

| Contrôle | Effet |
| --- | --- |
| **KNOB 1 à 4** | les 4 paramètres de la page affichée |
| **SELECT** | les pages de ce qu'on regarde (les deux du son de la piste, les deux de son filtre, les faces d'un effet…), en glissant |
| **ALGORITHM** | le type de ce qu'on a devant soi, là où il y a un choix (le A cerclé en haut) : sur le son de la piste, son modèle (les 24 de Plaits, puis les 4 batteries FM) ; sur son filtre, le modèle de filtre (OFF, LADDER, MS-20, SEM, VOX) ; sur Warps, d'un repère d'ALGO au suivant ; sur le delay, SYNC (vers la gauche) ou FREE (vers la droite) |
| **PRESETS** | la piste active, de 1 à 4 |
| **EDIT** | les trois ensembles de la piste active, l'un après l'autre : son **SON** (FREQ, HARMO, TIMBRE, MORPH ; SELECT : GATE, TRANSP, OCTAVE, F.KEY), son **BEND** (les bits : CLOCK, BITS, MASK, SHAPE ; SELECT : le glitch, GLITCH, SIZE, BEND, DROP), son **FILTRE** (TYPE, CUTOFF, RESO, ENV ; SELECT : DRIVE, NOISE, COLOR, FM), puis de nouveau le son |
| **LFO** | les trois LFO, l'un après l'autre (SELECT aussi) |
| **SEQ** | les pas de la piste (NOTE, PROB, RATCH, LENGTH ; puis RANDOM, puis EUCLID) ou de l'effet (DECAY, PROB, DEPTH, LENGTH) |
| **ARP** | Endless, celui de la piste (voir plus bas) |
| **HOME** | le mixer : les niveaux des 4 pistes, leurs pans, leurs filtres (EQ), le DUCK, puis le bus master (COMP, OTT) ; ALGORITHM sur COMP ou OTT : le mode du bus (OFF, COMP, OTT) |
| **FX** | les effets l'un après l'autre : le delay, la réverbe, Warps ; SELECT parcourt leurs faces (leurs potards, les envois des pistes, leurs pas) |
| **ENV** | tout ce qui est enveloppe sur la piste active : l'enveloppe de Plaits (DECAY, LPG COLOUR, TIMBRE ENV, MORPH ENV) ; ENV encore (ou SELECT) : P.ENV et l'enveloppe du filtre (F.ATK, F.DECAY, PING) |
| **PLAY** | le séquenceur tourne ou s'arrête ; **deux fois de suite** (STOP STOP, en moins de 0,4 s) : arrêté, et tout le son coupé, les queues du delay et de la réverbe comprises |
| **REC** | **KEY** ↔ **STEP** (voir plus bas) ; sur la page d'Endless, l'enregistrement |
| **clavier** | en KEY, joue la piste active, même à l'arrêt, avec sa TRANSP et son OCTAVE ; **OCT− / OCT+** pour l'octave du clavier (en STEP : la page de pas) ; en MOD, les trois premières touches blanches sont les LFO |
| **OCT− tenu + SELECT** | la **dose d'aléatoire**, sur son écran (voir plus bas) |
| **SEL** | le mode MOD (voir plus bas) ; **tenu** : la matrice de la piste |
| **SAVE tenu** | sauvegarde le projet (à l'arrêt) |
| **GLO** | TEMPO, SCALE, ROOT, SWING ; SELECT : le projet (INIT) et la machine ; GLO encore : retour |
| **GLO tenu + potard** | **ALL** : le même réglage sur les 4 pistes (ex. DECAY) ; GLO tenu + ALGORITHM : le même modèle. Tant que GLO est tenu, l'écran passe en noir sur la couleur du thème. Les réglages restent ; pendant 4 s (cinq carrés jaunes en bas), **GLO** les remet comme avant ALL au début de la mesure suivante (GLO encore avant la mesure : ils restent) |
| **ENV tenu + potard** | **RND** : l'aléatoire de ce réglage (avec GLO tenu aussi : sur les 4 pistes) |
| **REC tenu** (pas) | efface la piste de pas affichée : ses trigs, ses locks, ses aléatoires (et les câbles d'une piste) |
| **GLO + REC tenus 3 s** | **ERASE ALL**, depuis n'importe quelle page : tout revient comme dans un projet neuf (INIT) sauf **les quatre sons** (leurs modèles et leurs réglages : le son, l'enveloppe de Plaits, la gate, la hauteur), **le tempo, la gamme et sa tonique** : plus de pas, de locks, d'aléatoires (aucun potard RND), de câbles ni d'Endless ; les niveaux comme neufs, les pans au centre, pas de filtre (ni l'EQ, ni le filtre de piste : OFF), aucun envoi, le circuit bending coupé, les LFO, les effets (delay, réverbe, Warps coupé), le duck et le bus master comme neufs, pas de swing, la dose d'aléatoire à 0. La sortie, la qualité de Warps et le thème restent |
| **REC + SEL tenus 1,5 s** | efface **tous les câbles** (les modulations des LFO, de toutes les pistes et globales) |

Les appuis tenus (SAVE, REC pour effacer ou charger, SEL pour la matrice) affichent en bas ce qu'ils vont faire et
cinq carrés qui se vident un à un : lâcher avant le dernier annule. Ceux qui effacent ou écrasent durent une seconde.

La charge de calcul est toujours affichée en haut à droite (le cœur le plus chargé, en %) ; GLO puis SELECT
détaille ce que coûte chaque piste, Warps et les effets (FX : le mixer, le delay, la réverbe et le bus master). En
bas de cette page, le pire bloc de la dernière seconde (celui du pic) : ce qu'y ont pris les deux cœurs et le rendu
entier, puis chaque piste, Warps et les effets dans ce bloc-là, et la préparation du cœur 0 en détail (le début et les
LFO, les câbles, le séquenceur et ses trigs, les pistes des effets, les réglages des voix) : pour savoir ce qui
fait monter le pic. Les
quatre voix, Warps et les effets se répartissent entre les deux cœurs selon ce que chacun coûte, mesuré bloc après
bloc (une voix MODAL ou STRING coûte dix fois une VIRTUAL ANALOG) : le mixer, les effets et le bus master jouent pour
cela un bloc derrière les voix (2,9 ms de latence en sortie), pendant que les voix jouent le bloc suivant : le cœur 1
les commence dès le début du bloc, pendant que le cœur 0 le prépare (le séquenceur, les LFO, les câbles : « prep »
sur la page machine), puis les deux cœurs se partagent les voix : le cœur 0 propose les siennes au cœur 1 dès que le bloc est prêt, et
le cœur 1 les prend à la fin de son premier travail, sans attendre que le cœur 0 ait fini une de ses voix pour les
lui passer (sur le stress test, cette attente faisait monter le pic de 70 à 96 %). Une voix silencieuse depuis un moment, sa note finie, se repose jusqu'à son
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
**LENGTH** (le nombre de pas) ; les trois premiers se lockent aussi pas par pas. La deuxième page du son (EDIT,
SELECT) donne la **GATE** de la note (5 à 95 % du pas, lockable : des notes courtes et longues dans le même pattern),
la transposition, l'octave et **F.KEY** (le filtre qui suit la note) ; la deuxième page d'ENV le **P.ENV** (l'enveloppe
interne de Plaits sur la hauteur, pour des toms, des kicks qui plongent, des bulles).

**Les pas d'un effet** (SEQ sur un effet, ou sa dernière face) : **DECAY** (10 ms à 5 s), **PROB**, **DEPTH** et
**LENGTH**. Sans aucun trig, l'effet est toujours là (mode hold : en bas, la rangée des pas est pleine, de la
couleur de l'effet, HOLD écrit dedans) ; dès qu'il y a des trigs, chaque trig ouvre l'effet
et il se referme avec DECAY jusqu'à 1 − DEPTH (DEPTH 100 % : fermé entre les trigs). Pour le delay et la réverbe,
c'est l'entrée qui s'ouvre (les répétitions et la queue continuent) : des échos jetés sur certaines notes seulement ;
pour Warps, c'est un fondu entre la piste telle quelle et la sortie de Warps. Ses P-locks changent ses réglages pas par
pas : le temps du delay (avec la glissade de bande), son feedback, l'algorithme et le TIMBRE de Warps, la taille de la
réverbe…

**Le delay** (FX) : **TIME** en divisions du tempo (1/32 à 1/1, triolets et pointées) ; ou, ALGORITHM tourné vers la
droite, **FREE** : TIME en millisecondes (1 ms à 1,2 s), détaché de l'horloge (vers la gauche : SYNC, la division la
plus proche ; le temps est gardé d'un mode à l'autre). **FEEDBACK** (tout au bout,
**Loop** : les répétitions tournent indéfiniment, sans monter ni baisser ; la boucle ne gagne jamais : ce que la
résonance pousserait au-delà de −8 dB est tenu là, les répétitions sonnent en continu au lieu d'exploser), **FILTER** (à gauche passe-bas, à droite passe-haut, au milieu rien), **DRIVE** ;
puis **RESONANCE**, **MODE** (Mono ou Ping-pong : gauche au temps, droite au double), **MOD** (un pleurage lent) et
**RETURN**. Le filtre est celui des modules Mutable (le SVF de stmlib) ; la saturation (DRIVE) est propre jusqu'à mi-niveau
puis arrondie, et son gain ne s'ajoute pas au feedback (elle colore, elle ne pousse pas), dans une ligne
de 1,2 s. Quand le temps change, la lecture glisse jusqu'au nouveau, comme une bande.

**L'EQ** (HOME, troisième face) : un potard par piste pour lui faire de la place dans le mix. Au milieu, rien
(Flat) ; à gauche, un passe-bas qui descend de 18 kHz à 125 Hz (on enlève les aigus) ; à droite, un passe-haut qui
monte de 20 Hz à 8 kHz (on enlève les graves), en 24 dB/octave (deux SVF de stmlib, Butterworth), une octave par
pas de potard sur toute la course : un peu de potard enlève un peu, au bout c'est un mur. Le filtre est avant le
duck et les envois (ce qui part au delay et à la réverbe est filtré aussi) ; il se locke pas par pas, il a son
aléatoire (ENV) et un LFO peut le moduler. L'écran trace les quatre courbes, chacune dans la couleur de sa piste, la
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

**Le mode MOD** (SEL) : les trois premières touches blanches (fa, sol, la) deviennent les trois LFO, et clignotent
chacune à la vitesse de son LFO. On tient la touche d'un LFO et on tourne un potard : le câble est posé, comme un
atténuverteur : vers la droite le LFO pousse plus, vers la gauche il agit à l'envers ; revenir à 0 le retire (en
passant par 0, le potard s'y arrête). Le
potard montre en pointillé jusqu'où le câble l'emmène, le LFO écrit dessous.

**La matrice** (SEL tenu) : les 8 câbles de la piste puis les 8 globaux. K1 choisit le câble, K2 sa source, K3 sa
cible, K4 sa profondeur.

**Les LFO** (LFO, trois faces : LFO 1, 2, 3) : **WAVE** (Sine, Triangle, Saw qui monte, Ramp qui descend, Square,
S&H : une valeur au hasard par cycle, Smooth : un aléatoire lissé), **RATE**, **MODE** et **SLEW**. MODE : Free
(RATE de 0,02 à 20 Hz), Sync (calé sur la mesure, RATE devient un rapport : 4 par temps à 1 pour 16 mesures) ou
T1 à T4 (relancé au début à chaque trig de la piste : une rampe qui descend fait une enveloppe à chaque note). SLEW
lisse la sortie (1 ms à 2 s : un carré arrondi, un S&H qui glisse). L'écran trace ce que le LFO a fait ces 1,4
dernières secondes ; en bas, les trois LFO, chacun dans sa couleur, allumés à leur hauteur. Les LFO remplacent les
deux Tides des 0.3 : ils coûtent à peu près pareil (presque rien), mais se règlent d'un coup d'œil.

**Le son** (EDIT), première page : FREQ, puis HARMONICS, TIMBRE et MORPH sous le nom (court) que leur donne le manuel de Plaits pour
le modèle choisi (le KICK : ATTACK, BRIGHT, DECAY ; le FM 2-OP : RATIO, INDEX, FDBK ; le WAVETABLE : BANK, ROW,
COLUMN…). **ENV**, première page : l'enveloppe. Les modèles qui ont la leur (STRING, MODAL, KICK, SNARE, HI-HAT, et les
6-OP FM dans leur preset) contournent le LPG de Plaits : LPG COLOUR est alors grisé, et pour STRING, MODAL et les trois
batteries, DECAY s'ajoute à leur decay (leur MORPH) : il marche sur toutes les pistes, avec ses locks et son
aléatoire. Un potard grisé tourne toujours, mais ne change rien au son dans l'état présent.

**Le circuit bending** (EDIT, deuxième ensemble de la piste ; 0.7, à la place du grit) : de quoi casser le son,
musicalement. **Première page, les bits**, avant le filtre, à la manière du Biscuit d'OTO Machines (une boîte 8 bits
avec un interrupteur par bit) : **CLOCK** retient chaque échantillon (un sample & hold) sur un multiple de la note
jouée, de x48 à x2 : le repliement reste accordé ; **BITS** arrondit les échantillons, de 12 bits à 1 ; **MASK** lit
chaque échantillon comme un mot de 8 bits et allume, inverse ou coupe ses bits (douze masques : les bits bas coupés ou
inversés, des trous au milieu, le bit de signe inversé, trois bits seulement, un peigne… ; la partie sous le mot est
gardée tant que son dernier bit n'est pas coupé) ; **SHAPE**, une forme numérique d'abord : Rect (redressé, une octave
au-dessus), Down et Down 3 (le signe retourné tous les deux ou trois cycles, comme un diviseur : une octave, une
octave et une quinte en dessous), Fold (replié) ou Swap (les deux moitiés du mot échangées). Un condensateur de
liaison enlève le continu qu'un redresseur ou un bit retourné laissent. L'écran montre ce que les bits font d'une
sinusoïde et les huit interrupteurs (allumé, inversé, coupé). **Deuxième page (SELECT), le glitch**, après le
filtre, à la manière du Data Bender de Qu-Bit : à chaque double croche, **GLITCH** est la chance que la piste bégaie ;
elle prend alors une tranche du son qui passe (**SIZE** : 1/64, 1/32, 1/16 ou 1/8, divisée par deux tant qu'elle ne
tient pas dans la mémoire) et la rejoue jusqu'à ce qu'une double croche en décide autrement ; **BEND** rend chaque
répétition plus lente et plus grave (une bande qui s'arrête) ou plus rapide et plus aiguë ; **DROP** est la chance
qu'une répétition ait un accident (jouée à l'envers ou un trou). Les tranches sont gardées en 8 bits mu-law à
22,05 kHz, 4 Ko par piste (186 ms : le FM-1 n'a plus de mémoire à donner), un peu lo-fi, comme un sampler tordu.
Tout se locke pas par pas, a son aléatoire, et les potards continus se modulent par les câbles.

**Le filtre** (EDIT, troisième ensemble de la piste : entre les bits et le glitch, avant Warps et le mixer). **ALGORITHM** choisit le
modèle (GLO tenu : sur les quatre pistes). OFF par défaut : il ne coûte alors rien, et ses potards sont grisés.

| Modèle | D'où il vient | Types (TYPE) | Ce que DRIVE y fait |
| --- | --- | --- | --- |
| **LADDER** | l'échelle à transistors du Moog, d'après le modèle « amélioré » de Stefano D'Angelo et Vesa Välimäki (2013 ; leur code de référence, licence ISC) : la saturation de chaque paire de transistors à chacun des quatre étages, l'auto-oscillation du circuit | LP 24 dB ; HP 24 et BP pris sur ses étages (les mélanges de l'Oberheim Xpander) | ce qui entre dans les transistors (un peu plus fort, puis tassé : le niveau tenu) |
| **MS-20** | le Korg 35 (MS-10, premiers MS-20), d'après Will Pirkle (note AN-5) sur les filtres à rétroaction sans délai de Vadim Zavalishin, la saturation **dans** la boucle de résonance : elle drive et chante | LP 12 dB, HP 6 dB, BP (un passe-haut puis le passe-bas, comme les deux filtres du MS-20 en série) | la saturation de la boucle (son niveau rendu) |
| **SEM** | le filtre d'état 12 dB de l'Oberheim SEM (Pirkle, Zavalishin) : il résonne sans s'auto-osciller, comme le vrai | LP, BP, HP | un étage d'entrée à transistor un peu décentré (des harmoniques paires avec les impaires, sa courbe anti-repliée), puis ses deux intégrateurs (des OTA) poussés : la résonance s'épaissit et se tasse |
| **VOX** | trois formants, les trois premières résonances des voyelles (moyennes mesurées de Peterson et Barney, 1952) : CUTOFF passe de A à E, I, O, U, la résonance les aiguise | voix basse, moyenne, haute | le même étage d'entrée, et ses trois résonateurs poussés, plus doucement (les voyelles restent) |

**Première page** : **TYPE** (LP, BP, HP ; VOX : LOW, MID, HIGH), **CUTOFF** (20 Hz à 18 kHz ; VOX : la voyelle),
**RESO**, **ENV** (de combien l'enveloppe ouvre ou ferme, ±8 octaves). **Deuxième page** (SELECT) : **DRIVE**,
**NOISE** (du bruit dans le filtre, sur tous les modèles : la résonance souffle et siffle ; il suit l'enveloppe, il
souffle aux trigs, pas en continu), **COLOR** (le bruit : plus sombre à gauche, blanc au centre, plus brillant à
droite), **FM** (la sortie du filtre renvoyée dans sa fréquence de coupure, échantillon par échantillon, d'un côté ou
de l'autre : de la rugosité, puis du chaos). L'**enveloppe** du filtre est sur ENV (deuxième page) : celle d'un
vactrol (celle du low-pass gate de Plaits), **F.ATK** sa montée (instantanée à gauche, jusqu'à une montée lente),
**F.DECAY** sa chute, qui ralentit en descendant (de 5 ms à plus d'une seconde, puis sa queue), et **PING**, une
impulsion dans le filtre à chaque trig : avec de la résonance, il sonne tout seul (une percussion, même sur une voix
silencieuse). **F.KEY** (deuxième page du son) fait suivre la note (100 % : une octave par octave). Tout se locke pas
par pas (sauf TYPE), a son aléatoire, se module par les câbles, et la dose d'aléatoire relance ses réglages à chaque
trig (pas son PING ; son TYPE aussi au-delà de 60 %). À
STOP, les enveloppes se ferment et les filtres s'éteignent en ~23 ms (une auto-oscillation, un ping qui sonne encore,
leur bruit) jusqu'au trig suivant. L'écran du filtre dessine sa réponse telle qu'elle est (son enveloppe, sa FM, la
touche), sa fréquence et, dans un petit cadre à côté de son nom, ce qu'il coûte au cœur qui le joue (en % du
bloc : la part du filtre dans T1..T4 de la page machine) ; celui de l'enveloppe, sa courbe (la montée, la chute, le ping) et, dans l'anneau, où elle en est.

Les projets d'avant 0.6.0 sont relus tels quels : le type de filtre passe sur TYPE, le NOISE de SEM et le BREATH de
VOX sur NOISE (au même niveau de bruit).

**Les rythmes euclidiens** (SEQ, troisième face ; sur les effets, la dernière face) : **FILLS** coups répartis aussi
régulièrement que possible sur la longueur de la piste, **ROTATE** les décale ; LENGTH et PROB sont ceux de la piste
de pas. Tourner FILLS, ROTATE ou LENGTH **réécrit les trigs** de la piste (ses locks restent) ; on peut ensuite les
retoucher à la main. L'écran montre les pas en cercle, les trigs allumés, celui qui joue entouré.

**L'aléatoire** : la page RANDOM (SEQ, puis SELECT) a quatre potards, PAN, DECAY, TUNE et RATCHET. Tourné à droite,
une nouvelle valeur au hasard à chaque trig ; à gauche, des valeurs au hasard mais qui reviennent les mêmes à chaque
tour (une boucle) ; plus on tourne, plus l'écart est grand (100 : toute la course ; TUNE : une octave de part et
d'autre ; RATCHET : jusqu'à 7 coups de plus). Au centre, rien : le réglage retrouve exactement sa valeur. **Chaque
retour au centre tire une nouvelle boucle** pour ce réglage : une boucle qui ne plaît pas, on revient à 0 (ou on passe
côté hasard), on retourne côté boucle, c'en est une autre (les boucles des autres réglages ne bougent pas ; un projet
sauvegardé garde les siennes). **ENV tenu
+ un potard** fait la même chose sur n'importe quel réglage d'une piste ou d'un effet.

**Le quantiseur et le swing** (GLO) : TEMPO, SCALE et ROOT (chaque note, des pas, de l'aléatoire ou du clavier, va
à la note de la gamme la plus proche : Off, Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Harmonic et
Melodic minor, Major et Minor pentatonic, Blues, Whole tone, Hirajoshi), SWING (50 % : droit ; 75 % : les
contretemps retardés d'une demi-double croche). Quand une gamme est choisie, tout ce qui fait une hauteur y reste :
FREQ (et ce que les câbles et la dose d'aléatoire en font : un LFO sur FREQ joue en escalier dans la gamme), la
transposition, la note de l'oscillateur de Warps. Seul P.ENV glisse encore d'une note à l'autre : c'est l'enveloppe
interne de Plaits, un glissando voulu.

**Endless** (ARP, comme sur l'OP-1) : sur sa page, **REC** lance l'enregistrement (la frise passe en REC, le bouton
clignote) ; chaque touche ajoute une note (on l'entend), **SEL** ajoute un silence, jusqu'à 64 pas. **REC** à
nouveau : la piste joue ces notes en boucle sur le tempo, à la place de ses pas. Pendant qu'elle joue, **une
touche transpose** la boucle depuis sa première note (la transposition reste). Ses potards : RATE (1/32 à 1/4, avec
les triolets), GATE (la longueur des notes), DIRECTION (avant, arrière, aller-retour, au hasard), TRANSPOSE. **REC
tenu** efface la boucle (la piste retrouve ses pas). Probabilité, ratchets, aléatoire et quantiseur s'appliquent aussi
à ses notes. En bas, ses notes en barres, la note jouée en blanc.

**Warps** (FX, troisième effet) : le module de modulation croisée de Mutable, entre deux pistes. Sa première page
suffit pour jouer : **CARRIER**, la porteuse (Off : Warps éteint ; une piste T1-T4 ; ou son oscillateur interne,
trois formes), **MOD**, la piste modulante, **ALGO**, qui balaie ses six modulations croisées (crossfade,
cross-folding, anneau à diodes, anneau numérique, XOR, comparateur) en fondu de l'une à l'autre comme sur le module
(le sélecteur ALGORITHM saute d'un repère au suivant), et **MIX**, le dosage : à 0 la piste telle quelle, à 100 %
Warps seul. La sortie de Warps prend la place de la piste porteuse (ou de la modulante avec l'oscillateur), avec son
niveau, son pan et ses envois. Sa deuxième page (SELECT) va plus loin : **TIMBRE**, **LEVEL 1** et **LEVEL 2** comme
sur le module (avec l'oscillateur interne, LEVEL 1 règle sa note, C0 à C5, dans la gamme quand il y en a une), et
**MODE** : Warps ; + Dry (on entend aussi la piste modulante telle quelle) ; ou Freq shift, l'easter egg du module,
un décaleur de fréquence : ALGO fixe alors le décalage (au centre, rien ; jusqu'à ±2 kHz), TIMBRE la bande, LEVEL 1
la réinjection, LEVEL 2 le mélange. Sa piste de pas locke ALGO, MIX, TIMBRE, LEVEL 1 et LEVEL 2 pas par pas (pas la
porteuse, la modulante ni le mode) et l'ouvre ou la ferme sur la piste. Son oscillateur interne ne sonne que quand
le séquenceur tourne ou qu'on joue au clavier : à l'arrêt, il s'éteint en 30 ms (plus de dent de scie qui tourne
sous rien) et Warps ne calcule plus rien. Warps joue un bloc derrière les voix (~4 ms de latence sur la piste qu'il
remplace).

**La dose d'aléatoire** (cachée) : tenir **OCT−** et tourner **SELECT** (OCT− et OCT+ ensemble marchent toujours).
La machine passe sur **son écran à elle** : la troisième couleur du thème s'ouvre depuis le centre, RAN / DOM s'écrit
en grand, en blanc, ses lettres arrivant de gauche à droite à travers des signes ; plus la dose monte, plus ses
lettres se mettent à changer (une à quelques %, les six à 100 %, toutes relancées à chaque cran) et plus les cases du
fond tremblent. Dessous, la dose, sa barre (le dernier quart marqué) et ce que font les fills. **À chaque trig de
chaque piste**, tous ses réglages du son sont relancés au hasard, chacun de son côté : chaque note est un autre son,
rien ne boucle. **La dose suit une courbe** (son carré) : au début de la course, des écarts à peine perceptibles (à
20 %, 4 % de la plage), de plus en plus francs, toute la plage à 100 %. **Tout reste dans la gamme** : FREQ (jusqu'à
±6 demi-tons à 100 %) et la transposition (±7) sautent par demi-tons entiers, puis sur les notes de la gamme choisie ;
ce qui en sortirait n'est jamais tiré : l'enveloppe de hauteur (son glissando), le PING du filtre (une résonance
qui sonne à n'importe quelle hauteur), HARMONICS là où c'est un intervalle (les accords de CHORD, STRING MACHINE et
CHIPTUNE, le désaccord du VIRTUAL ANALOG, l'écart de SWARM et de PARTICLE), le décalage de Warps en décaleur de
fréquence. Le reste : HARMONICS ailleurs, TIMBRE, MORPH, DECAY, COLOUR et les enveloppes de Plaits (sur une batterie
FM, ses réglages), la gate, les bits (CLOCK, BITS, MASK, SHAPE) et le glitch (GLITCH, SIZE, BEND, DROP : à forte dose, des notes qui
bégaient), le
filtre (CUTOFF jusqu'à ±3,4 octaves, RESO, ENV, DECAY, DRIVE, FM, KEY, NOISE, COLOR, ATTACK ; au-delà de 60 % son
TYPE aussi, LP, BP ou HP, sur un trig sur deux à 100 % ; pas son modèle : un filtre OFF le reste, son CPU avec),
l'EQ et le pan. **Les envois d'effets**, de temps en temps et légèrement : à 100 %, un trig sur sept environ part
dans le delay ou dans la réverbe (un envoi de 20 à 35 %, en plus du sien), presque jamais à mi-course. Les effets
eux-mêmes sont relancés à chaque temps : le feedback, le filtre, le drive, la résonance et le pleurage du delay, la
réverbe, l'algorithme de Warps (d'un repère à un autre : entre deux, Warps calcule les deux algorithmes, deux fois
plus de travail), son timbre et ses niveaux. Rien de ce qui tient le temps (divisions, tempo, vitesses, longueurs,
pas), ni les niveaux, la gamme ou le branchement. On lâche OCT− : retour à la page, la dose reste (en bas, un trait
de la largeur de la dose ; les potards montrent la valeur que le dernier trig a tirée, RANDOM dessous). Revenir à 0
rend le son exact de départ dès le trig suivant, comme l'annulation d'ALL. Pendant le geste, l'octave du clavier (ou
la page de pas) ne bouge pas. (Jusqu'en 0.6.1, la dose tirait un seul écart par réglage, gardé à chaque trig : un son
déplacé, mais le même à chaque note.)

**Les fills** (le dernier quart de la dose, 76 à 100 %) : des trigs en plus à la fin des mesures (comptées depuis
PLAY), comme un batteur, sur les pistes qui jouent des pas (pas sur Endless). À 76 %, un fill sur le dernier temps
toutes les 4 mesures, sur une piste ; en montant, toutes les 2 mesures (85 %), sur les deux derniers temps (88 %), à
chaque mesure (93 %), sur une piste de plus à chaque tiers du quart (les quatre à 100 %), avec des motifs de plus en
plus chargés : une levée, des croches puis des doubles, un galop, un 3-3-2, un roulement, des flas et des ratchets
(deux ou trois coups dans une double croche), une descente de toms (la hauteur qui descend à chaque coup, dans la
gamme). Quelles pistes et quels motifs : tirés du hasard de la dose et de la mesure, les mêmes tant qu'on ne touche
pas la dose. Un trig de fill joue toujours (quelle que soit la probabilité du pas) ; sur un pas qui a déjà son trig,
le fill lui donne ses ratchets et sa hauteur.

**La sortie** (GLO, SELECT, K2) : Stereo, Mic headset ou Mono. **Mic headset** est pour un casque à micro (fiche à
4 contacts) branché à fond dans la prise du FM-1 (3 contacts) : ses écouteurs se retrouvent en série entre la
gauche et la droite et ne reçoivent que leur différence (le centre disparaît, ce qui est panoramiqué ressort). Le
canal droit inversé, ils reçoivent tout le mix, en mono (une oreille en opposition de phase : le son paraît un peu
« dans la tête »). Pour la stéréo, un casque sans micro ou un adaptateur casque/micro. Le réglage reste quand on
charge un autre projet.

**Les couleurs** (GLO, SELECT, K4) : huit thèmes, chacun un fond sombre et trois couleurs pour l'état des modules
(celles que les icônes et les anneaux prennent) et ALL : **Acid** (citron vert, violet, rouge orangé), **Ember**
(or, orange, cramoisi sur un noir chaud), **Neon** (menthe, lavande, rose sur indigo), **Ice** (cyan, pervenche,
magenta), **Rust** (vert-de-gris, rouille, jaune danger sur graphite), **Mono** (blanc, argent, rouge) et
**Mutable** (le teal, l'orange et le rose des modules, ceux des versions 0.1 à 0.3.6) et **Graphite** (des gris et
une seule lumière, orange, comme le Machinedrum d'Elektron : celui de la démo dans le navigateur). Comme la sortie et la
qualité de Warps, le thème reste quand on charge un autre projet ; il est gardé avec le projet sauvegardé.

**Le clavier** relance la note à chaque touche, même jouée legato, et une touche tapée très vite sonne quand même.

Un projet sauvegardé avec les 0.1.4 à 0.2.6 se recharge : ses réglages, ses modèles, ses câbles (sauf ceux qui
partaient de Marbles), ses pas, ses locks et Endless ; le tempo de Marbles devient le tempo, et les effets nouveaux
démarrent neutres (rien vers le delay, aucune piste de pas d'effet). Ceux des 0.3 aussi (l'EQ y est à plat, le grit
coupé ; depuis 0.7, le grit d'un projet plus ancien est oublié : le circuit bending prend sa place, coupé). En 0.4, les deux Tides laissent la place aux trois LFO : les câbles qui partaient de Tides A1, A2 et A3
partent des LFO 1, 2 et 3, les autres sont retirés, et les LFO démarrent comme ceux d'un projet neuf ; les potards de
Warps passent sur leurs nouvelles faces (MIX à 100 %, DRY et Freq shift dans MODE), avec leurs locks et leurs
aléatoires ; le delay est en SYNC.

## Le dépôt

- `firmware/` : le firmware (fm1-base `tools/build.py ../firmware`)
  - `app.c` : l'écran et les contrôles ; `mi/engine.cc` : le son (les pistes, les pas et leurs trigs, les LFO, la
    matrice, le quantiseur, les effets et leurs pistes de pas, le mixer ; les voix et Warps partagés entre les deux
    cœurs) ; `mi/mi_*.cc` : un pont C par module Mutable ; `mi/mi_reverb.h` : la réverbe de Clouds, ses lignes de
    retard en flottants ; `mi/mi_delay.h` : le delay (le SVF et la saturation de stmlib dans une ligne 16 bits)
  - `third_party/` : le code Mutable Instruments utilisé, copié tel quel ; les retouches sont dans `PATCHES.md` ;
    `third_party/lxr/` : les voix du LXR-02 (leurs retouches dans `MIFM1.md`), `lxr_port.h` / `.cc` ce sur quoi
    elles tournent ; `mi/mi_lxr.cc` : leur pont
  - `res/mi_tables.bin` : la zone de ressources (les plus grosses tables, écrites par l'installeur après le
    firmware), `mi/res_abs.cc` leurs adresses
  - `shim/` : les en-têtes C et C++ que la chaîne JieLi n'a pas (pas de libm, pas de bibliothèque C++)
  - `tests/` : le moteur sur l'ordinateur (`native.cc`) et le simulateur de fm1-base (`screens.txt`)
- `tools/` : la copie du code Mutable (`vendor_mi.sh`, `patch_mi.sh`, `split_tables.py`) et celle du LXR-02
  (`vendor_lxr.py`, depuis un clone de son dépôt ; `lxr_check.cc`), les vérifications « comme
  sur le FM-1 » (`device_check.sh`, `check_res_links.py`, `no_static_init.py`), l'installeur ; `tools/bench/` : le
  banc d'essai v0.0 (il se construit depuis le tag `bench-0.0` : les ponts vers les modules ont changé depuis)
- `make test` : le patch par défaut joué 16 s sur l'ordinateur, chaque piste et chaque modèle entendus, sans NaN
  (WAV dans `build/wav`), et `make lxr-check` : les batteries FM comparées au calcul du LXR-02

## Licences

Le firmware tel qu'il est publié (l'installeur, le `.fwsc`) contient les voix du LXR-02, sous GPL-3.0 : il est
distribué, dans son ensemble, sous **GPL-3.0** (`COPYING` ; le détail fichier par fichier : `LICENSING.md`), et ses
sources sont ce dépôt. Le code propre à MIFM1 reste sous
MPL-2.0, comme fm1-base (la MPL-2.0 permet de le combiner à du code GPL : sa section 3.3). Le code de Mutable
Instruments (Émilie Gillet) est sous licence MIT (`firmware/third_party/LICENSE-eurorack`). Les voix du LXR-02
(`firmware/third_party/lxr/`, et ce qui en reprend du code : `mi/mi_lxr.cc`, `tools/vendor_lxr.py`,
`tools/lxr_check.cc`) sont sous GPL-3.0-only (Copyright Erica Synths), avec les conditions de son `NOTICE.md`.
Mutable Instruments est une marque déposée, de même qu'Erica Synths, LXR et LXR-02 : ce projet n'utilise pas ces
noms comme les siens (à l'écran, les modèles s'appellent FM DRUM, FM SNARE…) et ne les cite que pour dire de qui est
le code qu'il fait tourner.
