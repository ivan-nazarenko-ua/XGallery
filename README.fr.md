# XGallery

[English](README.md) | [Українська](README.uk.md) | **Français**

Un programme Windows pour organiser une archive de photos et de vidéos. Il mémorise les fichiers
d'un dossier choisi, transforme les noms de dossiers en tags, permet d'ajouter vos propres tags et
notes, de rechercher par tags, de regarder les photos, de trouver les doublons et de garder vos
dossiers en ordre.

Toutes les informations sont conservées dans une base de données. Le programme ne modifie les
fichiers eux-mêmes que lorsque vous le demandez : rotation, suppression, ajout, nettoyage des
fichiers inutiles.

## Premier lancement

1. **Fichier → Nouvelle galerie…** (ou le bouton « Créer »). Indiquez un nom et le dossier
   racine qui contient vos photos.
2. Acceptez l'analyse. Le programme parcourt tous les sous-dossiers et enregistre les fichiers
   dans la base. Une somme de contrôle (sha256) est calculée pour chaque fichier, la première
   analyse d'une grande archive peut donc prendre du temps. Les analyses suivantes sont rapides,
   car elles ne traitent que les fichiers modifiés.

Pendant les opérations longues, une fenêtre de progression apparaît et l'interface reste
bloquée jusqu'à la fin de l'opération.

## Galeries

Le programme peut gérer plusieurs galeries indépendantes. Le choix de la galerie et les boutons
« Créer » et « Modifier » se trouvent sur la ligne du haut de la fenêtre principale.

Dans la fenêtre de modification, vous pouvez changer le nom, le dossier racine et l'option
**« Autoriser l'agrandissement des images »** : avec elle, les petites photos sont étirées pour
remplir la visionneuse. La même fenêtre contient le bouton **« Supprimer la galerie… »**. Il
efface de la base tout ce qui concerne la galerie, mais ne touche pas aux fichiers sur le disque.

## Tags

- **Automatiques.** Chaque dossier entre le dossier racine et le fichier devient un tag. Le
  fichier `Vacances/2024/mer.jpg` reçoit les tags « Vacances » et « 2024 ». Une virgule dans un
  nom de dossier est remplacée par une espace.
- **Les vôtres.** Un clic droit sur une vignette ouvre la fenêtre des métadonnées. Vous pouvez y
  retirer un tag du fichier et changer la **note** (0–100, 50 par défaut). Les modifications sont
  enregistrées avec le bouton « Enregistrer ». Un tag qui n'est plus utilisé nulle part est
  supprimé automatiquement. Un double-clic sur un tag dans cette fenêtre l'ajoute au champ
  « Contient les tags » de la fenêtre principale. La recherche elle-même ne démarre que lorsque
  vous cliquez sur « Rechercher ».
- **Ajouter des tags.** Le bouton « Ajouter des tags » de la fenêtre des métadonnées ouvre une
  fenêtre avec un champ « Nouveaux tags : » (plusieurs tags séparés par des virgules) et un tableau
  des tags de la galerie que le fichier n'a pas encore. Cochez ceux qu'il vous faut puis cliquez sur
  « Enregistrer ». Pendant qu'elle est ouverte, vous pouvez toujours ouvrir la photo depuis la
  fenêtre des métadonnées ; elle se ferme avec cette dernière. Un clic droit sur un tag du tableau, après confirmation, le supprime de **tous
  les fichiers de la galerie** ; cela n'a lieu qu'à l'enregistrement de la fenêtre des métadonnées,
  donc « Annuler » dans l'une ou l'autre fenêtre conserve le tag.
