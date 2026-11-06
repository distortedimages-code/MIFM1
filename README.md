# MIFM1

Une groovebox 4 pistes pour le M-VAVE FM-1 dont tout le son vient de code open source : le sien, celui de Mutable
Instruments et les voix de batterie du firmware de l'Erica Synths LXR-02, sur la base de
[fm1-base](https://github.com/charlesvestal/fm1-base) :

- un **filtre** par piste, quatre modèles d'après les meilleurs modèles virtuels analogiques ouverts : **LADDER**
  (l'échelle Moog), **MS-20** (Korg 35, la résonance qui drive), **SEM** (Oberheim) et **VOX** (des formants), en LP,
  BP ou HP, avec du bruit coloré dedans, une enveloppe à vactrol, un **ping** et une **FM** par sa propre sortie ;
- **six batteries** écrites pour MIFM1 (0.9), d'après le Microtonic de Sonic Charge : KICK, SNARE, CLAP, HAT, TOM,
  PERC, un oscillateur et un bruit chacune (voir plus bas) ;
- **la FM** de MIFM1 (0.10) : quatre opérateurs comme dans la puce de la Mega Drive (le YM2612), ses huit algorithmes
  sur un potard, la rétroaction de son premier opérateur, et deux mutations de l'Hydrasynth (PW, sync) ; trente accords
  des opérateurs, dont huit vrais accords (voir plus bas) ;
- **PADS** de MIFM1 (0.11) : des accords et leurs progressions (Arvo Pärt, la Folia, Pachelbel, la basse de
  lamentation, Bach, Satie…) joués par une nappe de cordes, sur une piste ; ses accords suivent la gamme du projet, et
  la gamme **Prog** fait suivre l'accord joué à toutes les autres pistes, en temps réel ; une **TEXTURE** d'après les
  nappes usées de Boards of Canada (0.16.16 : la bande, la dérive, la poussière, l'octave dessous, une cloche ; voir
  plus bas) ;
- **POLY** (0.12) : un petit synthé analogique polyphonique d'après le SH-101 de Roland et le S-1, quatre voix par
  piste, chacune son filtre à quatre pôles et son enveloppe ; **MODE** Mono ou Poly (0.16.15 : en Poly, les accords
  se jouent à la main au clavier), et un mode **VINTAGE** qui écarte un peu chaque voix (voir plus bas) ;
- **REV3** (0.13, PROPHET jusqu'à 0.16.15) : le grand polyphonique d'après le Prophet-5 de Sequential Circuits, nommé
  d'après sa troisième révision, quatre voix par piste, deux
  oscillateurs (treize réglages du panneau), son filtre, ses deux enveloppes, sa **Poly-Mod** (le balayage de la sync,
  la FM), un bouton **VINTAGE** comme celui de la Rev 4, une mémoire d'accords ou des accords qui suivent l'harmonie
  (celle de PADS, ou la gamme), et sans accord, ceux qu'on joue au clavier (voir plus bas) ;
- **OTIS** (0.16.14, SQ-1 jusqu'à 0.16.15) : un Ensoniq de poche, nommé d'après la puce du SQ-1 (OTIS, l'ES5505),
  quatre voix par piste : des **Transwaves** (32 cycles qui changent peu à peu, lus entiers et par marches ; depuis
  0.16.16, huit classiques et sept plus granuleuses), les filtres sans résonance de la puce, trois couches, un petit
  effet ; 24 programmes en huit familles, huit potards (voir plus bas) ;
- **TAURUS** (0.16.20) : une basse monophonique très grasse d'après les pédales **Taurus** de Moog (deux scies qui
  battent, l'échelle à transistors du Minimoog, un contour qui retombe, le DECAY, le glide ; ses trois presets TAURUS,
  BASS, TUBA) et le **Minimoog** (ses six formes d'onde, son troisième oscillateur une octave dessous, son mixer qui
  pousse le filtre), ses graves rendus quand la résonance monte (voir plus bas) ;
- **VOCODER** (0.16.21) : un vocodeur à 12 bandes et son propre synthé, d'après le **Vocoder Plus VP-330** de Roland :
  la voix est une piste au choix (MOD), la porteuse quatre voix de **scies** grasses qui deviennent des **cordes** puis
  un **chœur** (TONE), un **ensemble** pour l'épaissir, SHIFT pour déplacer les formants, AIR pour laisser passer les
  sifflantes ; sans voix, une machine à cordes ; depuis la 0.16.28, **PLAY** le joue en Mono, en Poly ou sur les
  **progressions de PADS** (voir plus bas) ;
- **STATIC** (0.16.24, refaite en 0.16.27 d'après **ARC/01**, le cahier des charges de Yoann) : une machine
  d'**événements électriques** pour des séquences minimales, percussives et instables, d'après **Pan Sonic** et **Ryoji
  Ikeda** ; trois générateurs, un par piste ou un par pas : **BODY** (un sinus sub, sa hauteur qui tombe, pur ou
  saturé), **ARC** (une porteuse modulée en phase par un modulateur libre en Hz, son feedback, un modulateur en anneau),
  **SPARK** (des salves de bruit, de bruit tenu ou d'impulsions dans un passe-haut ou un passe-bande) (voir plus bas) ;
- **Plaits** sur chaque piste, 6 de ses modèles (ses trois batteries, treize de ses oscillateurs, sa puce 8 bits et ses
  formants ont laissé la place aux moteurs de MIFM1), et un **circuit bending** par piste (0.7, à la place du
  grit) : ses **bits** à la manière du Biscuit d'OTO (une horloge calée sur la note, la résolution, les huit bits du mot
  allumés, inversés ou coupés, des formes numériques) et son **glitch** à la manière du Data Bender de Qu-Bit (des
  répétitions calées au tempo, ralenties ou accélérées, des accidents) ;
- **quatre batteries FM** comme modèles de piste en plus : les voix du **LXR-02** (son firmware est open source,
  GPL-3.0), grosse caisse et toms, caisse claire, cymbale et charleston, leur code tel quel (voir plus bas) ;
- **ZERO** (0.8.1), la voix propre de MIFM1 : des sinusoïdes pures en vraie FM à travers zéro (un modulateur sinus,
  triangle ou dent de scie), des pulsations rondes, un battement qui fait vivre le son ; chaque trig est un autre son,
  ou les mêmes seize en boucle (voir plus bas) ;
- un **séquenceur pas à pas** à la Elektron, 64 pas par piste, avec P-locks, et deux modes de jeu : **KEY** (le
  clavier joue la piste) et **STEP** (le clavier est la grille, les touches noires choisissent la piste ou l'effet) ;
  depuis 0.16.31, un **enregistrement en direct** (REC + PLAY : ce qu'on joue au clavier écrit sur les pas, avec un
  métronome) ;
- **Endless** sur chaque piste, comme sur l'OP-1 : on enregistre des notes et des silences un par un, ils tournent
  en boucle, une touche les transpose ; séquenceur arrêté, une touche tenue les joue (0.16.11) ;
- sur chaque trig : **probabilité**, **ratchets**, **longueur**, et un **aléatoire** sur n'importe quel réglage de la
  piste (à droite nouveau à chaque trig, à gauche en boucle) ; un **swing** global ; des **rythmes euclidiens** sur
  chaque piste de pas ;
- un **quantiseur global** (15 gammes et la tonique, et Prog : les accords de PADS) : aucune fausse note, sur toutes
  les pistes et au clavier, FREQ, la transposition et l'oscillateur de Warps compris ;
- **trois LFO** comme modulateurs (sinus, triangle, dent de scie, rampe, carré, S&H, aléatoire lissé ; depuis
  0.16.22, sept formes rythmiques et le chaos de Lorenz), chacun libre, calé sur le tempo ou relancé par les trigs d'une
  piste, avec un **slew** ; depuis 0.16.23, chacun peut devenir un **suiveur d'enveloppe** (FOLLOW) qui écoute une
  piste ;
- une **matrice de câbles** : les 3 LFO vers presque tous les paramètres, 8 câbles par piste et 8 globaux, posés en
  tenant la touche d'un LFO et en tournant un potard (en atténuverteur) ;
- une **dose d'aléatoire** cachée (OCT− tenu, SELECT), sur son propre écran, qui relance tout le son à chaque trig
  de chaque piste (hauteurs toujours dans la gamme, timbres, bits et glitch, filtres, pans, un envoi d'effet de temps en
  temps), discrète au début de la course et folle au bout, le rend tel quel à 0, et dans son dernier quart ajoute des
  **fills** en fin de mesure ;
- **TRAUMA** (OCT+ tenu, SELECT), sur son propre écran : une dose de saturation, en hommage à Audiotrauma, qui pousse
  petit à petit le son dans le rouge (des drives sur toutes les pistes, la FM des filtres, Warps, le drive du delay)
  sans jamais toucher une note, et ne fait rien à 0 ;
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
  place ; depuis 0.16.5, passer du passe-haut au neutre, du neutre au passe-bas ou de l'un à l'autre se fait en fondu
  de 3 ms, sans clic), un envoi par piste vers le delay et un vers la réverbe (ses lignes de retard sur 16 bits : la
  réverbe du module les stockait sur 12, d'où un grain et un souffle ; 0.5 et 0.6 les gardaient en flottants, 0.7
  donne l'autre moitié aux tranches du glitch) ;
- un **SPREAD** par piste (0.16.19) : la piste s'ouvre en largeur autour de son pan, ses graves restent au milieu, et
  sa somme en mono reste exactement la piste (voir plus bas) ;
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
« Repères » les montre tous), les 25 moteurs avec les icônes de l'écran (`tools/web/icons_svg.py` les reprend d'`app.c`)
et ce qu'ils modélisent, les quatre filtres avec leur réponse, le schéma de leur circuit et leur histoire, le circuit bending,
les effets, le séquenceur, la dose d'aléatoire, les spécifications, et des jauges de la charge du FM-1. « Essayer »,
sur un moteur ou un filtre, le charge sur la piste jouée (`firmware/web.c` web_try). Pour écouter MIFM1 sur un autre système (une
voiture, des enceintes de studio) ou montrer la machine sans elle. Les potards se tournent en glissant vers le haut
ou le bas ; sur un ordinateur, des raccourcis clavier jouent les touches. SAVE tenu garde le projet dans le
navigateur. L'écran du téléphone reste allumé tant que la page joue ; sur iPhone, le son passe même en mode
silencieux, celui des exemples aussi, sans avoir allumé la machine (0.16.29).

**Les exemples à jouer** (0.16.25 ; dessinés en 0.16.30). Le long des articles, de petits modules : un bouton Play, et
**le schéma est le potard** : chacun dessine ce qu'il explique, et on le joue en tirant ce dessin (au doigt, à la
souris ou aux flèches, le dessin pris au clavier). Papier et encre, la poignée en orange, ce qui est mesuré en
bleu-vert ; un son simple à chaque fois (une forme d'onde, un kick seul, un charleston seul), jamais une séquence à
plusieurs pistes. Chacun joue le **vrai code** : une deuxième copie du firmware compilé, dans son propre worklet audio,
jamais démarrée comme machine (`firmware/web.c` lab_*, `web/emu/app_worklet.js`) ; et ce qu'il dessine vient d'une
troisième, sur la page elle-même, qui ne joue jamais : réglée comme l'exemple, elle mesure le filtre, rend le coup,
écrit les valeurs comme l'écran du FM-1. Le dessin est donc là sans avoir rien allumé, et il suit le son quand il joue.
- **LADDER, MS-20, SEM** : la courbe du filtre, mesurée sur sa réponse impulsionnelle (son module du moteur, réglé
  comme les potards), et sous elle les harmoniques d'une dent de scie grave (La 1, 55 Hz, sans repliement) qui passent
  ou non ; on tire le point de coupure (à gauche, à droite : CUTOFF ; vers le haut : RESO), une barre à gauche pour
  DRIVE (le SEM : passe-bas, bande, passe-haut). Quand la résonance dépasse le seuil, le filtre **chante tout seul** :
  une sinusoïde à lui, marquée à sa hauteur.
- **VOX** : la carte des voyelles (F2 de gauche à droite, F1 de haut en bas, celles de Peterson et Barney), le chemin
  a, e, i, o, u que suit CUTOFF ; on glisse le point le long du chemin, RESO à droite, la voix en dessous.
- **KICK** : un coup figé, rendu par le moteur : son onde qui s'éteint et sa hauteur qui tombe (mesurée à ses passages
  par zéro, de 379 à 56 Hz) ; on tire SWEEP et DECAY dans le temps, WAVE à gauche.
- **P-locks** (nouveau) : un kick seul sur 16 pas, la ligne pointillée le potard, et chaque pas qui garde sa valeur à
  lui (sa note, ou son DECAY) ; on tire la barre d'un pas, double-clic pour l'enlever.
- **Euclide** (nouveau) : un cercle de 16 pas et N coups répartis au mieux (le calcul du séquenceur) ; au centre on en
  met plus ou moins, sur l'anneau on le tourne.
- **Ratchets** : les charlestons seuls sur les contretemps, un pas agrandi : où tombent ses coups (CURVE les
  rapproche ou les écarte), à quel volume, à quelle hauteur (CLIMB).

TAURUS, STATIC, TRAUMA, la dose d'aléatoire et Warps gardent pour l'instant leur écran noir (spectre ou niveau en
direct, leurs 16 pas dessous) et leurs potards : ils seront dessinés à leur tour. Un seul son à la fois sur la page : un
exemple lancé arrête celui d'avant et met la machine en pause là où elle en est ; toucher la machine, « Essayer » ou
l'allumer arrête l'exemple et la relance.

**La charge affichée est celle du FM-1, estimée.** Un navigateur ne peut pas mesurer le temps que le FM-1 mettrait (son
horloge avance d'un bloc à la fois, et sa vitesse n'est pas celle du FM-1) : dans cette version, l'horloge du moteur
est un modèle, où chaque partie avance celle du cœur qui la joue de ce qu'elle coûte au FM-1, et le second cœur est
simulé avec la sienne. L'en-tête, la page machine (GLO, SELECT), le CPU des filtres et la répartition entre les deux
cœurs lisent donc des temps de FM-1, et une ligne sous le panneau les résume. Le coût de chaque partie
(`firmware/mi/web_costs.h`) vient des **instructions** qu'elle exécute, comptées par valgrind (`tools/web/calibrate.py`) :
chaque modèle seul (et la FM avec son carré, son PW, sa sync), chaque filtre (avec et sans FM, avec et sans bruit), les bits,
Warps (ses algorithmes, Eco et HQ), les effets avec chaque mode du bus, l'EQ, le duck, la préparation (séquenceur,
LFO, câbles). Des instructions plutôt que des temps : ceux d'un ordinateur ne disent rien de ceux du FM-1 (le stress
test de 0.5.8, mesuré sur les deux : de 9 à 89 fois plus lent selon la piste, l'ordinateur vectorisant certaines
boucles et s'effondrant sur les nombres sous-normaux), alors que le temps du FM-1 suit le nombre d'instructions :
6,75 µs pour mille dans les voix, 6,8 dans Warps, 5,4 dans les effets, 17 dans la préparation (son code est plein de
branches et dispersé : le cache du FM-1 le rate plus souvent). Le stress test retombe ainsi sur ses chiffres du FM-1
(cœurs 78 et 77 %, pic 87 % ; sur la machine 79, 77 et 91) ; ailleurs, compter ±20 % sur une piste : le modèle ne
connaît pas la taille de code d'un modèle (qui passe ou non dans le cache) ni la plupart de ses réglages (la queue
d'un STRING).

La construire : `make web` (avec `pip install ziglang` ; dans `firmware/build/emu/`, à servir en HTTP) :
`tools/web/emcc` passe les commandes d'Emscripten que fm1-base donne au clang de Zig
(wasm32-wasi), et la page de MIFM1 (`firmware/web/emu/`) remplace celle de fm1-base. GitHub la publie à
chaque push dans `emu/`. Elle est écrite en anglais, son français dans un dictionnaire en elle : EN / FR, en haut, la
change sans la recharger (la machine continue de jouer), et le choix reste (`?lang=fr` le donne dans un lien). Une
phrase ajoutée : son anglais dans la page, son élément un `data-t` (le numéro suivant), son français dans le JSON
`i18n-fr`, puis `python3 tools/web/i18n.py check firmware/web/emu/index.html`. Après un changement qui pèse sur le CPU (un module, un filtre), `python3
tools/web/calibrate.py` recompte et réécrit la table.

## Enregistrer (USB)

Branché en USB, le FM-1 est aussi une **carte son** (class compliant : aucun pilote, sur Mac, Windows, Linux, iPad,
iPhone en USB-C), nommée « MIFM1 FM-1 » : **2 entrées, le mix master en stéréo**, 16 bits à 44,1 kHz, ce qu'on entend
au casque (le bus master compris : compresseur ou OTT, limiteur), **sauf le métronome**, qui ne bat que dans le casque
et le haut-parleur. MASTER ne règle que le haut-parleur et le casque : l'enregistrement garde son niveau (on peut couper
le FM-1 et écouter par l'enregistreur). Sur un iPhone, l'appareil photo l'enregistre avec la vidéo ; dans un DAW, une
piste stéréo, le MIDI par le même câble.

**3 dB sous la pleine échelle** (0.16.34) : les crêtes du FM-1 ne dépassent jamais −0,1 dBFS (son limiteur, puis une
saturation douce), mais entre deux échantillons le signal monte plus haut ; la démo, à −1,8 dBFS d'échantillon, a des
crêtes vraies à −0,9 dBTP (mesuré à 4× sur l'émulateur). Un iPhone le convertit puis le compresse en AAC, deux étapes
qui peuvent faire passer ces crêtes au-dessus de 0 et les écrêter (d'où la règle des masters : −1 dBTP au plus) : la
cause probable de l'écrêtage entendu sur les attaques et les basses de TAURUS dans la vidéo, pas au casque. À −3 dB, la
démo reste sous −3,9 dBTP ; le souffle du 16 bits est toujours à plus de 90 dB dessous.

**Depuis 0.16.33.** Avant, l'USB envoyait les quatre pistes séparées et le retour des effets (10 entrées), sans le bus
master : un iPhone n'en prenait que les deux premières, la piste 1 seule (constaté). Les pistes séparées sont mises de
côté pour l'instant (leur code reste, une option à la compilation : `ENG_USB_TRACKS`, `FM1B_UAC_CH`). Et le FM-1 garde
**deux blocs d'avance** sur l'USB au lieu d'un (~8 ms de latence) : un bloc rendu en retard (un pic de calcul) ne
s'entend plus dans l'enregistrement, quand il y laissait des échantillons répétés, un craquement (`make uac-stereo`, sur
l'ordinateur : 60 s, un rendu sur 400 en retard d'un bloc entier ; à deux blocs d'avance aucun échantillon perdu ni
répété, à un, 1 315 répétés).

**Les niveaux** (0.16.31) : chaque moteur sort au même niveau. Le son de chacun, tel qu'ALGORITHM l'amène, a été mesuré
(la sonie de la norme BS.1770 : les 400 ms les plus fortes d'une note tenue et d'une suite de noires) et ramené à
**−21 LUFS pour les synthés** (ils allaient de −16, ZERO, à −30, POLY et SPEECH), −22 pour les kicks, −24 pour les
caisses claires et les toms, −25 le clap, −26 la percussion, −28 la cymbale, −29 les charlestons (ils allaient de −20 à
−35) : une table d'un quart de décibel près, sur la voix telle qu'elle sort (avant ses bits, son filtre et Warps). La
démo sort 1 dB plus bas qu'avant. **Les bits** (le circuit bending) et **Warps** gardent maintenant le niveau qu'on leur
donne : ce qui en sort est ramené à ce qui y entre (suivi sur ~50 ms pour les bits, ~0,2 s pour Warps, qu'on compare à ses
deux pistes ensemble), jamais plus de 3 dB (les bits) ou 6 dB (Warps) au-dessus ; un masque ou peu de bits sur un son
faible le faisaient monter jusqu'à 13 dB, et Warps jusqu'à 20 dB au-dessus d'une piste douce.

Projet en 44,1 kHz (le FM-1 ne propose que ce taux ; un enregistreur à 48 kHz, l'iPhone, convertit lui-même). Sur
Windows, les DAW en ASIO passent par FlexASIO ou ASIO4ALL (ou leur mode WASAPI). Le sens inverse (l'ordinateur qui joue
dans le FM-1) n'est pas activé.

## Jouer

**À l'allumage**, l'écran reste noir pendant que la machine démarre, puis le nom MIFM1 s'écrit en Syne (ses lettres
passent par des signes au hasard dans les couleurs du thème avant de se poser, de gauche à droite), un trait de la
troisième couleur se tire dessous, puis « 4-TRACK SYNTHESIS MACHINE » et la version : 1,8 s, qu'un bouton passe.

Un FM-1 sans projet sauvegardé joue **la démo** (PLAY), un groove : piste 1 un kick sur les temps (
un double à la fin de la mesure) qui fait respirer (le duck) une basse FM en piste 2 (quatre opérateurs en chaîne,
une dent de scie en haut) dans le filtre LADDER (son enveloppe à chaque note, un LFO triangle d'une mesure sur la coupure), qui roule sur les contretemps avec
un riff de locks et un ratchet de temps en temps ; piste 3 le charleston FM sur toutes les doubles croches (decay et
pan tirés à chaque coup, ouvert sur les contretemps, 10 % de coups laissés) ; piste 4 des accords dub de septième
mineure, la FM (trois opérateurs entendus, l'accord sur RATIO) dans le filtre SEM (un S&H sur leur brillance, un hasard lisse sur la coupure, un souffle de
bruit), jetés dans un delay ping-pong à la croche pointée et dans la réverbe ; un compresseur doux sur le bus. Fa
mineur, 124 BPM. Elle est légère exprès (moins de la moitié de chaque cœur, un peu moins depuis que la FM remplace
VIRTUAL ANALOG et CHORDS), de la place pour ce qu'on y ajoute (Warps en prend 20 %).

**Repartir de zéro (INIT)** : GLO, SELECT (la page PROJECT), puis **K1** choisit le projet et **REC tenu** le
charge : *New* (4 pistes sur la FM de MIFM1, sur ses réglages, aucun pas, aucun câble, rien vers les
effets, do majeur), *The demo groove* (la démo ci-dessus) ou *The saved project* (le dernier sauvegardé, pour
annuler), *Stress test* ou *Stress test, on the 4 FM drums*. Le transport s'arrête ; rien n'est sauvegardé tant que SAVE n'est pas tenu.

**Le stress test** (GLO, SELECT, K1 sur *Stress test*, REC tenu) : un patch fixe, le même à chaque fois, qui démarre
tout seul et laisse sous les yeux la page machine (la charge des cœurs, le pic, ce que coûte chaque piste) : pour
comparer les versions entre elles. Une grosse caisse et une basse (la FM synchronisée sur un carré) passées aux bits
(et la piste 4 au glitch), des accords (la FM, trois opérateurs entendus, en PW sur un carré : la FM au plus lourd ;
0.10, les chiffres changent donc avec la FM) et un résonateur MODAL, chacun dans un des quatre filtres (depuis 0.5.8 : LADDER, MS-20, SEM avec sa FM, VOX pingé ; les
chiffres montent donc par rapport aux versions d'avant), Warps (anneau à diodes, T3 par T2), OTT sur le bus, le delay et la réverbe nourris, le duck, les
filtres, les trois LFO sur 20 câbles, des aléatoires, des locks et des ratchets. La sortie, la qualité de Warps (Eco
ou HQ) et le thème restent ceux de la machine : comparer avec le même réglage de Warps. *Stress test, on the 4 FM
drums* (le cinquième choix) joue le même patch, les mêmes câbles et les mêmes effets, mais les quatre pistes sur les
quatre batteries FM (chacune sur son kit, à sa hauteur) : ce que coûtent les voix du LXR-02 à côté de celles de
Plaits, piste par piste sur la page machine. Sur l'ordinateur, il prend un quart du temps du stress test.

**Les batteries** (0.9 ; ALGORITHM au début, la banque DRUMS) : celles de MIFM1, d'après le
[Microtonic](https://soniccharge.com/microtonic) de Sonic Charge (Magnus Lidström, 2003), un synthétiseur de batterie
qui ne lit aucun échantillon. Chacune tient en deux sources mélangées puis saturées : un **oscillateur** (sinus,
triangle ou dent de scie, entre les trois ; sa hauteur modulée, qui tombe d'en haut ou monte d'en bas, ou suit une
sinusoïde jusqu'à 2 kHz : du métal ; une attaque nette ou douce, une décroissance exponentielle) et un **bruit** blanc
passé dans un filtre à variables d'état (passe-bas, passe-bande ou passe-haut), son enveloppe exponentielle, linéaire
(coupée net : une caisse claire « gated ») ou modulée (relancée trois fois en quelques millisecondes avant de tomber :
un clap). Puis le **drive** (à droite des harmoniques impaires, dur ; à gauche des paires, chaud ; antirepliement ADAA).
Six modèles, un seul moteur : **KICK**, **SNARE**, **CLAP**, **HAT**, **TOM** et **PERC** (cloches, cowbell, rimshot)
diffèrent par leur kit (la hauteur de l'oscillateur en do 3, le mode de sa modulation, son attaque, le filtre du bruit
et son Q, l'enveloppe du bruit) et par les valeurs de départ de leurs potards. Les huit potards sont les mêmes pour
toutes, aux places de ceux des batteries FM :

| 1/3 : 2 | 1/3 : 3 | 1/3 : 4 | 2/3 : 1 | 2/3 : 2 | 2/3 : 3 | 2/3 : 4 | 3/3 : 1 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **WAVE** (sinus, triangle, dent de scie) | **SWEEP** (la modulation de hauteur : sa durée, 3 ms à 1,5 s ; ou sa vitesse, jusqu'à 2 kHz) | **DECAY** (l'oscillateur, 10 ms à 4 s) | **FILTER** (le bruit, 30 Hz à 18 kHz) | **N.DECAY** (le bruit, 10 ms à 4 s) | **MIX** (− l'oscillateur, + le bruit) | **DRIVE** (− chaud, + dur) | **P.AMT** (la profondeur de la modulation de hauteur, ±48 demi-tons) |

Choisir une batterie met ses potards à son kit et l'OCTAVE de la piste à 0 : un pas en do 3 joue la hauteur du kit,
FREQ, la note, TRANSP et OCTAVE la déplacent. Un trig la relance, son enveloppe fait le reste ; un trig sur un son qui
sonne encore l'enchaîne sans clic. Elles coûtent au FM-1 5 à 7 % d'un bloc par voix (le KICK de Plaits : 14 %).

**Les batteries FM** (ALGORITHM après PERC, la banque FM DRUMS) : **FM DRUM** (la voix grosse caisse et toms du
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

**ZERO** (0.8.1, à la place d'ARC, la voix de la 0.8.0 ; ALGORITHM après FM HI-HAT, la banque MIFM1) : la voix écrite
pour MIFM1, des sinusoïdes pures en **vraie FM à travers zéro**. Une sinusoïde porte la note ; un modulateur la module
en fréquence linéairement, comme l'entrée FM « through zero » d'un oscillateur analogique : la fréquence de la porteuse
est la note plus INDEX fois la note fois le modulateur. Au-delà d'INDEX 1, elle passe sous zéro et la phase repart à
l'envers ; la note reste où elle est (une FM linéaire garde sa hauteur moyenne, une FM exponentielle la fait glisser).
Le modulateur est un sinus, un triangle ou une dent de scie (SHAPE, entre les trois), sur un rapport de la note (RATIO :
x1/4, x1/2, x1, x3/2, x2 à x8). INDEX à 0 : un sinus pur (ses harmoniques sous -120 dB) ; plus haut, une basse ronde
qui se creuse, puis le grain d'une FM en dent de scie. Le tout est calculé à deux fois la fréquence d'échantillonnage et
ramené par un demi-bande IIR polyphase (le dessin de HIIR, de Laurent de Soras : plat jusqu'à 20,3 kHz, -74 dB dès
23,8 kHz) : les bandes latérales qui dépasseraient ne se replient pas (sous -90 dB, dent de scie à x3 sur un do 6).

**La pulsation** : chaque trig est une pulsation ronde, une montée en cosinus (3 à 50 ms, plus longue quand DECAY
l'est), puis une retombée sans angle ((1 + t/T)·e^(−t/T)). **BEAT** fait vivre le son, à un rythme de 0 à 8 Hz ;
**PULSE** dit où : à gauche dans le volume (une seconde sinusoïde, pure, BEAT Hz au-dessus de la note, bat avec la
première : les pulsations rondes de deux oscillateurs un peu désaccordés ; sous une FM profonde, c'est la fondamentale
qui pulse), à droite dans le timbre (le modulateur décalé de BEAT Hz par rapport à son rapport : le spectre de la FM
tourne sur lui-même), au milieu un peu des deux. **MOVE** fait bouger INDEX pendant la note : à droite la FM s'ouvre au
trig et se pose, à gauche elle grandit.

**VARY**, comme les aléatoires de MIFM1 : à droite chaque trig est un autre son, à gauche les mêmes seize sons en boucle,
à l'échantillon près (revenir au milieu tire une autre boucle pour la fois suivante) ; plus loin du milieu, plus ils
s'écartent : l'index, l'accord du rapport (un peu à côté : la FM respire), la décroissance, la forme, le rythme du
battement et où il commence, la lente dérive de la note. Un trig après un silence repart toujours du même point ; un trig
sur un son qui sonne encore continue de là (pas de clic). ZERO sort des formes d'ondes pures faites pour passer ensuite
dans le **circuit bending** de la piste (EDIT) : CLOCK, calé sur la note, les garde accordées.

Ses huit réglages sont aux places de ceux des batteries FM (les potards après FREQ, sur les trois pages du son) :

| 1/3 : 2 | 1/3 : 3 | 1/3 : 4 | 2/3 : 1 | 2/3 : 2 | 2/3 : 3 | 2/3 : 4 | 3/3 : 1 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **INDEX** (0 à 5, à travers zéro dès 1) | **RATIO** (x1/4 à x8, cran par cran) | **SHAPE** (le modulateur : Sine, Tri, Saw) | **DECAY** (une pulsation courte à longue ; au bout, Hold : tenu tant que la note l'est) | **BEAT** (Off, puis jusqu'à 8 Hz) | **MOVE** (− : la FM grandit, + : elle s'ouvre et se pose) | **VARY** (− : en boucle, + : nouveau à chaque trig) | **PULSE** (− : le battement dans le volume, + : dans le timbre) |

Choisir ZERO met ces réglages aux siens (des pulsations rondes de sinus, un souffle de FM, un battement lent, un peu de
boucle) et garde l'OCTAVE de la piste : ZERO joue la note comme Plaits, FREQ, la gamme, TRANSP et OCTAVE compris. ENV
n'a qu'une page, celle du filtre de la piste. Les câbles, les locks, les aléatoires de trig, la dose et le circuit
bending marchent sur lui comme ailleurs. Il coûte au FM-1 environ 11 % d'un bloc par voix (un MODAL de Plaits : 27 %).

**La FM** (0.10 ; ALGORITHM après ZERO, la banque MIFM1) : celle de MIFM1, quatre opérateurs comme dans la
**YM2612** de Yamaha, la puce de la Mega Drive (1988) et du [MEGAfm](https://www.twisted-electrons.com/) de Twisted
Electrons : ses **huit algorithmes** sur **ALGO**, la **rétroaction** de son premier opérateur ; et, sur les opérateurs
qu'on entend, deux des « Mutants » de l'[Hydrasynth](https://www.ashunsoundmachines.com/) d'ASM, PW et sync. Un
opérateur est une sinusoïde dont les opérateurs d'avant déplacent la phase (la modulation de phase de la puce et du
DX7). L'écran dessine l'algorithme joué (ses câbles, ses locks compris) :

| ALGO | Algorithme | Pour |
| --- | --- | --- |
| 1 | 1 › 2 › 3 › 4 | les basses qui claquent, le grain de la Mega Drive |
| 2 | (1 + 2) › 3 › 4 | des leads brillants |
| 3 | (1 + (2 › 3)) › 4 | des cuivres |
| 4 | ((1 › 2) + 3) › 4 | guitares, harpes, koto |
| 5 | (1 › 2) + (3 › 4) | deux paires : un piano électrique (le corps, la lame) |
| 6 | 1 › (2, 3, 4) | trois porteuses désaccordées : des nappes, des accords |
| 7 | (1 › 2) + 3 + 4 | une paire et deux sinus : des cloches |
| 8 | 1 + 2 + 3 + 4 | quatre tirettes d'orgue (INDEX les dose) ; INDEX à 0, un seul opérateur : l'oscillateur d'un analogique |

**RATIO** passe par **trente accords des opérateurs** (leurs quatre rapports à la note ; le 4 est toujours la note) :
harmoniques d'abord (1:1:1:1, 2:1:1:1…), puis des graves (x0,5), des aigus, une lame (Tine : x14), des inharmoniques
(Bell, Tube, Bars, Metal) ; les huit derniers sont des **accords** tempérés, justes avec les autres pistes (Major,
Minor, Sus4, Sus2, Maj7, Min7, Dom7, 4ths) : la note et son octave sur les deux premiers opérateurs, la tierce et la
quinte ou la septième sur les deux autres ; ils s'entendent en accords dès que trois opérateurs sonnent (ALGO 6 à 8),
ailleurs ce sont des modulations inharmoniques.

**FEEDBACK** : le premier opérateur se module lui-même. À droite, sa sinusoïde penche vers une **dent de scie** (ses deux
dernières sorties moyennées, comme le fait la puce ; « Saw » aux trois quarts), puis se casse en **bruit** ; à gauche,
un **carré** (deux dents de scie à une demi-période l'une de l'autre, retranchées : les harmoniques paires s'annulent ;
« Square »), puis du bruit. **MUTATE**, sur les opérateurs qu'on entend : à gauche **PW** (la première moitié du cycle
pressée, l'onde penche, comme la distorsion de phase des Casio CZ), à droite **SYNC** (leur phase repart à chaque cycle
de la note, leur hauteur jusqu'à trois octaves au-dessus ; la marche du redémarrage corrigée, polyBLEP). **INDEX** dose
les modulateurs (avec ALGO 8, les trois tirettes du haut). **Chaque opérateur a son enveloppe**, et c'est ce qui fait
bouger la FM. **SHAPE** (0.11.1) choisit leurs **formes**, quinze dessinées d'avance : la profondeur de chaque modulateur
au fil de la note, chacun la sienne (étirée, retardée : le premier le plus vite dans un pincement, le troisième le
premier dans une montée), si bien que le timbre passe par plusieurs états. Au milieu **Flat** (INDEX seul). À droite, la
FM qui s'ouvre au trig : **Soft**, **Pluck** et **Hard** (des pincements de plus en plus forts et courts : un marteau),
**Cascade** (une bosse par modulateur, l'une après l'autre), **Bounce** (des rebonds de plus en plus petits),
**Rebound** (la chute, puis une remontée lente : le timbre s'ouvre à nouveau), **Loop** (pincé encore et encore, chaque
modulateur à son propre rythme : leurs cycles se décalent). À gauche, la FM qui grandit : **Rise**, **Swell** et
**Slow** (la note monte en 0,15, 0,6 ou 2 s, les modulateurs grandissent après elle, l'un après l'autre), **Delay** (la
note part pure, la FM attend puis s'épanouit), **Wave** (une vague sur la note), **Reverse** (la FM grandit jusqu'à la
fin de la note : à l'envers), **Breath** (une respiration en boucle). Leur temps suit DECAY (la constante de temps de la
note ; tenue, une seconde ; une montée, celui de son attaque). L'écran les dessine sur la deuxième page : chaque
modulateur de l'algorithme joué, par-dessus INDEX (le pointillé). **DECAY** de 50 ms à 20 s, Hold au bout (tenu tant que
la note l'est, relâché d'autant plus lentement que la montée est longue) ; un opérateur au-dessus de la note s'éteint plus vite (comme les
partiels aigus d'un vrai son : une lame, l'éclat d'une cloche). **MOTION** fait respirer les opérateurs : le niveau de
chacun cycle à sa propre vitesse (des rapports irrationnels entre elles, 1, 1,618, 0,764, 2,236 : le timbre ne repasse
jamais par le même état), la rétroaction du premier respire avec lui, le mélange des opérateurs entendus bouge quand il y
en a deux ou plus ; plus haut, plus profond et plus vite (de 0,12 à 2,7 Hz), les opérateurs à quelques cents les uns des
autres et dérivant lentement. Chaque trig repart du même point : la note est rejouable, la tenue ne l'est jamais. Les modulateurs
perdent 3 dB par octave au-dessus du do 4 et restent sous ce qui se replierait. Tout est calculé à deux fois la
fréquence d'échantillonnage (une sinusoïde en table, 512 points interpolés : -94 dB) et ramené par le demi-bande de
ZERO : entre les harmoniques, rien au-dessus de -55 dB, dent de scie et carré jusqu'au do 6, sync et PW compris.

| 1/3 : 2 | 1/3 : 3 | 1/3 : 4 | 2/3 : 1 | 2/3 : 2 | 2/3 : 3 | 2/3 : 4 | 3/3 : 1 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **ALGO** (1 à 8) | **RATIO** (30 accords des opérateurs) | **INDEX** | **DECAY** (50 ms à 20 s ; au bout, Hold) | **MOTION** (Off, puis les opérateurs qui respirent) | **SHAPE** (15 formes : − la FM grandit, + elle s'ouvre au trig) | **FEEDBACK** (− : carré, + : scie, puis bruit) | **MUTATE** (− : PW, + : SYNC) |

Choisir la FM met ces réglages aux siens (le deuxième algorithme, ses modulateurs à x2 et x3, un pincement sur une dent
de scie douce, en mouvement) et garde
l'OCTAVE de la piste (la FM joue la note comme Plaits). La dose d'aléatoire tire tout, sauf RATIO dans ou vers les
accords (ils sortiraient de la gamme). Une voix coûte au FM-1 environ 13 % d'un bloc (le carré +1 %, PW +2 %, la sync
+1 %) : moins que les modèles de Plaits qu'elle remplace (VIRTUAL ANALOG 15 %, les 6-OP FM 17 %, HARMONIC 31 %).

**PADS** (0.11 ; ALGORITHM après la FM, la banque MIFM1) : des **accords et leurs progressions**, joués par une nappe
de cordes. **Une seule piste la joue** (ALGORITHM la saute sur les autres tant qu'une piste l'a) : ses accords sont
ceux du projet. **PROG** choisit une progression, et **chaque trig de la piste passe à l'accord suivant** (PLAY repart
du premier). L'accord est bâti dans la **gamme du projet** (GLO : SCALE et ROOT), sur sa tonique : chaque degré empile
ses tierces dans la gamme, si bien que toutes les gammes marchent et que les accords les suivent (Pachelbel en dorien,
la Folia en Hirajoshi) ; sans gamme (Off) ou en Prog, la progression joue dans son propre mode, sur ROOT. Quand un
accord veut une couleur que la gamme n'a pas (le V majeur de la Folia, en mineur : sa sensible), sa tierce est
haussée. L'écran écrit la progression, sa place et l'accord (« Folia 2/8 E »). En **Manual**, la note jouée choisit
l'accord : son degré dans la gamme (une touche, un accord ; les pas, leurs locks de note et Endless aussi).

| PROG | Les accords | D'après |
| --- | --- | --- |
| Manual | celui de la note jouée | |
| Descent | i, i(maj7), i7, i6 : une voix qui descend par demi-tons à l'intérieur de l'accord mineur (0.16.12) | le « line cliché » des musiques de film |
| Tintin | i tenu, et au-dessus une voix qui descend par degrés depuis l'octave et remonte (les notes de l'accord, déjà là, sautées ; mineur) | Arvo Pärt, *Für Alina* (1976) : les tintinnabuli |
| Mirror | I, IV et V, une note ajoutée qui tourne autour (majeur) | Pärt, *Spiegel im Spiegel* (1978) |
| Flat II | i ♭II ♭VII i : le ♭II majeur, un demi-ton au-dessus de la tonique, hors gamme (0.16.12) | la sixte napolitaine, le phrygien des bandes originales sombres |
| Pachelbel | I V vi iii IV I IV V | le Canon en ré |
| Tritone | i ♭V i ♭VI : un accord majeur à la quarte augmentée, hors gamme (0.16.12) | le triton, inquiétant |
| Folia | i V i VII III VII i V | la Folia (Corelli, Vivaldi, Rachmaninov) |
| Lament | i VII VI V | la basse de lamentation (Purcell, *Didon et Énée*), la cadence andalouse |
| Rise | i III IV i : le IV majeur dans une tonalité mineure, qui soulève (0.16.12) | l'emprunt au dorien |
| Fifths | vi ii V I IV vii iii vi | le cercle des quintes baroque |
| Gymnopedie | IVmaj7 Imaj7 | Satie, la première *Gymnopédie* |
| Hover | i(sus2) ♭VI(sus2) ♭III ♭VII(sus4) : suspendu, rien ne se résout (0.16.12) | les nappes des musiques de film d'aujourd'hui |
| Prelude | I ii7 V7 I vi ii V7 I | Bach, le premier prélude du *Clavier bien tempéré* |
| Shift | I i ♭VI ♭VII : le majeur puis le mineur sur la même tonique (0.16.12) | l'échange de modes |
| Plagal | I IV I IV vi IV I I | l'amen des chorals |
| Dorian | i7 IV7 | le vamp dorien |
| Mediant | i ♭VI i III : le III majeur, hors gamme, une médiante chromatique (0.16.12) | les médiantes des musiques de film |
| Dub | i7 iv7 i7 VII | le dub |
| Hymn | I vi IV V I IV V I | un choral |
| Pedal | i, puis ♭VII, v, ♭VII avec la tonique dedans : des accords qui bougent sur la tonique tenue (0.16.12) | la pédale |
| Phrygian | i II i VII VI VII II i | le phrygien |
| Dominant | I ♭II ♭vii ♭II, en phrygien dominant (0.16.12) | le phrygien dominant (flamenco, musiques du Moyen-Orient) |
| Epic | i VI III VII | l'éolien des musiques de film |
| Lento | i(add9) VI i(add9) iv | un mineur lent |
| Haze | i(add9) ♭VI(maj7) iv(add9) ♭VI : lent, large (0.16.12) | les nappes ambient |
| Static | I | un bourdon : en Prog, toute la machine sur un accord |

**COLOR** : la couleur de l'accord (0.16.12 : neuf ; avant, quatre) : **Power** (la fondamentale, la quinte et l'octave,
sans tierce), **Triad**, **Sus2** et **Sus4** (la tierce remplacée par la seconde, la quarte), **7th**, **9th**, **11th**
(sans sa tierce, qui frotte contre la onzième ; avant 0.16.12, cinq notes au plus, la onzième n'entrait jamais : une
neuvième), **Add9** (la triade et la neuvième, sans septième), **Add6** (la triade et la sixte) ; toujours les notes de
la gamme, sauf sur un accord hors gamme. Les dix progressions de 0.16.12 et ces couleurs sont rangées entre celles
d'avant : un projet, un pattern ou un preset d'avant retrouve sa progression et sa couleur. Un LFO branché sur COLOR
passe maintenant par plus de couleurs (ses parts sont plus étroites). **VOICING** : Close (serré), Open (la deuxième voix du haut descend d'une octave), Wide (la
quatrième aussi), Bass (et une basse sous l'accord, sa fondamentale). Chaque accord est **conduit depuis le précédent** :
de ses renversements, celui où les voix bougent le moins, autour de la note de la piste (FREQ, OCTAVE : le registre).
**DECAY** de 0,2 à 20 s, Hold au bout (tenu tant que la note l'est ; depuis 0.15.2, joué par les pas ou Endless,
**l'accord tient jusqu'au trig suivant de la piste**, quelle que soit la GATE : un trig par mesure, un accord par
mesure) ; **ATTACK** de 1 ms à 4 s (une montée douce, en S). Chaque note de l'accord est une dent de scie (polyBLEP), et **ENSEMBLE** en fait deux choses : à droite, trois dents
de scie par note, de plus en plus écartées (une string machine, ses cordes) ; à gauche **VINTAGE**, une seule par note,
mais chaque voix un peu désaccordée et **dérivant lentement à sa façon**, comme celles d'un vieux polyphonique, de plus
en plus vers la gauche. **TONE** : à gauche une sinusoïde sous chaque note et les scies filtrées (plus rond, plus
sombre), à droite plus brillant ; un passe-bas (d'état variable, celui de Zavalishin) qui s'ouvre avec l'enveloppe.
**TEXTURE** (0.16.16, à la place de BELLS) use la nappe, d'après celles de **Boards of Canada** (des synthés anciens
volontairement désaccordés, des prises superposées sur une bande mono qui se heurtent en un chorus microtonal, saturées
puis rééchantillonnées ; voir le site) : Off à gauche, puis cinq textures, chacune sur sa part du potard, d'une touche à
tout de gauche à droite. **Bell** : la **cloche FM** sur la note du haut, une octave au-dessus (deux opérateurs, 1 : 3,5,
son index qui retombe vite, son niveau lentement). **Tape** : une bande, son **pleurage** (0,45 Hz) et son
**scintillement** (une nouvelle visée toutes les 20 ms, lissée), les mêmes sur toutes les notes (jusqu'à un tiers de
demi-ton), une saturation douce (le niveau à peu près gardé) et l'aigu usé (le passe-bas jusqu'à 0,8 octave plus bas).
**Drift** : chaque voix fausse à sa façon (jusqu'à près d'un demi-ton : un écart à elle et une dérive lente). **Dust** :
la poussière, chaque échantillon tenu sur un à cinq, de 11 à 5 bits, un souffle et, dans la seconde moitié de sa part,
un craquement (jusqu'à une dizaine par seconde). **Down** : un sinus une octave sous chaque note, le passe-bas jusqu'à
0,7 octave plus bas. L'ancien BELLS n'est pas converti : un projet d'avant lit sa valeur comme une TEXTURE.

| 1/3 : 2 | 1/3 : 3 | 1/3 : 4 | 2/3 : 1 | 2/3 : 2 | 2/3 : 3 | 2/3 : 4 | 3/3 : 1 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **PROG** (Manual, 26 progressions) | **COLOR** (Power, Triad, Sus2, 7th, Sus4, 9th, Add9, 11th, Add6) | **VOICING** (Close, Open, Wide, Bass) | **DECAY** (0,2 à 20 s ; au bout, Hold) | **ATTACK** (1 ms à 4 s) | **TONE** (− : sombre, + : brillant) | **ENSEMBLE** (− : VINTAGE, + : WIDE) | **TEXTURE** (Off, Bell, Tape, Drift, Dust, Down) |

**La gamme Prog** (GLO, SCALE après Hirajoshi) : quand une piste joue PADS, **toutes les autres pistes suivent son
accord**, en temps réel : à chaque trig de PADS, le quantiseur passe à la **gamme de l'accord**, ses notes et les notes
de passage de la gamme (celle du projet, ou le mode de la progression), moins celles qui frottent (un demi-ton
au-dessus d'une note de l'accord : la quarte sur un accord majeur). Une basse, un arpège, la dose d'aléatoire, le
clavier : tout reste dans l'harmonie. Sans piste PADS, Prog ne quantise rien.

Choisir PADS met ces réglages aux siens (Tintin, des triades ouvertes, tenues, une montée lente, un peu sombre, en
ensemble, sans texture) et garde l'OCTAVE de la piste. Elle coûte au FM-1 de 5 % d'un bloc (une triade, une dent de
scie par note) à 12 % (six notes, trois dents de scie chacune, la cloche) ; sur ses réglages, 7 %. TEXTURE sur
Down y ajoute environ 30 % (un sinus par note), sur Dust 10 %, les autres presque rien.

**POLY** (0.12 ; ALGORITHM après PADS, la banque MIFM1) : un petit synthé analogique polyphonique d'après le
**SH-101** de Roland (1982 : un oscillateur, sa dent de scie, son impulsion et sa sous-octave mélangées, un filtre à
quatre pôles, une seule enveloppe) et son héritier numérique à quatre voix, l'**AIRA S-1** (2023, sa mémoire
d'accords). **Quatre voix par piste**, chacune son oscillateur, son filtre et son enveloppe. **WAVE** passe de la dent
de scie (à gauche) à la dent de scie et au carré mêlés, au carré seul, à une impulsion de plus en plus fine (jusqu'à
12 %), puis à la **PWM** (sa largeur balancée par un LFO lent, chaque voix le sien). **SUB** : la sous-octave du
SH-101, un carré une octave dessous de plus en plus fort, puis deux octaves dessous, puis deux octaves à 25 % (ses
trois réglages, des diviseurs sur l'oscillateur). Chaque arête est corrigée par un polyBLEP du troisième ordre (une
B-spline cubique sur quatre échantillons : à l'oscillateur, rien au-dessus de -55 dB entre les harmoniques jusqu'au
do 6). Le **filtre** est celui du SH-101, le IR3109 de Roland (celui des Juno-60 et Jupiter-8) : quatre pôles en
cascade (les filtres trapézoïdaux de Zavalishin, la boucle résolue d'un coup), son entrée saturée ; **CUTOFF** de 20 Hz
à 20 kHz (il suit le clavier à 70 %), **RESO** jusqu'à l'auto-oscillation (la résonance mange un peu de grave, dont la
moitié est rendue). Une seule **enveloppe** sur le filtre et le volume : **ENV** à droite sa profondeur sur la coupure
(jusqu'à cinq octaves : le squelch des lignes acides), à gauche une **montée** (jusqu'à 2 s, le filtre s'ouvrant avec
elle) ; **DECAY** de 50 ms à 20 s, Hold au bout (tenu tant que la note l'est).

**MODE** (0.16.15, à la place de CHORD) : **Mono**, une seule voix (un trig la relance, le clavier joue la dernière
touche, revient à la précédente encore tenue quand on la lâche) ; **Poly**, une voix par trig, les précédentes
résonnent sous la nouvelle (quatre au plus, la plus discrète cédée la première), et **les accords se jouent à la main
au clavier** : chaque touche tenue a sa voix (quatre au plus, les plus récentes), lâchée elle retombe comme un trig
(DECAY à Hold : elle se relâche) ; les pas de la piste se taisent tant qu'une touche est tenue. Les accords de la
mémoire du S-1 et de l'harmonie sont passés à REV3 (son CHORD). La dose d'aléatoire ne touche pas MODE.

**VOICE** : à gauche **VINTAGE**, comme sur un vieux polyphonique : chaque voix sa propre justesse (quelques cents),
une dérive lente qui lui est propre, son filtre et son enveloppe un peu à part, de plus en plus vers la gauche ; au
milieu des voix exactes ; à droite **UNISON** : sans accord, les quatre voix sur la note, de plus en plus écartées
(un lead épais), avec un accord, ses voix légèrement désaccordées. Le niveau des voix suit leur nombre (0,6 / √voix
qui sonnent) ; depuis 0.16.5 il y glisse en quelques millisecondes : quand une note qui résonnait encore sous la
suivante s'éteignait, il sautait d'un coup, un petit clic juste après l'attaque de la nouvelle (pas en UNISON, où les
quatre voix sonnent toujours). De même dans REV3 et PADS.

| 1/3 : 2 | 1/3 : 3 | 1/3 : 4 | 2/3 : 1 | 2/3 : 2 | 2/3 : 3 | 2/3 : 4 | 3/3 : 1 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **WAVE** (Saw, Square, PW, PWM) | **CUTOFF** (20 Hz à 20 kHz) | **RESO** | **DECAY** (50 ms à 20 s ; au bout, Hold) | **SUB** (−1 oct., −2, −2 à 25 %) | **ENV** (− : une montée, + : sa profondeur) | **MODE** (Mono, Poly) | **VOICE** (− : VINTAGE, + : UNISON) |

Choisir POLY met ces réglages aux siens (une dent de scie sur sa sous-octave, un squelch, une voix par trig, un peu
de vintage) et garde l'OCTAVE de la piste. Une voix coûte au FM-1 environ 3 % d'un bloc (quatre : 11 %).

**REV3** (0.13 ; PROPHET jusqu'à 0.16.15 ; ALGORITHM après POLY, la banque MIFM1) : d'après le **Prophet-5** de
Sequential Circuits (Dave Smith et John Bowen, 1978 : le premier polyphonique entièrement programmable), et nommé d'après
sa troisième révision, la **Rev 3** (les puces Curtis).
**Quatre voix par piste**, chacune deux oscillateurs, du bruit, le filtre à quatre pôles et deux enveloppes (celle du
filtre, celle du volume). **OSC** choisit l'un de treize réglages des oscillateurs, ceux qu'on retrouve sur tous les
programmes du Prophet : **Saws** (deux dents de scie), **Saw+Sq**, **Pulses** (deux impulsions), **Fifth** (B une
quinte au-dessus), **Octave**, **Sub** (B un carré une octave dessous), **Hollow** (une impulsion carrée et un triangle
une octave au-dessus), **Thin** (une impulsion à 12 %), **Sync** et **SyncSq** (A synchronisé sur B, une quinte ou une
octave au-dessus), **Bell** (une impulsion, B à 19 demi-tons pour la FM), **Breath** (deux scies et du bruit),
**Noise**. Toutes les arêtes sont corrigées comme dans POLY (polyBLEP du troisième ordre), les coins du triangle de B
aussi (son polyBLAMP), et la marche de A à chaque redémarrage de la sync : rien au-dessus de -55 dB entre les harmoniques
jusqu'au do 6. Le **filtre** : quatre pôles comme ceux de Curtis, la boucle résolue d'un coup, son entrée saturée,
**CUTOFF** (il suit le clavier à 70 %), **RESO** jusqu'à l'auto-oscillation. **ENV** : à droite la profondeur de
l'enveloppe du filtre (jusqu'à cinq octaves, une attaque immédiate : les sons pincés), à gauche sa profondeur avec une
**attaque** : les cuivres (le filtre s'ouvre en un dixième de seconde, le volume un peu avant), puis les nappes (le
filtre en 1,5 s, le volume en 2,5). **DECAY** de 50 ms à 20 s pour les deux, Hold au bout (le volume tenu, le filtre
redescendant au tiers).

**POLY-MOD**, ce qui fait le Prophet : à gauche, l'enveloppe du filtre sur la hauteur de A (jusqu'à trois octaves ; avec
Sync, le balayage qui hurle des leads des années 80 ; ailleurs, des zaps) ; à droite, l'oscillateur B sur la fréquence
de A (une FM linéaire : avec Bell, des cloches et du métal). **VINTAGE** (0 : Clean) fait ce que fait le bouton Vintage
du Prophet-5 Rev 4 (2020) : des voix exactes d'aujourd'hui aux premières révisions instables, A et B de plus en plus
écartés (jusqu'à 14 cents), chaque voix sa justesse, sa dérive lente, sa coupure et ses enveloppes un peu à part.
**CHORD** : à droite la **mémoire d'accords** du S-1, des accords parallèles sur la note (Oct, 5th, Sus4, Major,
Minor, Min7, Min9 : les accords de la dub techno) ; à gauche l'**harmonie** : l'accord que joue PADS quand une piste la
joue (la note choisit le registre ; à chaque accord de PADS, REV3 suit), sinon celui de la gamme du projet sur la
note (une gamme majeure si elle est Off) : **Triad**, **7th**, **9th** (sans sa quinte), **Open** (fondamentale,
quinte, dixième), **Sus**, **Root5** (la fondamentale et sa quinte), **Root** (la fondamentale seule, sous la note :
une basse qui suit la progression). Au milieu **Off** : une voix par trig, les précédentes résonnant dessous (leur
niveau glisse quand l'une s'éteint, depuis 0.16.5, comme dans POLY), et depuis 0.16.15 **les accords joués à la main
au clavier**, une voix par touche tenue, comme POLY en Poly, et depuis 0.16.16 les accords des pas (voir le mode
STEP). La dose d'aléatoire ne touche pas CHORD (ses accords
parallèles sortiraient de la gamme).

| 1/3 : 2 | 1/3 : 3 | 1/3 : 4 | 2/3 : 1 | 2/3 : 2 | 2/3 : 3 | 2/3 : 4 | 3/3 : 1 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **OSC** (13 réglages) | **CUTOFF** (20 Hz à 20 kHz) | **RESO** | **DECAY** (50 ms à 20 s ; au bout, Hold) | **VINTAGE** (Clean, puis les premières révisions) | **ENV** (− : avec une attaque, + : pincée) | **POLY-MOD** (− : l'enveloppe sur A, + : B sur A) | **CHORD** (− : l'harmonie, + : une mémoire d'accords ; Off : le clavier) |

Choisir REV3 met ces réglages aux siens (deux dents de scie un peu écartées, un filtre de cuivre, une voix par
trig) et garde l'OCTAVE de la piste. Une voix coûte au FM-1 environ 4,7 % d'un bloc (quatre : 16 %). Depuis la 0.13, ZERO, les batteries, la FM, POLY, REV3 et OTIS partagent la
mémoire d'une piste (une piste ne joue qu'une machine à la fois) : passer d'une machine à l'autre la refait à neuf.

**OTIS** (0.16.14 sous le nom SQ-1, renommé en 0.16.16 ; ALGORITHM après REV3, la banque MIFM1, à la place de
CHIPTUNE et FORMANT) : un **Ensoniq de poche**, d'après le SQ-1 (1990, un VFX-SD allégé : trois voix par son, des
filtres numériques sans résonance, ses **Transwaves**), et nommé d'après sa puce, **OTIS** (l'ES5505, dont MAME documente
le fonctionnement). Ses manières, pas ses sons : ses ondes sont celles de MIFM1. Quatre voix par piste (une par trig,
les précédentes résonnent dessous).

- **La lecture** : des cycles 16 bits lus par un accumulateur de phase, linéairement entre deux échantillons, comme la
  puce. Une **Transwave** : 32 cycles de 256 échantillons qui changent peu à peu (pour une cloche, 8 boucles de quatre
  périodes, ses partiels arrondis au quart de la note : de l'inharmonique qui boucle juste). Une note lit un cycle entier
  et prend le suivant **à sa fin**, comme la puce déplaçait ses points de boucle : la Transwave avance par marches, elle
  ne se fond pas (le pas adouci sur 8 échantillons, pas plus).
- **Les Transwaves** (0.16.16 : quinze, faites à partir de recettes ; entre parenthèses, les programmes qui les jouent).
  **Huit classiques** : une résonance qui monte (Res Bass, Sweeper), une FM dont l'indice grandit (E.Piano), les modes
  d'une barre (Metal, Gong, Mallet), ceux d'un verre (Glass, Crystal), des touches, un clavier électrique et ses barres
  (Tines), les grappes d'une nappe (Pad), des voyelles a, e, i, o, u (Vox, Choir), une sync dure (Grind). **Sept plus
  granuleuses** : des cycles au hasard (un commun à tous, et le sien, de plus en plus vers la fin) lissés de moins en
  moins, d'un remous sombre à un buzz brut (Dust, Static) ; un sinus et une scie écrasés, tenus sur 1 à 16
  échantillons, sur 64 à 2 niveaux (Crusher, Click) ; un glissando dans chaque cycle, de la note jusqu'à 1 à 41
  harmoniques, dans une fenêtre en sinus (Chirp) ; le **VOSIM** (Kaegi et Tempelaars, 1978) : trois impulsions en
  sinus carré, chacune plus douce, leur hauteur le formant (3 à 28 harmoniques), puis le silence jusqu'à la fin du cycle
  (Grains) ; des harmoniques rebattues, gardées ou non d'un cycle à l'autre (moins nombreuses, plus haut), leurs signes
  au hasard (Shard) ; la note par deux sinus 2 à 32 fois plus haut, et une fois et demie autant : un fracas de
  modulateur en anneau (Ring, Perc) ; une FM rebouclée sur elle-même (de 0,3 à 5) : son sinus, puis un cri, puis un
  bruit qui tient, le même à chaque cycle (Chaos, Machine). Le repli, l'impulsion qui s'affine, l'escalier et deux des
  trois FM de 0.16.14 sont partis.
- **Où elles vivent** : dans la mémoire de la voix Plaits de la piste (16 Ko de cycles, 2 Ko pour la ligne de SPACE),
  prêtée tant que la piste joue OTIS. Changer de programme refait la Transwave **un peu à chaque bloc** (environ 5 % du
  FM-1 pendant ce temps, le cycle joué en premier) : la première note est là en quelques millisecondes, la Transwave
  entière en 0,1 à 0,4 s ; deux programmes sur la même Transwave (Res Bass et Sweeper, par exemple) n'attendent rien.
- **Les filtres** : ceux d'OTIS, des pôles simples sans résonance : deux ou quatre passe-bas, un passe-haut ; COLOR
  les règle, le clavier et l'enveloppe les ouvrent un peu. Les ondes les plus brillantes replient à l'aigu (au-dessus
  de do 5), comme sur l'original ; le passe-bas descend alors sous le repli.

| WAVE | INDEX | SWEEP | MOTION | COLOR | SHAPE | STACK | SPACE |
| --- | --- | --- | --- | --- | --- | --- | --- |
| le programme (24) | où la note démarre dans la Transwave | une enveloppe l'y déplace (milieu : rien ; à droite vers le haut, à gauche vers le bas) | un LFO dessus (sa vitesse et sa profondeur ensemble ; après 50 % un léger vibrato, après 70 % instable) | à gauche plus sombre (les passe-bas se ferment), Open au milieu, à droite plus fin (le passe-haut monte) | l'enveloppe : Perc, Pluck, Keys (basses, claviers), Swell, Pad ; la durée du balayage avec | la deuxième couche (sa part, son désaccord : ailleurs dans la Transwave, à l'octave ou à la quinte), puis la troisième (un clic, un souffle, un tintement) | un chorus, puis l'effet du programme : phaser, peigne accordé sur la note, saturation |

Les **24 programmes**, trois par famille (0.16.16) : **Grit Bass** (Res Bass, Crusher, Chaos), **Electric** (E.Piano,
Tines, Shard), **Metallic** (Metal, Ring, Gong), **Glass** (Glass, Chirp, Crystal), **Voices** (Vox, Grains, Choir),
**Evolving Pad** (Pad, Dust, Sweeper), **Industrial** (Grind, Static, Machine), **Percussive** (Perc, Mallet,
Click). Chacun choisit sa Transwave, ses couches, l'échelle de son enveloppe, le sens de son balayage
(une pincée retombe vers INDEX, une nappe monte depuis), ses pôles et son effet ; leurs niveaux sont égalisés. Le
premier écran dessine **le cycle que la voix lit**, en direct, et sa place (« Electric · 13 / 32 »).

Au clavier, OTIS joue les accords à la main (0.16.15) : une voix par touche tenue, quatre au plus, chacune relâchée avec
sa touche ; les pas de la piste se taisent tant qu'une touche est tenue ; depuis 0.16.16, il joue aussi les accords des pas. Choisir OTIS met ses réglages aux siens
(E.Piano, un peu de balayage, un chorus). Une voix coûte au FM-1 environ 3 %
d'un bloc avec ses trois couches, SPACE à fond et le reste environ 5,5 % (quatre voix, trois couches, SPACE à fond :
environ 19 % ; REV3, quatre voix : 16 %). Les projets et les sons enregistrés d'avant qui jouaient CHIPTUNE ou FORMANT passent à OTIS avec ses
réglages ; les autres modèles de Plaits sont renumérotés.

**TAURUS** (0.16.20 ; ALGORITHM après OTIS, la banque MIFM1) : une **basse monophonique**, une voix par piste, d'après
les pédales **Taurus** de Moog (le modèle 205A, 1975-1981, dessiné par David Luce) telles que leur manuel les décrit, et
le **Minimoog** (1970). Du Taurus : deux oscillateurs en dent de scie, B contre A (son intervalle, un battement de
quelques cents, leur dosage), l'**échelle à transistors** brevetée par Moog (24 dB/octave, sa résonance, EMPHASIS,
jusqu'à l'auto-oscillation), un **contour** sur la coupure qui monte et retombe quoi que fasse la note (« non
sustaining », 5 ms d'attaque), un contour de volume et son interrupteur **DECAY** (allumé : le son s'éteint au rythme
du decay quand on lâche), le **glide** (un retard de condensateur, jusqu'à une demi-seconde). Du Minimoog : ses **six
formes d'onde** (triangle, triangle-dent de scie, dent de scie, carré, rectangle large, rectangle étroit), son
**troisième oscillateur** une octave sous les deux autres (la sous-octave, de la même forme), et son **mixer** qu'on
pousse dans le filtre pour le saturer.

- **L'échelle** : le modèle de D'Angelo et Välimäki (celui du filtre LADDER de MIFM1) : une paire de transistors, sa
  tangente hyperbolique, à chacun des quatre étages, accordée sur sa propre boucle depuis 0.16.30 (voir LADDER, plus bas). Une vraie échelle perd ses graves quand sa résonance monte (la
  boucle les retranche) : ici l'entrée est poussée d'autant, presque tout est **rendu** (à 90 % d'EMPHASIS, la
  fondamentale d'un la 1 sous une coupure à 400 Hz garde 82 % de son niveau, et la saturation fait le reste).
  La coupure suit la note à moitié (TUBA un tiers, les ondes du Minimoog deux tiers).
- **Les oscillateurs** tournent librement (jamais en phase) et dérivent d'un ou deux cents, chacun le sien ; leurs
  fronts sont anti-repliés (polyBLEP). Dans TAURUS, B est une octave au-dessus : la fondamentale n'est qu'à A, solide,
  et B bat avec la deuxième harmonique d'A (le tourbillon lent, sans que les graves se creusent). Dans BASS, les deux
  sont à 16 pieds ; dans TUBA, B est une octave dessous.

| VOICE | BEAT | MIX | CUTOFF | EMPH | CONTOUR | DECAY | GLIDE |
| --- | --- | --- | --- | --- | --- | --- | --- |
| les trois presets du Taurus (**Taurus** : le rugissement qui retombe en ronronnement, tenu ; **Bass** : pincé, le volume retombe avec DECAY ; **Tuba** : les graves remplis, B une octave dessous, une attaque douce), puis les six ondes du Minimoog (**Tri**, **Shark**, **Saw**, **Square**, **Wide**, **Narrow**), son troisième oscillateur dessous | B contre A, de 0 (Locked) à 50 cents | jusqu'au milieu B monte au niveau d'A (A=B), au-delà les deux sont poussés dans l'échelle (Hot : elle sature) | 30 Hz à 11 kHz | la résonance, chante seule au bout | à droite sa profondeur (jusqu'à 7 octaves), à gauche une montée lente du volume et du filtre ensemble (Sw) | les deux contours et le relâchement, 50 ms à 2,8 s | 0 (Off) à 0,5 s |

Choisir TAURUS met ses réglages aux siens : le preset Taurus, B qui bat de 6 cents, A=B, la coupure basse, un peu de
résonance, le contour profond, un decay d'une demi-seconde. Une voix coûte au FM-1 environ 6 % d'un bloc. Les projets
et les sons enregistrés avec les 0.16.18 et 0.16.19 se relisent : TAURUS prend la place 16, SPEECH et les autres
modèles de Plaits passent un cran plus loin.

**VOCODER** (0.16.21 ; ALGORITHM après TAURUS, la banque MIFM1) : un **vocodeur à canaux** et son propre synthé,
d'après le **Vocoder Plus VP-330** de Roland (1979 : un vocodeur à 10 bandes, des cordes, un chœur, un ensemble à
lignes à retard). La **voix** (le modulateur) est une autre piste, choisie avec MOD (T1 à T4, prise avant le mixer :
son volume à zéro, on ne l'entend plus qu'à travers le vocodeur ; SPEECH y est à sa place). La **porteuse** est le
VOCODER lui-même : quatre voix par piste, jouées par les pas, leurs accords, le clavier ou une progression (PLAY).

- **PLAY** (0.16.28, à la place de FREQ, le premier potard de la page : sur le VOCODER, il ne transpose plus, TRANSP
  et OCTAVE le font) : un sélecteur qui part du milieu. À **gauche**, **Mono** : une seule voix, la dernière reprise à
  chaque trig (sa fin tenue par RELEASE devient l'attaque de la suivante), le clavier joue une note. Au **milieu**,
  **Poly** (le réglage d'avant, celui d'un projet ou d'un son enregistré avant la 0.16.28) : la note du trig et
  l'accord du pas, ou les touches tenues, quatre voix. À **droite**, un cran par accord : **Manual** (la touche ou la
  note du pas choisit l'accord de la gamme, son degré, comme PADS) puis les **26 progressions de PADS** : à chaque
  trig (ou chaque touche) l'**accord suivant**, construit comme PADS le construit (dans la gamme du projet, ou dans le
  mode de la progression si SCALE est sur Off ou Prog ; sa triade et les notes que la progression ajoute, quatre au
  plus) et **placé au plus près du précédent**, chaque voix bougeant le moins possible, autour de la note jouée.
  Chaque piste a sa place dans sa progression : deux VOCODER, ou un VOCODER et PADS, peuvent jouer la même ou une
  autre. Un nouveau départ (PLAY du séquenceur) repart du premier accord. La dose d'aléatoire ne touche pas à PLAY,
  un lock ou un câble oui (une progression par pas, si on veut).

- **La porteuse** : trois dents de scie par voix (anti-repliées), et TONE qui va des **scies** grasses (désaccordées de
  18 cents, la troisième dessous) aux **cordes** (6 cents, la troisième une octave au-dessus : le 4 pieds, adoucies)
  puis au **chœur** (3,5 cents, plus sombre, les formants d'un « a » : 650, 1 080 et 2 650 Hz). ATTACK et RELEASE font
  son enveloppe.
- **Le vocodeur** : 12 bandes d'une demi-octave, de 130 Hz à 6,3 kHz (deux filtres d'état de Chamberlin de Q 2,2
  chacune : l'octave d'à côté 21 dB plus bas). La voix est analysée au huitième ou à la moitié de la fréquence
  d'échantillonnage selon la bande, la porteuse filtrée à la moitié ou à plein régime : c'est ce qui le rend abordable.
  Les pôles de chaque filtre sont placés là où ils doivent être pour sa bande et sa fréquence, son gain ramené à 1 en
  son centre (la formule habituelle ne tient que loin sous la fréquence d'échantillonnage). Le niveau de chaque bande
  de la voix est suivi (2 ms de montée, FOLLOW pour la retombée, puis **Hold** qui fige le timbre) et donné à la même
  bande de la porteuse. Un **gain automatique** garde le résultat au niveau de la porteuse : une voix chuchotée ou
  criée, une boucle de batterie, le même volume, seule leur forme compte. **SHIFT** décale les bandes de la voix
  jusqu'à trois (une octave et demie) vers le bas ou le haut : une voix plus grave ou plus petite. **AIR** laisse
  passer les aigus de la voix (au-dessus de 4,5 kHz : les « s » et les « t », plus intelligibles).
- **L'ensemble** : trois prises sur une ligne de 6 ms balayée par deux LFO (0,63 et 5,9 Hz), décalées d'un tiers de
  tour, comme les ensembles à lignes à retard des machines à cordes ; ENSEMBLE dose sa profondeur et son mélange.

MOD sur Off : la porteuse seule, une machine à cordes à quatre voix.

| PLAY | MOD | TONE | ENSEMBLE | ATTACK | RELEASE | FOLLOW | SHIFT | AIR |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Mono**, **Poly**, **Manual**, puis les 26 progressions de PADS | **Off**, ou la piste qui parle (**T1** à **T4**) | **Saws**, **Strings**, **Choir**, et entre les deux | la profondeur et le mélange de l'ensemble, 0 à 100 | 3 ms à 3 s | 30 ms à 6 s | la retombée des bandes, 10 ms à 1 s, puis **Hold** | les bandes de la voix décalées, −3,0 à +3,0 | les aigus de la voix, 0 à 100 |

Choisir VOCODER met ses réglages aux siens : MOD sur Off (à tourner vers la piste qui parle), les cordes, l'ensemble à
70, une attaque de 27 ms, un relâchement d'un tiers de seconde, FOLLOW à 40 ms, AIR à 35. Il coûte au FM-1 environ
4,5 % d'un bloc avec une voix, 7 % avec quatre, et ses bandes 12 % de plus quand MOD prend une piste (quatre voix
vocodées : environ 19 %, autant qu'OTIS). Les projets et les sons enregistrés avec la 0.16.20 se relisent : VOCODER
prend la place 17, SPEECH et les autres modèles de Plaits passent un cran plus loin.

**STATIC** (0.16.24 ; refaite en 0.16.27 ; ALGORITHM après VOCODER, la banque MIFM1) : une machine d'**événements
électriques**, une voix par piste, pour des séquences minimales, percussives et instables. Son plan est **ARC/01**, le
cahier des charges DSP que Yoann a écrit en novembre 2026 pour un instrument de phénomènes électriques (fréquences pures,
modulation croisée, instabilité contrôlée, événements percussifs), d'après **Pan Sonic** et **Ryoji Ikeda**. Sa règle
centrale : le contraste entre le son pur et le son détruit compte plus que la quantité de destruction. Ses fréquences sont
en Hz, ses temps en ms, son index en radians ; le silence compte autant que le timbre. ARC/01 a trois générateurs ;
STATIC en joue **un par voix**, choisi par **MODE** (le deuxième potard de la première page), qu'un pas peut locker :
trois pistes de STATIC sont les trois lanes d'ARC/01, une seule piste peut passer de l'un à l'autre à chaque pas. Les
routes d'ARC/01 entre eux passent par le **LFO FOLLOW** (0.16.23 : l'enveloppe d'une piste) et les **câbles** : la
salve de SPARK qui ouvre l'INDEX d'ARC, un S&H de LFO qui fait sauter son MOD.

- **BODY** (la masse) : un sinus en phase continue, une octave sous la note (le sub : 65 Hz pour un Do 3), dont la
  hauteur part de **PITCH** demi-tons au-dessus (jusqu'à +48) et tombe vers la note en **SWEEP** (2 à 120 ms). Deux
  chemins mélangés par **MIX** : **PURE**, auquel rien n'est fait (sa fondamentale gardée, ses harmoniques sous −100 dB
  au test), et **DRIVE** (une saturation douce, de x1 à x20, au même niveau de crête). **CLICK** ajoute un clic de 3 ms
  (les aigus d'un bruit) ; **ATTACK** de 0,2 à 10 ms ; **DECAY** de 20 ms à 2 s, puis **Hold**.
- **ARC** (le cœur timbral) : une porteuse à la note **modulée en phase** par un modulateur à sa propre fréquence,
  **MOD**, de 0,1 Hz à 8 kHz, jamais un rapport de la porteuse : sortie = sin(2π φC + INDEX · m + FEEDBACK · lp(sortie
  précédente)). **INDEX** de 0 à 15 radians (à 0, un sinus pur ; à 3, un métal clair) ; **FEEDBACK** de 0 à 2, la
  sortie précédente passée dans un passe-bas à 3 kHz et bornée (la boucle peut être brutale pour le timbre, jamais pour
  l'amplitude) ; **RING**, le produit porteuse × modulateur mélangé ; **DRIFT**, une dérive lente des deux fréquences
  jusqu'à 30 cents, puis des sauts du modulateur au hasard (jusqu'à ±7 demi-tons, 4 par seconde) ; **DRIVE**, une
  saturation asymétrique (son continu retiré après) ; **DECAY** de 20 ms à 4 s, puis Hold. Les changements d'INDEX, de
  FEEDBACK et de RING glissent sur un bloc (pas d'escalier).
- **SPARK** (les événements électriques) : pas un bruit continu, des **événements**. Chaque trig lance une salve de
  **CLUSTER** coups (1 à 9) à intervalles irréguliers (3 à 30 ms) ; tant que la gate est ouverte (LENGTH du pas, qu'on
  peut étirer sur plusieurs pas), **DENSITY** en ajoute au hasard, jusqu'à 30 par seconde. Chaque coup est une
  enveloppe courte (**DECAY** de 1 à 400 ms, puis Hold : un bruit continu tant que la gate est ouverte) sur une
  **SOURCE** : **White** (un bruit blanc, que HOLD colore), **S/H** (un bruit tenu par un sample and hold à **HOLD**,
  de 20 Hz à 18 kHz : du crépitement lent, comme l'ECHON 6 de Morphor, au souffle), **Impulse** (une impulsion, HOLD sa
  largeur) ou **Train** (des impulsions à HOLD). Puis un filtre, **FILTER** de 100 Hz à 16 kHz, que la note déplace
  (Do 3 : la valeur affichée), et **Q** : à gauche un **passe-haut** (Q jusqu'à 8), à droite un **passe-bande** (jusqu'à
  12), leurs niveaux rattrapés (un passe-bande étroit garde peu d'un bruit, une impulsion le fait sonner).

| MODE | 1 | 2 | 3 | 4 | 5 | 6 | DECAY |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **Body** | PITCH +0 à +48 st | SWEEP 2 à 120 ms | MIX Pure à Drive | DRIVE x1 à x20 | CLICK | ATTACK 0,2 à 10 ms | 20 ms à 2 s, Hold |
| **Arc** | MOD 0,1 Hz à 8 kHz | INDEX 0 à 15 rad | FEEDBACK 0 à 2 | RING | DRIFT (30 cents, puis des sauts) | DRIVE | 20 ms à 4 s, Hold |
| **Spark** | SOURCE (White, S/H, Impulse, Train) | HOLD 20 Hz à 18 kHz | FILTER 100 Hz à 16 kHz | Q (HP à gauche, BP à droite) | DENSITY 0 à 30/s | CLUSTER x1 à x9 | 1 à 400 ms, Hold |

Tourner **MODE** met les sept autres potards au **point de départ** de ce générateur, les trois presets de test
d'ARC/01 : BODY, *PURE IMPACT* (+18 demi-tons en 28 ms, 15 % de drive, un clic léger, 240 ms) ; ARC, *COPPER ARC* (MOD
247 Hz, INDEX 3 rad, FEEDBACK 0,18, un drive moyen, 450 ms) ; SPARK, *RELAY ROOM* (S/H à 1,6 kHz, un passe-bande à 2,8
kHz, Q 4, un coup par trig, 12 ms). Un lock de MODE sur un pas ne touche pas aux potards. La dose d'aléatoire ne touche pas à MODE (0.16.28). Choisir STATIC le met en BODY.
Son hasard est une graine fixe : le même projet joué de la même façon rejoue les mêmes événements. Une voix coûte au
FM-1 environ 4,2 % d'un bloc en BODY, 4,3 % en ARC, 3,7 % en SPARK (tenue). SPACE (0.16.24) n'existe plus : l'envoi
de la piste vers la réverbe reste sur la page du mixer. Les projets enregistrés avec la 0.16.23 et les sons des
0.16.21 à 0.16.23 se relisent : STATIC prend la place 18, SPEECH et les autres modèles de Plaits passent un cran plus
loin ; un projet qui jouait l'ancienne STATIC garde ses valeurs, qui veulent dire autre chose.

L'écran est noir, ses lumières aux couleurs de Mutable Instruments (bleu-vert, orange, rose, celles de leurs
icônes). En haut, l'icône du module dans un anneau de sa couleur, son nom en grand et une ligne sur son état. Puis
les quatre potards côte à côte, dans l'ordre des KNOB 1 à 4 : un anneau fin allumé de son début (ou de son milieu,
avec « − + ») jusqu'à la valeur, la valeur dedans, un câble en pointillé et son point d'arrivée dans l'autre couleur,
la source écrite dessous. En bas les quatre pistes, une LED chacune allumée par sa gate dans la couleur de son modèle,
celle qu'on joue en défonce (chaque case sous son potard, le modèle en 4 lettres : KICK, FM, PADS, MODL…) ; sur les pages des LFO, les trois
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
| **ALGORITHM** | le type de ce qu'on a devant soi, là où il y a un choix (le A cerclé en haut) : sur le son de la piste, son modèle (les 20 : les batteries, les batteries FM, ZERO et la FM, Plaits) ; sur son filtre, le modèle de filtre (OFF, LADDER, MS-20, SEM, VOX) ; sur Warps, d'un repère d'ALGO au suivant ; sur le delay, SYNC (vers la gauche) ou FREE (vers la droite) |
| **PRESETS** | la piste active, de 1 à 4 (les presets eux-mêmes : EDIT tenu) |
| **EDIT** | **tapé** : les trois ensembles de la piste active, l'un après l'autre : son **SON** (FREQ, HARMO, TIMBRE, MORPH ; SELECT : GATE, TRANSP, OCTAVE, F.KEY), son **BEND** (les bits : CLOCK, BITS, MASK, SHAPE ; SELECT : le glitch, GLITCH, SIZE, BEND, DROP), son **FILTRE** (TYPE, CUTOFF, RESO, ENV ; SELECT : DRIVE, NOISE, COLOR, FM), puis de nouveau le son ; **tenu : les presets** de la piste (touches noires : la banque, A celle d'usine ; blanches : le son, posé sur la piste et joué ; SAVE tenu : l'y garde ; REC tenu : l'efface) |
| **LFO** | les trois LFO, l'un après l'autre (SELECT aussi) |
| **SEQ** | tapé : les pas de la piste (NOTE, PROB, SPEED, LENGTH ; puis RATCHETS : RATCH, SPAN, CURVE, DIR ; puis CLIMB : CLIMB, NOTES, ON, SHAPE ; puis RANDOM, puis EUCLID) ou de l'effet (DECAY, PROB, DEPTH, LENGTH) ; **tenu : les patterns** (touches noires : la banque, blanches : le pattern, plusieurs : un enchaînement ; REC tenu : efface) |
| **ARP** | Endless, celui de la piste (voir plus bas) ; à nouveau : ses options (STRIDE, REST, HOLES, LEAP) |
| **HOME** | le mixer : les niveaux des 4 pistes, leurs pans, leur SPREAD, leurs filtres (EQ), le DUCK, puis le bus master (COMP, OTT) ; ALGORITHM sur COMP ou OTT : le mode du bus (OFF, COMP, OTT) |
| **FX** | les effets l'un après l'autre : le delay, la réverbe, Warps ; SELECT parcourt leurs faces (leurs potards, les envois des pistes, leurs pas) |
| **ENV** | tout ce qui est enveloppe sur la piste active : l'enveloppe de Plaits (DECAY, LPG COLOUR, TIMBRE ENV, MORPH ENV) ; ENV encore (ou SELECT) : P.ENV et l'enveloppe du filtre (F.ATK, F.DECAY, PING) |
| **PLAY** | le séquenceur tourne ou s'arrête ; **deux fois de suite** (STOP STOP, en moins de 0,4 s) : arrêté, et tout le son coupé, les queues du delay et de la réverbe comprises |
| **REC tenu + PLAY** | (0.16.31) **l'enregistrement en direct** de la piste active, ou son arrêt (voir plus bas) |
| **GLO tenu + PLAY** | (0.16.31) le **métronome** de l'enregistrement en direct, allumé ou éteint (allumé au démarrage) |
| **REC** | **KEY** ↔ **STEP** (voir plus bas) ; sur la page d'Endless, l'enregistrement ; pendant l'enregistrement en direct, son arrêt (les pas continuent) |
| **clavier** | en KEY, joue la piste active, même à l'arrêt, avec sa TRANSP et son OCTAVE ; **OCT− / OCT+** pour l'octave du clavier (en STEP : la page de pas) ; en MOD, les trois premières touches blanches sont les LFO |
| **OCT− tenu + SELECT** | la **dose d'aléatoire**, sur son écran (voir plus bas) |
| **OCT+ tenu + SELECT** | **TRAUMA**, la dose de saturation, sur son écran (voir plus bas) |
| **SEL** | le mode MOD (voir plus bas) ; **tenu** : la matrice de la piste |
| **SAVE tenu** | sauvegarde le pattern dans sa case (à l'arrêt) ; EDIT tenu, avec une touche blanche : le son de la piste dans ce preset |
| **GLO** | TEMPO, SCALE, ROOT, SWING ; SELECT : le projet (INIT) et la machine ; GLO encore : retour |
| **GLO tenu + potard** | **ALL** : le même réglage sur les 4 pistes (ex. DECAY) ; GLO tenu + ALGORITHM : le même modèle. Tant que GLO est tenu, l'écran passe en noir sur la couleur du thème. Les réglages restent ; pendant 4 s (cinq carrés jaunes en bas), **GLO** les remet comme avant ALL au début de la mesure suivante (GLO encore avant la mesure : ils restent) |
| **ENV tenu + potard** | **RND** : l'aléatoire de ce réglage (avec GLO tenu aussi : sur les 4 pistes) |
| **REC tenu** (pas) | efface la piste de pas affichée : ses trigs, ses locks, ses aléatoires (et les câbles d'une piste) |
| **GLO + REC tenus 3 s** | **ERASE ALL**, depuis n'importe quelle page : tout revient comme dans un projet neuf (INIT) sauf **les quatre sons** (leurs modèles et leurs réglages : le son, l'enveloppe de Plaits, la gate, la hauteur), **le tempo, la gamme et sa tonique** : plus de pas, de locks, d'aléatoires (aucun potard RND), de câbles ni d'Endless ; les niveaux comme neufs, les pans au centre, pas de filtre (ni l'EQ, ni le filtre de piste : OFF), aucun envoi, le circuit bending coupé, les LFO, les effets (delay, réverbe, Warps coupé), le duck et le bus master comme neufs, pas de swing, la dose d'aléatoire et TRAUMA à 0. La sortie, la qualité de Warps et le thème restent |
| **REC + SEL tenus 1,5 s** | efface **tous les câbles** (les modulations des LFO, de toutes les pistes et globales) |

Les appuis tenus (SAVE, REC pour effacer ou charger, SEL pour la matrice) affichent en bas ce qu'ils vont faire et
cinq carrés qui se vident un à un : lâcher avant le dernier annule. Ceux qui effacent ou écrasent durent une seconde.

La charge de calcul est toujours affichée en haut à droite (le cœur le plus chargé, en %) ; GLO puis SELECT
détaille ce que coûte chaque piste, Warps et les effets (FX : le mixer, le delay, la réverbe et le bus master). En
bas de cette page, le pire bloc de la dernière seconde (celui du pic) : ce qu'y ont pris les deux cœurs et le rendu
entier, puis chaque piste, Warps et les effets dans ce bloc-là, et la préparation du cœur 0 en détail (le début et les
LFO, les câbles, le séquenceur et ses trigs, les pistes des effets, les réglages des voix) : pour savoir ce qui
fait monter le pic (une étoile après une tâche : c'est le cœur 1 qui l'a jouée). Les
quatre voix, Warps et les effets se répartissent entre les deux cœurs selon ce que chacun coûte, mesuré bloc après
bloc (une voix MODAL coûte plus de deux fois une voix de FM) : le mixer, les effets et le bus master jouent pour
cela un bloc derrière les voix (2,9 ms de latence en sortie), pendant que les voix jouent le bloc suivant : le cœur 1
les commence dès le début du bloc, pendant que le cœur 0 le prépare (le séquenceur, les LFO, les câbles : « prep »
sur la page machine), puis les deux cœurs se partagent les voix : le cœur 0 propose les siennes au cœur 1 dès que le bloc est prêt, et
le cœur 1 les prend à la fin de son premier travail, sans attendre que le cœur 0 ait fini une de ses voix pour les
lui passer (sur le stress test, cette attente faisait monter le pic de 70 à 96 %). Une voix silencieuse depuis un moment, sa note finie, se repose jusqu'à son
trig suivant (elle ne coûte plus rien).

**Le code en RAM** (0.14.4) : le FM-1 exécute son programme depuis la flash, à travers un petit cache ; du code qui
n'y est pas encore attend la flash, une ligne de 32 octets à la fois (environ 1 µs). Sur le stress test, la première
piste FM d'un bloc payait ainsi ~170 µs pour charger le code de la FM, la même FM sur la piste suivante presque rien
(mesuré : T2 passée sur un autre moteur, T3 montait de 26 à 32 %) ; un compte d'instructions ne le voit jamais, d'où
une machine 20 à 40 % plus chargée que ce que l'émulateur annonçait. Ce que toutes les pistes exécutent à chaque bloc
tourne donc depuis la RAM que le FM-1 garde pour le code (32 Ko, copiés au démarrage) : la chaîne d'une voix (son
rendu, ses câbles, ses bits, TRAUMA), les quatre filtres et la batterie MIFM1 (`o2_ram`, `firmware/fm1.toml`). Les mêmes
instructions, le même son, échantillon pour échantillon. Mesuré sur la machine, le stress test : les cœurs de 95 et
89 % à 78 et 72 %, son pic de 120 % (G3, des blocs en retard, des grésillements) à 99 % (G1), plus rien n'y grésille ;
les effets aussi s'allègent (35 à 30 %), leur code plus souvent dans le cache. L'émulateur annonce de nouveau ce que
la machine fait (75 % par cœur). Depuis 0.14.5, la FM aussi : son rendu compilé pour la taille (ses réglages, ses
enveloppes, une fois tous les 8 échantillons : 18,5 Ko déroulés, 5 Ko ainsi, 3 % d'instructions en plus, les mêmes
échantillons), ses six boucles à part, 11,6 Ko en tout ; la zone de code en RAM passe à 42 Ko (10 Ko pris au pool,
qui en avait de trop). Depuis 0.15.1, ce qui tourne hors du son (les textes des potards à l'écran, la lecture d'un projet
ancien, les démos) est compilé pour la taille aussi (`MI_COLD`, `firmware/mi/bridge.h`) : une quinzaine de Ko de flash
rendus aux presets, le code du son inchangé. En 0.16.1, les seize moteurs de Plaits que MIFM1 ne joue plus depuis la
0.9 (VA, waveshaping, FM, additif, wavetable, accords, le six-opérateurs, string machine, wave terrain, phase
distortion, VA-VCF, ses trois batteries) sortent du firmware : un moteur muet tient leurs places (les huit joués gardent
leurs numéros), une cinquantaine de Ko de flash et 26 Ko de mémoire rendus (l'arène du son passe de 280 à 260 Ko ; le
projet de travail et le tampon de lecture des anciens projets passent dans le pool, hors de la RAM).

**Le garde-fou** (0.8.2) : quand un bloc dépasse 92 % de son temps, la FM des filtres s'allège pendant 0,3 s (ses
coefficients recalculés un échantillon sur deux) ; quand un bloc arrive en retard (plus de 100 % : un clic), un sur
quatre ; deux retards en 0,3 s, la FM des filtres coupée ; puis un cran de moins toutes les 0,3 s. Quand la machine est
si pleine, le son bouge de partout : une FM un peu plus légère un instant ne s'entend pas, un clic si. La page machine
l'affiche devant le pic (G1, G2, G3). Ce qu'il gagne reste modeste (sur le stress test avec la dose à 100 %, compté :
3, 5 et 6 points par cœur) : il évite les clics au bord, il ne remplace pas une machine moins chargée.

**Le mode STEP** (REC) : les 16 touches blanches sont les pas de la page (allumés : un trig, celui qui joue
clignote), les **7 premières touches noires choisissent la piste de pas** : 1 à 4 les pistes, 5 le delay, 6 la
réverbe, 7 Warps (l'écran passe sur la piste ou l'effet choisi) ; OCT− et OCT+ changent de page (16 pas chacune,
jusqu'à 64). Appuyer sur un pas pose un trig ; appuyer sur un trig l'enlève (avec ses locks). **Tenir un pas et
tourner un potard pose un P-lock** sur ce pas : pour une piste, sur n'importe quelle page qui est la sienne (Plaits,
son niveau, ses envois, son pan, sa note…) ; pour un effet, sur ses potards. Le potard montre alors la valeur du pas
(LOCK dessous). Tenir un pas et taper SEL efface ses locks. **Tenir un trig et appuyer sur un pas plus loin**
(0.16.15) lui donne sa **longueur** : la note tient jusqu'à ce pas (sur ce pas, la part qu'en donne GATE), une barre
la montre sur les pas qu'elle couvre ; un trig qui a des **ratchets** les étale sur ces pas à la place (ses coups sur
autant de pas, SPAN ; l'écran dit lequel). Un pas avant le trig le ramène à son seul pas. La longueur est un lock du
pas (effacée avec les autres). Depuis 0.16.31, **les pas qu'une note longue tient s'allument aussi sur le clavier**, à
mi-intensité (le troisième niveau des LED du FM-1, entre éteint et allumé : ses touches n'ont qu'une couleur), les trigs
restant pleins. **Tenir un trig et appuyer sur REC** (0.16.16) montre **ses notes** : l'écran
dessine un clavier de deux ou trois octaves, les notes du pas pleines, et le nom de l'accord (« STEP 5  Cm7 ») ; une
touche du clavier ajoute sa note ou l'enlève (on l'entend ; quatre au plus, une au moins), les potards 1 à 4 déplacent
chacune d'un demi-ton ; **REC** à nouveau revient aux pas. POLY en Poly, REV3 sans accord et OTIS jouent tout
l'accord ; une machine à une seule voix joue la plus grave, l'écran dit « MONO: THE LOWEST » et dessine les autres en
pointillé. NOTE reste la plus grave, les autres sont des locks du pas (des intervalles au-dessus d'elle, jusqu'à huit
octaves), effacés avec les autres ; un trig joué au clavier ou par Endless n'a pas d'accord. La piste de pas suit la page : aller sur le delay (FX) montre les pas du delay.
**REC** à nouveau : retour au mode KEY.

**L'enregistrement en direct** (0.16.31) : **tenir REC et appuyer sur PLAY**, puis jouer au clavier. Chaque note tombe
sur le pas le plus proche de la piste active, avec sa hauteur (sa NOTE, comme le dirait la vue des notes d'un pas) ;
**juste avant un pas**, elle y est écrite et ce pas ne la rejoue pas quand le séquenceur y arrive (on l'a déjà
entendue) ; des touches pressées dans le même pas font un **accord** (quatre notes au plus) ; une touche **tenue** sur
plusieurs pas donne sa **longueur** à la note (comme un trig tenu avec un pas plus loin en STEP). On enregistre par-dessus
ce qui est là : un pas joué remplace ses notes, les autres pas restent, et les autres locks du pas aussi. **PLAY passe au
rouge** et REC clignote ; **REC** à nouveau arrête l'enregistrement (les pas continuent), PLAY arrête tout. Depuis
l'arrêt, le **métronome compte une mesure** avant le premier pas : un ping à chaque temps (do 6), une quinte plus haut sur
le premier de la mesure, ~25 ms, au casque et au haut-parleur seulement (ni dans les effets ni sur l'USB, qui enregistre le mix sans lui) ; il bat ensuite tant qu'on enregistre.
**GLO tenu + PLAY** l'allume ou l'éteint (éteint : l'enregistrement part tout de suite). Le pas d'arrivée suit la
SPEED de la piste ; dans une autre DIRECTION qu'en avant, une note va toujours au pas qui joue. Une piste en Endless ou
en ARP joue les touches à sa manière : rien ne s'écrit sur ses pas.

Les pages d'une section se suivent sous SELECT (ou en retapant son bouton) ; depuis 0.16.6, une barre sous l'en-tête
les montre, une part par page, celle qu'on regarde allumée.

**Les pas d'une piste** (SEQ) : **NOTE**, **PROB** (probabilité), **SPEED** et **LENGTH** (le nombre de pas) ; NOTE et
PROB se lockent aussi pas par pas. **SPEED** (0.16), la vitesse de la piste : x1/8, x1/4, x1/2, x3/4, x1, x3/2, x2,
x3, x4 (x3/4 et x3/2 pour du ternaire, x1/2 une piste qui traîne sous les autres). Tournée en jouant, la piste repart
aussitôt à sa nouvelle vitesse depuis le pas où elle est (avant 0.16.6, revenir de x2 à x1 la figeait aussi longtemps
qu'elle avait joué, et descendre à x1/2 de même).

**RATCHETS** (0.16.6 : SEQ, la deuxième page des pas ; avant, la face PLAY, la dernière) : **RATCH**, 1 à 16 coups
par trig, répartis sur **SPAN**, de 1 à 16 pas (RATCH 3, SPAN 4 : trois coups sur quatre doubles croches, un triolet
de noires ; RATCH 8, SPAN 16 : huit coups sur la mesure ; la figure court sur les pas suivants, un trig posé dedans la
coupe et reprend la main), **CURVE**, la vitesse le long de ces coups (vers la droite ils se resserrent et
s'adoucissent, une balle qui rebondit ; vers la gauche ils s'écartent, un roulement qui se pose ; au milieu,
réguliers ; rien à 1 coup), et **DIR**, le sens de la piste : en avant, en arrière, l'aller-retour (PING), au hasard
(RND), ivre (un pas de plus ou de moins à chaque fois, au hasard) et +2-1 (deux en avant, un en arrière). L'écran dit
combien de coups sur combien de pas. RATCH, SPAN et CURVE se lockent pas par pas (un pas tenu en STEP, un potard
tourné) ; l'aléatoire de RATCH (ENV tenu) ajoute des coups et, au-delà de 50, de la durée. **La face PLAY d'un effet**
(sa dernière) : SPEED, DIR, PROB et LENGTH. GLO tenu : SPEED, DIR, RATCH ou SPAN sur les quatre pistes. Toutes les
pistes repartent ensemble au début
du pattern ; un pattern dure le temps de sa piste la plus longue à sa vitesse (un aller-retour compte ses deux sens).
Effacer la piste (REC tenu) la remet à x1, en avant, sans CLIMB.

**CLIMB** (0.16.7 : SEQ, la troisième page des pas) : chaque trig monte (ou descend) un peu à chaque fois qu'il joue,
puis revient à sa note. **CLIMB**, de combien à chaque fois : −12 à +12 demi-tons, ou, la gamme du projet allumée
(SCALE), autant de ses notes (+1 en do mineur : do, ré, mi♭, fa…) ; 0 : rien. **NOTES**, combien de notes avant de
revenir (2 à 16 ; +2 sur 3 notes : la note, +2, +4, la note…). **ON**, ce qui fait monter : *Trig*, chaque fois que le
trig joue (ses ratchets sur sa note du moment) ; *Ratch*, chaque coup de ses ratchets (un roulement qui grimpe, qui
repart de la note à chaque trig) ; *Both*, chaque coup, d'une lecture à l'autre. **SHAPE** : *Ramp* (jusqu'en haut, puis
la note), *Bounce* (en haut, puis redescend pas à pas), *Random* (n'importe laquelle de ces notes). Chaque pas compte ses
propres lectures (le pas 1 monte d'une mesure à l'autre sans rien devoir au pas 5), depuis le PLAY ou le début d'un
autre pattern. CLIMB se locke pas par pas : une piste à 0 dont un seul pas est locké à +5, et lui seul grimpe ; ENV
tenu, son aléatoire. Un trig qui ne joue pas (sa PROB) ne compte pas. GLO tenu : NOTES, ON ou SHAPE sur les quatre
pistes. Les projets d'avant 0.16.7 sont relus sans CLIMB ; ils avaient 384 locks, il y en a eu 380 (les quatre derniers
ont fait la place de CLIMB : un projet qui les utilisait tous perd les quatre derniers), puis 367 depuis 0.16.23 (treize
ont fait la place des followers des LFO ; un projet en relit 367 au plus, dans l'ordre).

La deuxième page du son (EDIT,
SELECT) donne la **GATE** de la note (5 à 95 % du pas, lockable : des notes courtes et longues dans le même pattern),
la transposition, l'octave et **F.KEY** (le filtre qui suit la note) ; la deuxième page d'ENV le **P.ENV** (l'enveloppe
interne de Plaits sur la hauteur, pour des toms, des kicks qui plongent, des bulles).

**Les patterns** (0.15 ; **SEQ tenu**, d'après les Pocket Operator de Teenage Engineering) : 11 banques (A à K) de
16 patterns, **chacun un projet entier**, sons compris (seuls les réglages de la machine, la sortie, la qualité de
Warps et les couleurs, restent communs). SEQ tenu, l'écran des patterns s'ouvre : en haut les banques, sur les
**touches noires** ; dessous les 16 patterns de la banque, sur les **touches blanches** (pleins : remplis ; vides :
un cadre ; celui qui joue dans la première couleur ; le suivant encadré dans la deuxième, qui clignote). **Une touche
joue ce pattern**, en boucle ; **plusieurs à la suite font un enchaînement**, joué en boucle dans l'ordre : A1, A1,
A2, A5 joue A1 deux fois, puis A2, puis A5 (le mode song). Le changement se fait **à la fin du pattern en cours** (sa
plus longue LENGTH), tout de suite à l'arrêt. **SAVE tenu** (à l'arrêt) enregistre le pattern en cours dans sa case ;
un pattern quitté sans SAVE revient tel qu'il était sauvé (ses modifs perdues). Une case vide prend le pattern en
cours tel quel : c'est ainsi qu'on fait une variation (SAVE la remplit). **REC tenu et une touche blanche** efface un
pattern (à l'arrêt). Ils sont gardés compressés dans la flash (un projet de 3,8 Ko en tient dans 0,7 à 1,5 Ko : de
70 à 100 patterns selon leurs P-locks ; l'écran dit la place qui reste), à l'abri d'une coupure (une sauvegarde
coupée laisse le pattern comme il était). Au premier démarrage, le projet sauvé devient A1. **Depuis 0.16.32, les
projets et les patterns d'avant 0.16** (de 0.1.4 à 0.15) **ne se lisent plus** : un projet sauvé d'avant laisse la
place à la démo, un pattern d'avant se comporte comme une case vide (SAVE l'écrase). Ceux de 0.16.0 et après se lisent
toujours. Le code qui les convertissait (une douzaine de formats) rend sa place à la flash. La sauvegarde de la page
d'installation (backup) garde le pattern en cours, pas encore les autres ; dans le navigateur, ils durent le temps de
la page.

**Les presets** (0.15.1 ; **EDIT tenu**) : le **son d'une piste**, rappelable sur n'importe laquelle. Un preset
garde tout ce qui fait le son : le modèle, les potards de son son, de son enveloppe et de sa hauteur (GATE, TRANSP,
OCTAVE compris), ses bits, son filtre, son glitch et l'aléatoire de ces potards ; pas son niveau, ses envois, son pan,
ses pas ni ses câbles (ils restent ceux de la piste). EDIT tenu, l'écran des presets de la piste active s'ouvre
(PRESETS change de piste) : 11 banques sur les **touches noires**, 16 sons sur les **touches blanches**. **A, c'est
l'usine** : KICK LONG, KICK HARD, SNARE, CLAP, HAT, OPEN HAT, TOM, METAL (le résonateur modal), ACID (POLY), FM BASS
(celle de la démo), REESE (REV3, deux scies qui dérivent), FM STAB, DARK PAD (PADS, sa TEXTURE sur Tape), PLUCK (la corde de Plaits),
SYNC LEAD (REV3) et LE BRUIT (le bruit de Plaits, cinq bits troués, glitché). **B à K sont à toi** (160 sons). Une
touche blanche **pose le son sur la piste** et le joue (do 3, comme un pas) tant qu'elle est tenue ; **SAVE tenu et une
touche blanche** y garde le son de la piste (il garde le nom du son d'où il vient, sinon celui du modèle) ; **REC tenu
et une touche blanche** l'efface ; la banque A ne bouge pas. PADS ne va que sur une piste à la fois : l'écran dit
laquelle l'a. Les presets vivent avec les patterns dans la flash (un son : une centaine d'octets compressé) ; un
preset s'enregistre même en jouant, sauf s'il faut d'abord faire de la place (un secteur effacé, le son coupé un
instant) : l'écran demande alors l'arrêt.

**Les pas d'un effet** (SEQ sur un effet, ou ses pages sous FX, avant ses rythmes euclidiens et sa face PLAY) : **DECAY** (10 ms à 5 s), **PROB**, **DEPTH** et
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

**Le SPREAD** (HOME, troisième face, 0.16.19) : un potard par piste pour l'ouvrir en largeur. À 0 (Off), la piste
est un point, là où son pan la met ; en tournant, elle s'étale de part et d'autre. Le principe est celui du
pseudo-stéréo à peignes complémentaires (Lauridsen, 1954) : on fabrique une « différence » à partir de la piste
elle-même, qu'on **ajoute d'un côté et qu'on retire de l'autre**. Cette différence, ce sont les aigus de la piste
(au-dessus de 150 Hz, un passe-haut à 24 dB/octave), retardés de 8 à 13 ms puis diffusés par deux filtres passe-tout
de Schroeder (quelques millisecondes chacun : une petite traînée d'échos serrés plutôt qu'un seul écho, comme le font
Orban ou Wider de Polyverse), avec des retards différents sur chaque piste (deux pistes ouvertes ne creusent jamais
les mêmes fréquences). Gauche plus droite redonne donc exactement la piste : **en mono** (une sono de club, un
téléphone, une enceinte Bluetooth), rien ne s'annule, rien ne fait de peigne, et **les graves restent au milieu**,
identiques des deux côtés, sans problème de phase. Chaque côté pris seul a ses aigus un peu plus forts (+2 dB au bout
de la course) et, de quelques dizaines de Hz en quelques dizaines de Hz, les fréquences partent tantôt à gauche, tantôt à droite : à
l'oreille, de la largeur, sans le timbre « flanger » d'un peigne régulier. Au bout, les deux côtés ne sont plus
corrélés qu'à 0,22 ; la première moitié de la course fait déjà l'essentiel. Une piste tirée à fond d'un côté reste un
point (il n'y a pas de place au-delà), et la largeur se resserre à mesure qu'on s'écarte du centre. Le SPREAD vient
après l'EQ et le duck ; les envois au delay et à la réverbe n'en dépendent pas. Il ne se locke pas, mais un LFO peut
le moduler (une piste qui respire en largeur). L'écran
montre chaque piste comme une barre dans l'image stéréo, à sa place et à sa largeur du moment. Ses lignes tiennent
dans 8,8 Ko de mémoire, et il ne calcule que pour les pistes ouvertes (environ 2,5 % du processeur par piste).

**L'EQ** (HOME, quatrième face) : un potard par piste pour lui faire de la place dans le mix. Au milieu, rien
(Flat) ; à gauche, un passe-bas qui descend de 18 kHz à 125 Hz (on enlève les aigus) ; à droite, un passe-haut qui
monte de 20 Hz à 8 kHz (on enlève les graves), en 24 dB/octave (deux SVF de stmlib, Butterworth), une octave par
pas de potard sur toute la course : un peu de potard enlève un peu, au bout c'est un mur. Le filtre est avant le
duck et les envois (ce qui part au delay et à la réverbe est filtré aussi) ; il se locke pas par pas, il a son
aléatoire (ENV) et un LFO peut le moduler. L'écran trace les quatre courbes, chacune dans la couleur de sa piste, la
piste active par-dessus.

**Le duck** (HOME, cinquième face) : DECAY (20 ms à 2,6 s), puis la profondeur de T2, T3 et T4. À chaque trig de
T1 (un pas, un ratchet, une touche), ces pistes baissent d'autant et remontent avec DECAY. Le duck agit sur la piste
avant ses envois : ses échos et sa réverbe partent baissés avec elle (les queues déjà dans les effets continuent).
Sa profondeur se locke pas par pas, comme le reste de la piste.

**Le bus master** (HOME, sixième et septième faces) : ALGORITHM choisit le mode, OFF, **COMP** ou **OTT** ; les
potards de la face qui ne joue pas sont grisés. COMP : THRESH (0 à −36 dB), RATIO (1:1 à 20:1), RELEASE (20 ms à 1,3 s),
GAIN (±12 dB) ; à droite, la réduction de gain. OTT : DEPTH (le mélange avec le son direct), TIME (les temps d'attaque
et de release, ×1 au milieu, de ×1/4 à ×4), DRIVE (l'entrée) et LEVEL (la sortie) ; à droite, ses trois bandes,
montées (rose) ou écrasées (bleu). Après les deux, toujours, le limiteur de stmlib.

Sur les pages des quatre pistes (niveaux, pans, SPREAD, EQ, envois, duck), le nom de la piste active est en défonce. Et en
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
S&H : une valeur au hasard par cycle, Smooth : un aléatoire lissé, puis les formes de la 0.16.22, plus bas), **RATE**,
**MODE** et **SLEW**. MODE : Free
(RATE de 0,02 à 20 Hz), Sync (calé sur la mesure, RATE devient un rapport : 4 par temps à 1 pour 16 mesures) ou
T1 à T4 (relancé au début à chaque trig de la piste : une rampe qui descend fait une enveloppe à chaque note). SLEW
lisse la sortie (1 ms à 2 s : un carré arrondi, un S&H qui glisse). L'écran trace ce que le LFO a fait ces 1,4
dernières secondes ; en bas, les trois LFO, chacun dans sa couleur, allumés à leur hauteur. Les LFO remplacent les
deux Tides des 0.3 : ils coûtent à peu près pareil (presque rien), mais se règlent d'un coup d'œil.

**Les formes rythmiques et le chaos** (0.16.22). Les rythmiques dessinent une figure sur un cycle coupé en huit : en
Sync à « 1 / 2 beats », chaque huitième est une double croche, à « 1 / bar » une croche (et le swing du GLOBAL s'y
applique) ; en T1 à T4, la figure repart à chaque trig de la piste.

- **Pump** : le creux du sidechain et sa remontée (tenu un instant, revenu aux deux tiers du cycle). En T1 sur la
  piste du kick, vers un LEVEL : le pompage.
- **Stairs** : quatre marches qui montent. Vers la hauteur avec le quantiseur : des arpèges.
- **Clave** : les trois coups du tresillo (3 + 3 + 2), chacun qui retombe.
- **Bounce** : les arcs d'une balle qui rebondit, chacun plus court (0,6 du précédent) et plus bas ; le cycle les
  contient tous.
- **Rises** et **Falls** : des rampes qui montent, ou qui descendent, sur le 3 + 3 + 2.
- **Zigzag** : monte sur trois huitièmes, retombe sur un, remonte sur deux, redescend sur deux.
- **Chaos** : l'attracteur de **Lorenz** (1963 ; σ 10, ρ 28, β 8/3), sa coordonnée x : il tourne autour d'une de
  ses deux ailes, puis de l'autre, sans jamais refaire le même chemin. Ni périodique ni aléatoire : déterministe et
  imprévisible. RATE règle sa vitesse (un tour d'aile par cycle, à peu près).

**Le suiveur d'enveloppe** (0.16.23, la forme **Follow**). Le LFO écoute une piste et rend son niveau (de 0 à 1),
pour moduler ce qu'on veut avec : un kick qui ouvre le filtre d'une autre piste, une voix qui pousse une réverbe. Sa
face prend trois potards à sa façon : **ATTACK** (à la place de RATE, 0,5 ms à 1 s), **MODE** et **DECAY** (à la place
de SLEW, 5 ms à 5 s). MODE :

- **Follow** : il suit le niveau de la piste, avec son attaque et son relâchement.
- **Trig** : il déclenche une enveloppe attaque / decay chaque fois que la piste dépasse le seuil (THRESH ; il se
  réarme quand elle repasse sous la moitié du seuil).
- **T1** à **T4** : la même enveloppe, déclenchée par les trigs de la piste (sans écouter le son).

Une deuxième face, **LFO 1 In** (2, 3), apparaît après la sienne (LFO, puis SELECT) : son entrée. **SOURCE** : la piste
écoutée (T1 à T4), prise avant son volume, le filtre du mixer et le duck : une piste au volume à zéro s'entend
encore. **GAIN** : de −24 à +24 dB. **THRESH** : de −60 à 0 dB, le seuil de Trig ; en Follow, le plancher (en dessous,
0 ; au-dessus, le reste étiré jusqu'à 1). **FILTER** : à gauche un passe-bas de 18 kHz à 100 Hz (n'écouter que les
graves : le kick d'un mix), à droite un passe-haut de 20 Hz à 5 kHz (n'écouter que les aigus : les charleys), 12 dB
par octave, éteint au milieu. Il écoute le dernier bloc de la piste (2,9 ms de retard) et coûte presque rien.

**Le son** (EDIT), première page : FREQ, puis HARMONICS, TIMBRE et MORPH sous le nom (court) que leur donne le manuel de Plaits pour
le modèle choisi (le SWARM : DETUNE, DENSITY, GRAIN ; le MODAL : STRUCT, BRIGHT, DECAY…) ; sur une batterie, une
batterie FM, ZERO ou la FM, leurs huit réglages à eux. **ENV**, première page : l'enveloppe. Les modèles qui ont la leur
(STRING, MODAL) contournent le LPG de Plaits : LPG COLOUR est alors grisé, et pour
STRING et MODAL, DECAY s'ajoute à leur decay (leur MORPH) : il marche sur toutes les pistes, avec ses locks et son
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
qu'une répétition ait un accident (jouée à l'envers ou un trou). Les tranches sont gardées en 8 bits mu-law à 44,1 kHz, 8 Ko par piste (186 ms), dans la mémoire que la réverbe a rendue
en passant ses lignes de retard sur 16 bits : un peu de grain, comme un sampler tordu.
Tout se locke pas par pas, a son aléatoire, et les potards continus se modulent par les câbles.

**Le filtre** (EDIT, troisième ensemble de la piste : entre les bits et le glitch, avant Warps et le mixer). **ALGORITHM** choisit le
modèle (GLO tenu : sur les quatre pistes). OFF par défaut : il ne coûte alors rien, et ses potards sont grisés.

| Modèle | D'où il vient | Types (TYPE) | Ce que DRIVE y fait |
| --- | --- | --- | --- |
| **LADDER** | l'échelle à transistors du Moog, d'après le modèle « amélioré » de Stefano D'Angelo et Vesa Välimäki (2013 ; leur code de référence, licence ISC) : la saturation de chaque paire de transistors à chacun des quatre étages, l'auto-oscillation du circuit. **Accordé sur sa boucle** (0.16.30) : sa boucle, en retard d'un échantillon, résonnait plus bas que sa coupure et chantait plus tôt à mesure qu'elle montait (avec le g de D'Angelo et Välimäki : une résonance à 2,7 kHz pour une coupure à 6,4 kHz, l'auto-oscillation dès 50 % de RESO, et le filtre qui se refermait au-delà, 1,3 kHz à 9 kHz) ; maintenant ses étages sont réglés pour que la boucle résonne sur CUTOFF (à un cent près de 15 Hz à 4 kHz, puis jusqu'à 4,8 kHz, le passe-bas ouvert jusqu'à 6,8 kHz), et sa rétroaction pour qu'il chante au-delà de 95 % à toutes les coupures. Le même accord dans TAURUS | LP 24 dB ; HP 24 et BP pris sur ses étages (les mélanges de l'Oberheim Xpander) | ce qui entre dans les transistors (un peu plus fort, puis tassé : le niveau tenu) |
| **MS-20** | le Korg 35 (MS-10, premiers MS-20), d'après Will Pirkle (note AN-5) sur les filtres à rétroaction sans délai de Vadim Zavalishin, la saturation **dans** la boucle de résonance : elle drive et chante | LP 12 dB, HP 6 dB, BP (un passe-haut puis le passe-bas, comme les deux filtres du MS-20 en série) | la saturation de la boucle (son niveau rendu) |
| **SEM** | le filtre d'état 12 dB de l'Oberheim SEM (Pirkle, Zavalishin) : il résonne sans s'auto-osciller, comme le vrai | LP, BP, HP | un étage d'entrée à transistor un peu décentré (des harmoniques paires avec les impaires, sa courbe anti-repliée), puis ses deux intégrateurs (des OTA) poussés : la résonance s'épaissit et se tasse |
| **VOX** | trois formants, les trois premières résonances des voyelles (moyennes mesurées de Peterson et Barney, 1952) : CUTOFF passe de A à E, I, O, U, la résonance les aiguise | voix basse, moyenne, haute | le même étage d'entrée, et ses trois résonateurs poussés, plus doucement (les voyelles restent) |

**Première page** : **TYPE** (LP, BP, HP ; VOX : LOW, MID, HIGH), **CUTOFF** (20 Hz à 18 kHz ; VOX : la voyelle),
**RESO**, **ENV** (de combien l'enveloppe ouvre ou ferme, ±8 octaves). **Deuxième page** (SELECT) : **DRIVE**,
**NOISE** (du bruit dans le filtre, sur tous les modèles : la résonance souffle et siffle ; il suit l'enveloppe, il
souffle aux trigs, pas en continu), **COLOR** (le bruit : plus sombre à gauche, blanc au centre, plus brillant à
droite), **FM** (la sortie du filtre renvoyée dans sa fréquence de coupure, échantillon par échantillon, d'un côté ou
de l'autre : de la rugosité, puis du chaos ; depuis 0.14 son potard suit une courbe exponentielle, discrète jusqu'à
la moitié, 12 % de la profondeur au milieu, toute la profondeur au bout). L'**enveloppe** du filtre est sur ENV (deuxième page) : celle d'un
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

**Les rythmes euclidiens** (SEQ, cinquième page ; sur les effets, l'avant-dernière) : **FILLS** coups répartis aussi
régulièrement que possible sur la longueur de la piste, **ROTATE** les décale ; LENGTH et PROB sont ceux de la piste
de pas. Tourner FILLS, ROTATE ou LENGTH **réécrit les trigs** de la piste (ses locks restent) ; on peut ensuite les
retoucher à la main. L'écran montre les pas en cercle, les trigs allumés, celui qui joue entouré.

**L'aléatoire** : la page RANDOM (SEQ, puis SELECT) a quatre potards, PAN, DECAY, TUNE et RATCHET. Tourné à droite,
une nouvelle valeur au hasard à chaque trig ; à gauche, des valeurs au hasard mais qui reviennent les mêmes à chaque
tour (une boucle) ; plus on tourne, plus l'écart est grand (100 : toute la course ; TUNE : une octave de part et
d'autre ; RATCHET : jusqu'à 7 coups de plus et, au-delà de 50, jusqu'à 15, et une durée (SPAN) plus longue). Au centre, rien : le réglage retrouve exactement sa valeur. **Chaque
retour au centre tire une nouvelle boucle** pour ce réglage : une boucle qui ne plaît pas, on revient à 0 (ou on passe
côté hasard), on retourne côté boucle, c'en est une autre (les boucles des autres réglages ne bougent pas ; un projet
sauvegardé garde les siennes). **ENV tenu
+ un potard** fait la même chose sur n'importe quel réglage d'une piste ou d'un effet.

**Le quantiseur et le swing** (GLO) : TEMPO, SCALE et ROOT (chaque note, des pas, de l'aléatoire ou du clavier, va
à la note de la gamme la plus proche : Off, Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Harmonic et
Melodic minor, Major et Minor pentatonic, Blues, Whole tone, Hirajoshi, Prog : l'accord que joue PADS, voir plus
haut ; puis, depuis 0.16.12, **Phryg. dom** (le phrygien dominant : la seconde mineure et la tierce majeure) et
**Flip** (mineur une mesure, dorien la suivante : la sixte bémol, puis naturelle ; mineur à l'arrêt)), SWING (50 % : droit ; 75 % : les
contretemps retardés d'une demi-double croche). Quand une gamme est choisie, tout ce qui fait une hauteur y reste :
FREQ (et ce que les câbles et la dose d'aléatoire en font : un LFO sur FREQ joue en escalier dans la gamme), la
transposition, la note de l'oscillateur de Warps. Seul P.ENV glisse encore d'une note à l'autre : c'est l'enveloppe
interne de Plaits, un glissando voulu.

**Endless** (ARP, comme sur l'OP-1) : sur sa page, **REC** lance l'enregistrement (la frise passe en REC, le bouton
clignote) ; chaque touche ajoute une note (on l'entend), **SEL** ajoute un silence, jusqu'à 64 pas. **REC** à
nouveau : la piste joue ces notes en boucle sur le tempo, à la place de ses pas. Pendant qu'elle joue, **une
touche transpose** la boucle depuis sa première note (la transposition reste). **Le séquenceur à l'arrêt** (0.16.11),
une touche tenue la joue : depuis sa première note, transposée par la touche, au tempo et à sa RATE ; on lâche, elle
s'arrête (une autre touche pendant qu'on tient : la transposition suit). Ses potards : RATE (1/32 à 1/4, avec
les triolets), GATE (la longueur des notes), DIRECTION (avant, arrière, aller-retour, au hasard ; depuis 0.16.13,
+2-1 : deux en avant, une en arrière, et CONVERGE : la première, la dernière, la deuxième, l'avant-dernière…), TRANSPOSE. **REC
tenu** efface la boucle (la piste retrouve ses pas). Probabilité, ratchets, aléatoire et quantiseur s'appliquent aussi
à ses notes. En bas, ses notes en barres, la note jouée en blanc. **Sa deuxième face** (0.16.2 ; ARP à nouveau, ou
SELECT) choisit ce qui joue : **STRIDE**, une note sur combien (ALL : toutes ; 1/2 : une sur deux, les autres sautées ;
sur une boucle de longueur impaire, la suite finit par les passer toutes, dans un autre ordre), **REST**, un silence
tous les combien de pas (1/5 : quatre notes puis un silence ; la boucle reprend où elle en était), **HOLES**, des
silences au hasard (0 à 100 %), **LEAP**, des notes une octave plus haut au hasard. GLO tenu : sur les quatre pistes. En bas
(0.16.3), la frise le montre : les notes que STRIDE n'atteint jamais en gris, la note en cours encadrée et vide
pendant un silence de REST (en gris) ou un trou de HOLES (en blanc), relevée avec une marque au-dessus quand LEAP la
monte d'une octave.

**L'ARP** (0.16.13 : la troisième face d'Endless, ARP encore ou SELECT) : **MODE** choisit ce qu'Endless parcourt :
*Loop*, la boucle enregistrée (comme avant) ; *Keys*, **les touches tenues** (un arpégiateur) ; *Chord*, **l'accord que
joue PADS**, qui change avec la progression. **OCTAVES** (1 à 4) les répète plus haut ; **LATCH** garde les notes une
fois lâchées (la touche suivante, après avoir tout lâché, recommence un nouvel accord ; LATCH remis à Off : tout
s'arrête) ; **ORDER** les range par hauteur (*Pitch*) ou dans l'ordre où on les a jouées (*Played*). Tout le reste
d'Endless s'y applique : RATE, GATE, DIRECTION (montée, descente, aller-retour, au hasard, +2-1, CONVERGE), STRIDE
(des sauts), REST, HOLES, LEAP, les ratchets, l'aléatoire et le quantiseur. Il joue avec le séquenceur et **sans lui** :
séquenceur arrêté, une touche tenue le lance (en *Keys*, les touches elles-mêmes ; en *Chord*, n'importe laquelle),
on lâche, il s'arrête, sauf ce que LATCH garde. Les touches n'y transposent pas : en *Keys* elles sont les notes. En bas,
les notes qu'il parcourt, la jouée en blanc.

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
La machine passe sur **son écran à elle** : du rouge s'ouvre depuis le centre (quel que soit le thème), RAN / DOM
s'écrit en grand, en blanc, ses lettres arrivant de gauche à droite à travers des signes ; plus la dose monte, plus
ses lettres se mettent à changer (une à quelques %, les six à 100 %, toutes relancées à chaque cran). Dessous, la
dose, sa barre (le dernier quart marqué) et ce que font les fills. **À chaque trig de
chaque piste**, tous ses réglages du son sont relancés au hasard, chacun de son côté : chaque note est un autre son,
rien ne boucle. **La dose suit une courbe** (son carré) : au début de la course, des écarts à peine perceptibles (à
20 %, 4 % de la plage), de plus en plus francs, toute la plage à 100 %. **Tout reste dans la gamme** : FREQ (jusqu'à
±6 demi-tons à 100 %) et la transposition (±7) sautent par demi-tons entiers, puis sur les notes de la gamme choisie ;
ce qui en sortirait n'est jamais tiré : l'enveloppe de hauteur (son glissando), le PING du filtre (une résonance
qui sonne à n'importe quelle hauteur), HARMONICS là où c'est un intervalle (l'écart de SWARM
et de PARTICLE), WAVE d'OTIS (un programme : sa Transwave refaite), RATIO de la FM dans ou vers ses accords, le décalage de Warps en décaleur de
fréquence. Le reste : HARMONICS ailleurs, TIMBRE, MORPH, DECAY, COLOUR et les enveloppes de Plaits (sur une batterie
FM, ses réglages), la gate, les bits (CLOCK, BITS, MASK, SHAPE) et le glitch (GLITCH, SIZE, BEND, DROP : à forte dose, des notes qui
bégaient), le
filtre (CUTOFF jusqu'à ±3,4 octaves, RESO, ENV, DECAY, DRIVE, FM, KEY, NOISE, COLOR, ATTACK ; au-delà de 60 % son
TYPE aussi, LP, BP ou HP, sur un trig sur deux à 100 % ; pas son modèle : un filtre OFF le reste, son CPU avec),
et le pan (pas l'EQ depuis 0.8.5 : la couleur de la piste reste la sienne). **Ce qui coûte cher une fois allumé, la dose ne l'allume pas** (0.8.3) : le DRIVE, la FM et le NOISE
du filtre, les bits et le glitch, s'ils sont à 0 (le potard, son lock et son aléatoire), y restent ; allumés, elle les
relance comme le reste. Allumés partout à 100 %, ils faisaient passer le stress test de 79 à 89 % par cœur (compté),
son pire bloc au-delà du bord ; ainsi, la dose à 100 % ne lui coûte presque rien de plus (80 %). **Les envois d'effets**, de temps en temps et légèrement : à 100 %, un trig sur sept environ part
dans le delay ou dans la réverbe (un envoi de 20 à 35 %, en plus du sien), presque jamais à mi-course. Les effets
eux-mêmes sont relancés à chaque temps : le feedback, le filtre, le drive, la résonance et le pleurage du delay, la
réverbe, l'algorithme de Warps (d'un repère à un autre : entre deux, Warps calcule les deux algorithmes, deux fois
plus de travail), son timbre et ses niveaux (sauf LEVEL 1 quand Warps joue son propre oscillateur : c'est sa note,
qui reste où elle est depuis 0.8.6). **Les ratchets** (0.16.8 ; 0.16.9, ses courbes revues pour rester musicales) : un trig qui n'en a pas en reçoit à
partir de 50 % de la course (2 à 4 coups sur son pas ; au-delà de 75 %, jusqu'à 6, parfois sur 2 à 4 pas), une chance
qui monte vite puis plafonne : 19 % à 60 %, 34 % à 70 %, 43 % à 80 %, jamais plus de 50 % (à 100 %) ; un trig qui en a
n'est pas touché avant 30 %, puis, de temps en temps, ils changent (un ou deux coups de plus ou de moins, au-delà de
75 % un pas de plus ou de moins) ou **sautent** (un seul coup), une fois sur deux chacun : jusqu'à 5 % des trigs à 50 %,
puis la même courbe jusqu'à 50 % ; au-delà de 50 %, leur CURVE est relancé (rebond, roulement qui se pose) ; au-delà de
75 %, de temps en temps, le roulement grimpe ou descend d'un à trois demi-tons (ou notes de la gamme) par coup, sur un
pas sans CLIMB à lui. **Des trigs laissés de côté** (0.16.10), à partir de 50 %, discrètement, sur la même courbe :
8 % des trigs à 60 %, 14 % à 70 %, 17 % à 80 %, jamais plus de 20 % (à 100 %) ; deux fois moins sur les temps, jamais le premier
pas de la mesure, pour que le groove tienne (un trig des fills joue toujours). Un trig laissé de côté ne compte pas pour
CLIMB. Comme le reste de la dose, à chaque trig un nouveau tirage. Rien de ce qui
tient le temps (divisions, tempo, vitesses, longueurs, pas), ni les niveaux, la gamme ou le branchement. On lâche OCT− : retour à la page, la dose reste (en bas, un trait
de la largeur de la dose ; les potards montrent la valeur que le dernier trig a tirée, RANDOM dessous). Revenir à 0
rend le son exact de départ dès le trig suivant, comme l'annulation d'ALL. Pendant le geste, l'octave du clavier (ou
la page de pas) ne bouge pas. (Jusqu'en 0.6.1, la dose tirait un seul écart par réglage, gardé à chaque trig : un son
déplacé, mais le même à chaque note.)

**TRAUMA** (0.14, caché) : tenir **OCT+** et tourner **SELECT**, de 0 à 100 % (2 % par cran, 5 en tournant vite).
Un hommage à Audiotrauma, le label strasbourgeois d'électro, d'indus et de noise qui poussait ses disques dans le
rouge : une dose de saturation qui entre petit à petit. **À 0, rien** : le son tel qu'il est réglé, ce qui permet de
glisser depuis là. Puis, chacun à son point de la course, ce qui ajoute des harmoniques, **jamais une note** (tout
reste dans la gamme) :
- **dès le début**, un **drive sur chaque piste**, juste après son filtre : la courbe du drive des batteries (x − x³/3,
  puis plat), antialiasée (ADAA), son gain de 1 à 8 (le carré de la course), chaud (décalé : des harmoniques paires,
  le continu retiré par un condensateur à 8 Hz) ; le niveau de la piste est suivi (ses crêtes, qui retombent en
  0,1 s) et amené au même point de la courbe, puis rendu : le même drive sur une piste douce que sur une forte, son
  niveau gardé, ses queues remontées dans la saturation en retombant (comme une pédale qui tient). Il entre en fondu
  sur le premier quart. Là où la piste a un filtre, son **DRIVE** monte aussi (+0,45 au bout) ;
- **à partir de 25 %**, **chaque trig a son drive** (de 20 à 100 % de celui de la dose au bout de la course,
  depuis 0.14.7 : un trig presque propre, le suivant à fond ; de 70 à 100 % avant), chaud ou, sur jusqu'à la moitié
  des trigs, dur (harmoniques impaires) ;
- **à partir de 40 %**, la **FM des filtres** sur une part des trigs (un tiers, puis tous), d'un côté ou de l'autre,
  **au plus 10 % de sa profondeur** à 100 % (le 10 % de l'ancien potard : plus, le son se perd) ;
- **à partir de 50 %**, **Warps** s'ouvre par moments (MIX, TIMBRE, LEVEL 2), relancé **à chaque trig de la piste
  qu'il traite** (depuis 0.14.7 ; un temps à la fois quand il joue son propre oscillateur), seulement s'il est
  branché : TRAUMA ne l'allume jamais (son CPU : un cinquième d'un cœur, le même ouvert ou fermé) ;
- **à partir de 60 %**, le **DRIVE du delay** (des échos qui saturent).

Son écran, en noir et blanc : des barres blanches qui entrent des deux côtés, puis le slogan du label en Anton (une
Impact libre), ses lettres et ses lignes serrées : VOUS ÊTES LE, et dessous BRUIT en très grand, aussi large. À 0, il
ne bouge pas ; dosé, **ses lettres sont tirées au hasard aussi vite que l'écran va** (un quart à quelques %, trois sur
cinq à 100 %), et à travers elles **le slogan passe d'une langue à l'autre** toutes les 1,4 s (0,7 s à 100 %), ses
lettres retombant une à une : VOUS ÊTES LE BRUIT, YOU ARE THE NOISE, あなたたちは ノイズだ, VY JSTE TEN HLUK, IHR
SEID DER KRACH, VOI SIETE IL RUMOR, NI ÄR RENT OLJUD (le mot exact a six lettres en italien et en suédois : trop
large en grand ; les kana en Dela Gothic One). Une lettre ou quelques-unes passent au rouge au hasard ; **sur le
dernier cinquième, un stroboscope** (l'écran blanc une image sur quatre, puis sur trois, une sur deux à 100 %).
Dessous, la dose et sa jauge ; **à 100 %, la jauge sort de son cadre** jusqu'au bord de l'écran, en rouge, et son
chiffre file au-delà de 100. On lâche OCT+ : retour à la page, TRAUMA reste (en bas, un
trait orange sous celui de la dose d'aléatoire). Les deux doses se combinent. Elle se sauve avec le projet ; un
projet de 0.13 revient avec TRAUMA à 0.

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
Depuis 0.16.15, il **garde les touches tenues** : une voix seule (la FM, ZERO, Plaits, les batteries, POLY en Mono…)
joue la **dernière** touche appuyée, et quand on la lâche alors qu'une autre est encore tenue, **elle revient sur
celle-ci** (un nouveau trig) ; Endless fait de même avec sa transposition. POLY en Poly, REV3 sans accord et OTIS
jouent toutes les touches tenues, une voix chacune : des accords à la main.

La 0.16.17 avait mis **ROBOVOX** (une puce de parole qui chante, d'après le Robovox de Kraftwerk) à la place de
SPEECH ; retiré en 0.16.18, SPEECH est revenu : un projet ou un son de la 0.16.17 se relit, SPEECH sur la piste qui
jouait ROBOVOX, ses potards au milieu.

Un projet sauvegardé avec les 0.1.4 à 0.2.6 se recharge : ses réglages, ses modèles, ses câbles (sauf ceux qui
partaient de Marbles), ses pas, ses locks et Endless ; le tempo de Marbles devient le tempo, et les effets nouveaux
démarrent neutres (rien vers le delay, aucune piste de pas d'effet). Ceux des 0.3 aussi (l'EQ y est à plat, le grit
coupé ; depuis 0.7, le grit d'un projet plus ancien est oublié : le circuit bending prend sa place, coupé). En 0.4, les deux Tides laissent la place aux trois LFO : les câbles qui partaient de Tides A1, A2 et A3
partent des LFO 1, 2 et 3, les autres sont retirés, et les LFO démarrent comme ceux d'un projet neuf ; les potards de
Warps passent sur leurs nouvelles faces (MIX à 100 %, DRY et Freq shift dans MODE), avec leurs locks et leurs
aléatoires ; le delay est en SYNC.

## Inspirations

Chaque moteur de MIFM1 doit quelque chose à une machine, et le site le raconte, moteur par moteur
([distortedimages-code.github.io/MIFM1/emu](https://distortedimages-code.github.io/MIFM1/emu/)) :

- les batteries : le **Microtonic** de Sonic Charge (Magnus Lidström, 2003) ; les batteries FM : le **LXR-02** d'Erica
  Synths, héritier du LXR de Sonic Potions (son code) ;
- la FM : la puce **YM2612** de la Mega Drive (Yamaha, 1988) et les enveloppes du **DX7** (1983) ; ZERO : la FM à travers
  zéro, de John Chowning aux oscillateurs analogiques ;
- PADS : le tintinnabuli d'**Arvo Pärt** (1976), les string machines, et pour sa TEXTURE les nappes usées de **Boards of
  Canada** ;
- POLY : le **SH-101** de Roland (1982) et l'**AIRA S-1** (2023) ; REV3 : le **Prophet-5** de Sequential Circuits (1978),
  sa Rev 3 et le bouton Vintage de la Rev 4 (2020) ;
- OTIS : l'**Ensoniq SQ-1** (1990), le **VFX** (1989) et ses Transwaves (ses filtres n'avaient pas de résonance :
  elles étaient le seul moyen d'en avoir une), la puce **OTIS** (ES5505) ; avant eux, le **Mirage** (1984, un
  échantillonneur 8 bits à 1 695 dollars) et l'**ESQ-1** (trois oscillateurs par voix sur la puce DOC de Robert Yannes,
  celui du SID du Commodore 64) ;
- TAURUS : les pédales **Taurus** de Moog (le 205A de David Luce, 1975 ; sous Genesis, Rush, Yes, Pink Floyd, The Police,
  U2) et le **Minimoog** (1970) ;
- VOCODER : le **Vocoder Plus VP-330** de Roland (1979-1980 ; sous Vangelis, Yellow Magic Orchestra, Laurie Anderson,
  Underworld), et avant lui le vocodeur de **Homer Dudley** (Bell Labs, 1937) ;
- STATIC : **ARC/01**, le cahier des charges DSP de Yoann (novembre 2026) ; **Ryoji Ikeda** (*+/−*, 1996 : des
  sinus et du bruit blanc, rien d'autre), **Pan Sonic** (Mika Vainio et Ilpo Väisänen, Turku, 1992 : un minimalisme
  dur et fort, sur des machines construites pour eux), et le sample and hold de l'**ECHON 6** de Morphor (2025) ;
- les filtres : l'échelle de **Moog**, le Korg 35 du **MS-20**, le **SEM** d'Oberheim ; le circuit bending : le
  **Biscuit** d'OTO et le **Data Bender** de Qu-Bit ; le delay : les potards du **Zen Delay** ; la réverbe et Warps :
  **Clouds** et **Warps** de Mutable Instruments ;
- le séquenceur : les P-locks d'**Elektron**, Endless de l'**OP-1**, les chaînes des **Pocket Operators**, les rythmes
  euclidiens de Toussaint et Bjorklund ;
- le SPREAD : le pseudo-stéréo de **H. Lauridsen** (radio danoise, 1954) et de **Robert Orban** (son Stereo
  Synthesizer, breveté en 1970 : une différence passée par des passe-tout, ajoutée d'un côté et retirée de l'autre, les
  graves gardés au centre), et **Wider** de Polyverse et Infected Mushroom (2018), des peignes et des passe-tout qui
  s'annulent en mono.

**Le bruit avant nous** (0.16.16, sur le site) : la lignée d'où vient Audiotrauma, et les machines qu'elle avait en
main. **Cabaret Voltaire** (Sheffield, 1973 : des boucles de bande, puis des oscillateurs en kit) ; **Throbbing
Gristle** (leur label, Industrial Records, donne son nom au genre ; le Gristleizer de Chris Carter, un filtre et un
ampli contrôlés en tension avec un LFO) ; **Einstürzende Neubauten** (Berlin-Ouest, 1980 : la ferraille et les outils de
chantier) et **Test Dept** (Londres, 1981 ; *Shoulder to Shoulder* avec le chœur des mineurs grévistes du sud du pays
de Galles, 1985) ; **Front 242** (*No Comment*, 1984 : « Electronic body music » sur la pochette ; un Emulator II, un
DX7, une Drumulator) ; **Ministry** (*Twitch*, 1986, leur premier disque échantillonné, sur un Fairlight) ; **Skinny
Puppy** (*VIVIsectVI*, 1988 : un Ensoniq ESQ-1 et deux Mirage dans le matériel dessiné par Dwayne Goettel) ; **Nine Inch
Nails** (*Pretty Hate Machine*, 1989, sur un Emax, un Prophet VS, un Xpander et un Minimoog ; sur la tournée Self
Destruct, 1994-96, jusqu'à quatre DX7 détruits par soir, joués comme contrôleurs ; en mai 2000, le groupe « n'a presque
plus de claviers à casser » et cherche des DX7 d'origine ; les touches arrachées à coups de talon se racontent partout,
sans source de première main). Aussi Severed Heads, Nitzer Ebb, SPK.

Sources : [Pretty Hate Machine](https://en.wikipedia.org/wiki/Pretty_Hate_Machine),
[Far Out](https://faroutmagazine.co.uk/did-nine-inch-nails-really-destroy-their-keyboards-every-night/),
[Sweetwater inSync, 2000](https://www.sweetwater.com/insync/nine-inch-nails-needs-dx-7/),
[le matériel de VIVIsectVI](https://www.matrixsynth.com/2012/04/skinny-puppy-vivisect-synth-layout.html),
[Dwayne Goettel](https://en.wikipedia.org/wiki/Dwayne_Goettel),
[Reverb sur Throbbing Gristle](https://reverb.com/news/the-gear-of-throbbing-gristle),
[Front 242](https://en.wikipedia.org/wiki/Front_242),
[Twitch](https://en.wikipedia.org/wiki/Twitch_(Ministry_album)),
[Music Has the Right to Children](https://en.wikipedia.org/wiki/Music_Has_the_Right_to_Children),
[MusicRadar sur The Campfire Headphase](https://www.musicradar.com/artists/its-our-take-on-a-pop-record-pop-music-that-has-been-melted-on-a-hot-dashboard-in-the-sun-how-boards-of-canada-brewed-a-serene-genre-blurring-classic),
[Ensoniq Mirage](https://en.wikipedia.org/wiki/Ensoniq_Mirage), [ESQ-1](https://en.wikipedia.org/wiki/Ensoniq_ESQ-1),
[VFX](https://en.wikipedia.org/wiki/Ensoniq_VFX),
[le pseudo-stéréo, de Lauridsen à Kendall](https://csoundjournal.com/issue14/PseudoStereo.html),
[le brevet d'Orban](https://patents.google.com/patent/US3670106A/en),
[le manuel de Wider](https://polyversemusic.com/downloads/manuals/Wider%20Manual.pdf),
[le manuel du Taurus 205A](https://archive.org/details/Moog_Taurus_Model_205A_Owner_s_Service_Manual),
[Sound On Sound sur le Taurus](https://www.soundonsound.com/reviews/moog-taurus),
[Moog Taurus](https://en.wikipedia.org/wiki/Moog_Taurus),
[Roland VP-330](https://en.wikipedia.org/wiki/Roland_VP-330),
[Vintage Synth Explorer sur le VP-330](https://www.vintagesynth.com/roland/vocoder.php),
[Vocoder](https://en.wikipedia.org/wiki/Vocoder),
[le Muziekgebouw sur les sinus et le bruit blanc de Ryoji Ikeda](https://www.muziekgebouw.nl/en/ryoji-ikeda-s-sine-waves-and-white-noise-lz3y),
[Pan Sonic au CTM](https://archive.ctm-festival.de/index.php?id=3622),
[MusicTech sur l'ECHON 6](https://musictech.com/reviews/hardware-instruments/morphor-echon-6-review/) ; les autres sur le site.

## Le dépôt

- `firmware/` : le firmware (fm1-base `tools/build.py ../firmware`)
  - `app.c` : l'écran et les contrôles ; `mi/engine.cc` : le son (les pistes, les pas et leurs trigs, les LFO, la
    matrice, le quantiseur, les effets et leurs pistes de pas, le mixer ; les voix et Warps partagés entre les deux
    cœurs) ; `mi/mi_*.cc` : un pont C par module Mutable, et les machines de MIFM1 (`mi/mi_shared.cc` : ce qu'elles
    partagent, les textes de leurs potards et leur sortie) ; `mi/mi_reverb.h` : la réverbe de Clouds, ses lignes de
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