- **Copier et coller des tags.** Dans la liste des tags de cette fenêtre (aussi en modification
  groupée), sélectionnez des tags à la souris ou au clavier et appuyez sur Ctrl+C, ou choisissez
  « Copier les tags » dans le menu du clic droit. Ctrl+V ou « Coller les tags » ajoute une liste
  depuis le presse-papiers : les tags sont séparés par des virgules ou des sauts de ligne, les
  caractères `<` `>` `/` `\` sont remplacés par des espaces, l'écriture est l'écriture habituelle
  et les tags déjà présents sont ignorés. Pratique pour donner le même ensemble de tags à
  différents fichiers.
- **Modification groupée.** Cochez les cases dans le coin des vignettes. La sélection est
  conservée quand vous passez d'une page à l'autre et même d'une recherche à l'autre, vous pouvez
  donc réunir autant de fichiers que vous voulez. Le nombre de fichiers sélectionnés est affiché
  sous la grille (« Sélectionnés : N »). Un clic droit sur n'importe quelle vignette ouvre une
  fenêtre avec les tags communs à tous les fichiers sélectionnés. Les tags ajoutés et retirés
  s'appliquent à tous les fichiers sélectionnés à la fois ; un fichier qui a déjà un tag ajouté ne
  le reçoit qu'une fois. **« Tout cocher »** coche tous les fichiers de la page courante,
  **« Tout décocher »** retire les coches sur toutes les pages. Changer de galerie efface la
  sélection.
- **Affichage depuis la fenêtre de modification.** Un clic sur la vignette dans la fenêtre des
  métadonnées ouvre la photo dans la visionneuse (sans navigation) ou la vidéo dans son programme.
  En modification groupée, un clic sur une ligne de la liste des fichiers fonctionne de la même
  façon, et les flèches de la visionneuse ne parcourent que les photos sélectionnées.
- **Écriture.** Tous les tags sont enregistrés sous une seule forme : première lettre en
  majuscule, le reste en minuscules (« chats », « CHATS » → « Chats »). Le tiret bas `_` se lit
  comme une espace : les dossiers `_avril` et `avril` donnent un seul tag « Avril », et
  `nouvel_an` devient « Nouvel an ». Les tags saisis dans les champs de recherche sont ramenés à
  la même forme. Les caractères `,` `<` `>` `/` et `\` sont interdits dans les noms de tags.
- **`<sans tags>`** est une entrée de service, toujours la première de la liste. Elle trouve les
  fichiers qui n'ont aucun tag, ou les exclut de la recherche. Dans la liste et les champs de
  recherche, elle apparaît en gras sur un fond teinté, pour ne pas la confondre avec un tag
  ordinaire nommé « Sans tags ».

## Recherche et tri

- **« Contient les tags »** — un fichier doit avoir tous les tags indiqués (séparés par des
  virgules).
- **« Ne contient pas »** — un fichier ne doit avoir aucun de ces tags.
- **« Strict »** (la case à côté de « Contient les tags ») — ne trouve que les fichiers qui ont
  exactement ces tags et aucun autre. Le champ « Ne contient pas » est désactivé dans ce mode. Si
  « Contient les tags » est vide, ce sont les fichiers sans tags qui sont trouvés.
- Un double-clic sur un tag dans la liste de gauche l'ajoute à « Contient les tags ». Un clic
  droit permet de choisir où ajouter le tag, et aussi de le **renommer**. Si un tag portant le
  nouveau nom existe déjà, rien n'est renommé.
- **Tri :**
  - « Classique » — d'abord par **chemin virtuel**, puis par note décroissante, puis par nom de
    fichier. Le chemin virtuel, ce sont tous les tags du fichier par ordre alphabétique, reliés
    par « / », les chiffres après les lettres, par exemple « Marie/Mer/2024 ». Ainsi, les photos
    ayant le même ensemble de tags restent groupées, quel que soit le dossier où elles se
    trouvent ;
  - « Plus récents d'abord » — les fichiers ajoutés le plus récemment à la galerie viennent en
    premier (pratique après l'ajout d'un nouveau lot de photos), puis comme en Classique ;
  - « Plus gros d'abord » — les fichiers les plus volumineux d'abord (pratique pour libérer de
    la place), puis comme en Classique.
- **« Masquer les autres tags »** (la case au-dessus de la liste des tags) — la liste ne garde
  que les tags des fichiers trouvés par la dernière recherche. `<sans tags>` est toujours affiché.
- **« Filtre des tags… »** (le champ sous cette case) — dès la saisie, la liste ne garde que les
  entrées contenant le texte, sans tenir compte de la casse (`<sans tags>` reste toujours affiché). Le filtre
  s'applique toujours, y compris avec « Masquer les autres tags », et le texte reste jusqu'à ce que
  vous l'effaciez vous-même (la croix du champ ou le clavier) ou que vous fermiez le programme.

Les résultats s'affichent par pages de 16 vignettes ; les boutons sous la grille permettent de
les parcourir. L'info-bulle d'une vignette montre le chemin, la note, la taille et les tags.

## Langue de l'interface

Le programme parle ukrainien, anglais et français. Au premier lancement, une fenêtre avec trois
boutons-drapeaux apparaît ; si vous la fermez, la langue est l'ukrainien et la question sera
reposée la fois suivante. Vous pouvez changer de langue à tout moment dans le menu
**Language** — la fenêtre est traduite immédiatement, sans redémarrage. Le choix est enregistré
dans la même base que les galeries.

## Visionneuse

Un clic sur la vignette d'une photo l'ouvre dans la visionneuse.

| Action | Effet |
|---|---|
| ← → (↑ ↓) | photo précédente / suivante des résultats de recherche, en boucle |
| Double-clic | plein écran / retour à la taille précédente |
| Clic droit | rotation de 90° dans le sens inverse des aiguilles d'une montre |
| Échap | quitter le plein écran ; un second Échap ferme la fenêtre |

Après une rotation, une barre **« Enregistrer dans le fichier »** apparaît. Un JPEG n'est pas
réencodé : seule l'indication d'orientation dans ses métadonnées change, si bien que ni la
qualité ni les données de prise de vue ne sont perdues, quel que soit le nombre de rotations. Les
fichiers PNG, BMP, TIFF et WebP sont réécrits sans perte de qualité. Les GIF, les WebP animés et
les TIFF à plusieurs pages ne peuvent pas être enregistrés : seule leur première image
subsisterait. Si vous fermez la fenêtre ou le programme avec une rotation non enregistrée, le
programme vous demande quoi en faire.

## Vidéos et autres fichiers

- **Les vidéos** (mp4, mov, avi, mpg, mkv, wmv, etc.) s'affichent sous forme d'image, comme
  dans l'Explorateur Windows. Un clic ouvre la vidéo dans le programme que Windows lui associe.
- **Les autres fichiers** s'affichent sous forme de carte avec leur extension et n'ouvrent rien.
- Les fichiers `*.db` et `*.ini`, dont `Thumbs.db` et `desktop.ini`, n'entrent jamais dans une
  galerie. Le dossier du programme lui-même (exe, bibliothèques, base) est également ignoré s'il
  se trouve dans une galerie.

## Le menu « Fichier »

- **Analyser (F5)** — synchronise la base avec le disque : ajoute les nouveaux fichiers, retire
  ceux qui ont disparu, met à jour ceux qui ont changé. Si vous avez déplacé ou renommé un
  fichier, le programme le reconnaît à son contenu : sa note et vos propres tags sont conservés.
- **Ajouter des fichiers à la galerie…** — choisissez des fichiers (plusieurs à la fois si vous
  voulez), puis le dossier de premier niveau de la galerie où les copier. Vous pouvez saisir un
  nouveau nom, le dossier sera créé. Le nom du dossier devient un tag.
- **Ajouter un dossier à la galerie…**
  - si le dossier choisi contient des fichiers, le programme demande dans quel dossier de la
    galerie (tag) copier tout son contenu, sous-dossiers compris ;
  - s'il ne contient que des sous-dossiers, chacun d'eux devient un dossier (tag) distinct de la
    galerie.

  Dans les deux cas, les fichiers sont **copiés**, les originaux restent en place. Les doublons
  sont ignorés. Si un fichier du même nom existe déjà avec un contenu différent, la nouvelle copie
  reçoit le suffixe `(1)`, `(2)`… À la fin, vous voyez combien de fichiers ont été copiés et
  combien de doublons ont été ignorés. Les noms que Windows réserve aux périphériques (`CON`,
  `NUL`, `COM1`…) reçoivent un `_` dans le nom du dossier (`CON_`), tandis que le tag reste sans
  lui (« Con »).
- **Rechercher les doublons…** — des groupes de fichiers au contenu identique. Cochez ceux qui
  sont en trop et cliquez sur « Supprimer » : les fichiers vont dans la **Corbeille**. Si vous
  cochez tous les fichiers d'un groupe, le premier est conservé.
- **Supprimer les fichiers inutiles…** — supprime `Thumbs.db`, `desktop.ini` et les dossiers
  vides de la galerie. La suppression est **définitive, sans passer par la Corbeille** : une liste
  et une confirmation s'affichent donc d'abord. Le dossier du programme n'est pas touché.

Un fichier isolé peut être supprimé dans la fenêtre des métadonnées (clic droit sur une
vignette) : le bouton **« Supprimer le fichier… »** le met à la Corbeille.

Dans la même fenêtre, le bouton **« Exporter… »** copie le fichier hors de la galerie, à
l'emplacement de votre choix. Si cet emplacement se trouve dans la galerie actuelle, le programme
demande d'abord si vous voulez vraiment un doublon ; la copie rejoint alors la galerie selon les
règles habituelles d'ajout : tags tirés des dossiers et, si le nom est déjà pris,
`photo (1).jpg`. La note et les tags ajoutés à la main ne sont pas repris sur la copie.

Le champ **« Nom du fichier : »** de la même fenêtre renomme le fichier sur le disque lorsque vous
cliquez sur « Enregistrer » ; sa note et ses tags sont conservés. Si le renommage échoue (nom déjà
pris ou caractères interdits), le fichier garde son ancien nom et le programme affiche une erreur.

Tant que **« Conserver l'extension lors du renommage »** est activé dans le menu **Paramètres**
(c'est le cas par défaut), le champ n'affiche que le nom sans l'extension, et celle-ci ne peut pas
être modifiée. Si vous le désactivez, le champ affiche le nom complet et l'extension devient
modifiable.

## Mode portable (pour une clé USB)

Le programme cherche la base `xgallery.sqlite` d'abord à côté de son exe, et si elle n'y est
pas, dans `%APPDATA%\Nova30\XGallery\`.

Pour faire une version portable, placez à côté de l'exe votre base venant d'AppData, ou
simplement un fichier vide `xgallery.sqlite`. En mode portable :

- le titre de la fenêtre indique **[Portable]** ; en mode normal, il indique **[Full]** ;
- le dossier racine d'une nouvelle galerie doit se trouver sur le même disque que le programme.
  Son chemin est enregistré par rapport à l'exe, la galerie continue donc de fonctionner même si
  la clé USB reçoit une autre lettre de lecteur. Une galerie sur un autre disque, venue avec une
  base d'AppData, reste modifiable.

Le chemin de la base utilisée s'affiche quelques secondes dans la barre d'état après le
lancement.

## Sauvegarde

Tous les tags, notes et galeries sont enregistrés dans un seul fichier, `xgallery.sqlite`. Pour
faire une sauvegarde, copiez-le pendant que le programme est fermé. Une nouvelle version du
programme peut mettre la base à jour à son premier lancement ; une version plus ancienne ignore
ces changements et ne fonctionnera pas correctement avec une telle base. Faites donc une copie
avant de mettre le programme à jour.

## Licence

Ce projet est distribué sous la [licence MIT](LICENSE).

## Mentions relatives aux tiers et distribution

- [Qt](https://www.qt.io/) — framework d'application ([LGPL v3](licenses/lgpl-3.0.txt))
