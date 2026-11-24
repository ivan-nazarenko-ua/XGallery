# XGallery

[English](README.md) | [Українська](README.uk.md) | **Français**

Un programme Windows pour organiser une archive de photos et de vidéos. Il mémorise les fichiers
d'un dossier choisi, transforme les noms de dossiers en tags, permet d'ajouter vos propres tags,
regroupe les fichiers ayant les mêmes tags en collections notées, recherche par tags, affiche les
photos, trouve les doublons et garde vos dossiers en ordre.

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

La liste **« Groupes de tags »** de la même fenêtre contient vos propres groupes de tags, par exemple
« Personnes » et « Lieux ». « Ajouter… », « Renommer… » (ou un double-clic) et « Supprimer » modifient
la liste ; « Monter » et « Descendre » déplacent le groupe sélectionné. Les modifications sont
enregistrées avec « Enregistrer ». Supprimer un groupe ne supprime pas ses tags : ils restent
simplement sans groupe.

## Tags

- **Automatiques.** Chaque dossier entre le dossier racine et le fichier devient un tag. Le
  fichier `Vacances/2024/mer.jpg` reçoit les tags « Vacances » et « 2024 ». Une virgule dans un
  nom de dossier est remplacée par une espace.
- **Les vôtres.** Dans une collection (voir plus bas), un clic droit sur la vignette d'un fichier
  ouvre la fenêtre des métadonnées. Vous pouvez y retirer un tag du fichier ou en ajouter. Les
  modifications sont enregistrées avec le bouton « Enregistrer ». Un tag qui n'est plus utilisé nulle part est
  supprimé automatiquement. Un double-clic sur un tag dans cette fenêtre l'ajoute au champ
  « Contient les tags » de la fenêtre principale. La recherche elle-même ne démarre que lorsque
  vous cliquez sur « Rechercher ».
- **Groupes de tags.** Chaque tag appartient à un groupe de la galerie ou à aucun. La liste des
  tags de la fenêtre principale montre d'abord les tags sans groupe, puis les groupes dans l'ordre
  défini dans la fenêtre de modification de la galerie ; dans un groupe, les tags suivent l'ordre
  alphabétique. Chaque groupe commence par son nom en italique gris entre tirets (— Personnes —) :
  ce n'est pas un tag et il ne peut pas être sélectionné. Pour déplacer un tag, faites un clic droit
  dessus dans la liste et choisissez **« Groupe »** → « Sans groupe » ou le groupe voulu ; le groupe
  actuel est coché. Un tag qui n'est plus utilisé nulle part est supprimé avec son appartenance au groupe.
- **Ajouter des tags.** Le bouton « Ajouter des tags » de la fenêtre des métadonnées ouvre une
  fenêtre avec un champ « Nouveaux tags : » (plusieurs tags séparés par des virgules) et un tableau
  de tous les tags de la galerie, groupés comme dans la liste de la fenêtre principale : chaque
  groupe commence une nouvelle ligne, son nom (en gras) précédant ses tags. Les noms longs passent à la ligne ; plus la fenêtre est large, plus il y a de colonnes. Le nombre entre parenthèses après un tag indique combien de collections l'ont (des collections, pas des fichiers). Les tags du fichier sont cochés : cochez ceux à
  ajouter, décochez ceux à retirer, puis cliquez sur
  « Enregistrer ». En modification groupée, les tags que seuls certains des fichiers sélectionnés
  ont sont grisés et ne peuvent pas être modifiés ; saisir un tel tag dans « Nouveaux tags : » le donne
  à tous les fichiers. Pendant qu'elle est ouverte, vous pouvez toujours ouvrir la photo depuis la
  fenêtre des métadonnées ; elle se ferme avec cette dernière. Un clic droit sur un tag du tableau ouvre un menu :
  **« Supprimer… »**, après confirmation, le supprime de **tous les fichiers de la galerie** ; **« Groupe »**
  → « Sans groupe » ou un autre groupe l'y déplace (il apparaît aussitôt dans ce groupe, le groupe actuel
  est coché). Les deux modifient toute la galerie : ils sont enregistrés dès que vous cliquez sur
  « Enregistrer » dans cette fenêtre (« Annuler » ici conserve le tag tel quel ; « Annuler » dans la
  fenêtre des métadonnées ne les défait plus).
- **Copier et coller des tags.** Dans la liste des tags de cette fenêtre (aussi en modification
  groupée), sélectionnez des tags à la souris ou au clavier et appuyez sur Ctrl+C, ou choisissez
  « Copier les tags » dans le menu du clic droit. Ctrl+V ou « Coller les tags » ajoute une liste
  depuis le presse-papiers : les tags sont séparés par des virgules ou des sauts de ligne, les
  caractères `<` `>` `/` `\` sont remplacés par des espaces, l'écriture est l'écriture habituelle
  et les tags déjà présents sont ignorés. Pratique pour donner le même ensemble de tags à
  différents fichiers.
- **Modification groupée.** Dans une collection, cochez les cases dans le coin des vignettes. La
  sélection est conservée quand vous passez d'une page, d'une collection et même d'une recherche à
  l'autre, vous pouvez donc réunir autant de fichiers que vous voulez. Le nombre de fichiers
  sélectionnés est affiché sous la grille (« Sélectionnés : N »). Un clic droit sur n'importe
  quelle vignette de fichier ouvre une
  fenêtre avec les tags communs à tous les fichiers sélectionnés. Les tags ajoutés et retirés
  s'appliquent à tous les fichiers sélectionnés à la fois ; un fichier qui a déjà un tag ajouté ne
  le reçoit qu'une fois. **« Tout cocher »** coche tous les fichiers de la collection ouverte (toutes ses pages), **« Cocher la page »** seulement ceux de la page courante,
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

## Collections

Les fichiers ayant exactement le même ensemble de tags forment une **collection**. Ses tags par
ordre alphabétique, reliés par « / », les chiffres après les lettres, forment le chemin de la
collection, par exemple « Marie/Mer/2024 ». Ainsi, les photos ayant le même ensemble de tags
restent groupées, quel que soit le dossier où elles se trouvent.

- La fenêtre principale affiche les **collections** des fichiers trouvés : une couverture et un nom
  pour chacune. Elles viennent dans l'ordre de leur premier fichier dans les résultats de recherche
  (voir « Tri »), autant par page que la fenêtre peut en contenir.
- **Nom** — par défaut, les deux derniers tags du chemin séparés par une espace (« Mer 2024 »).
  Vous pouvez le changer ; les noms peuvent se répéter. Les fichiers sans tags forment une
  collection sans nom, affichée comme `<sans tags>`.
- **Note** — de 1 à 100, 50 pour une nouvelle collection. La note appartient à la collection, pas
  aux fichiers. Lors de la mise à jour depuis une version où les fichiers avaient une note, chaque
  collection a reçu la note moyenne de ses fichiers, arrondie au supérieur.
- **Couverture** — par défaut, la première photo de la collection par nom de fichier. Pour en
  choisir une autre, ouvrez la fenêtre des métadonnées de la photo dans la collection et cliquez
  sur **« Définir comme couverture »** (effectif après « Enregistrer » ; le bouton est grisé si la
  photo est déjà la couverture, et absent en modification groupée). Si la couverture quitte la
  collection, la première photo redevient la couverture. Une collection qui ne contient que des
  vidéos ou d'autres fichiers montre son premier fichier.
- Un **clic droit** sur une collection ouvre la fenêtre « Propriétés de la collection » : le nom
  (le bouton « Par défaut » rétablit le nom par défaut) et la note. **« Supprimer la collection… »**
  met tous ses fichiers à la Corbeille et les retire de la galerie, après une seule confirmation ; la
  fenêtre de progression a un bouton **« Interrompre »** (les fichiers déjà déplacés restent
  supprimés, les autres restent dans la collection). Un fichier qui ne peut pas aller à la Corbeille
  (trop gros pour elle, ou un disque sans Corbeille) n'est jamais supprimé en silence : le programme
  demande s'il faut le supprimer définitivement (ce fichier ou tous ceux-là) ou interrompre. Si un
  fichier ne peut pas être déplacé pour une autre raison, vous choisissez de continuer (il reste) ou
  d'interrompre.
- Un **clic gauche** ouvre la collection : la grille affiche ses fichiers, page par page. La
  recherche, le tri et la liste des tags sont alors verrouillés ; **« ◀ Fermer la collection »**
  (ou Échap) revient à la liste des collections, à la même page. Dans une collection, un clic sur
  un fichier l'ouvre, un clic droit ouvre sa fenêtre des métadonnées, et la modification groupée
  fonctionne comme d'habitude.
- **Quand les tags changent.** Si une collection perd tous ses fichiers (vous avez changé les tags
  de tous, renommé ou supprimé un tag, déplacé son dossier puis lancé une analyse), son nom, sa
  note et sa couverture passent à la nouvelle collection où sont allés les fichiers. S'il reste au
  moins un fichier, la nouvelle collection reçoit les propriétés par défaut. Si les fichiers
  rejoignent une collection qui existe déjà, celle-ci garde ses propres propriétés. Une collection
  ouverte suit ses fichiers de la même façon ; si elle disparaît, le programme revient à la liste
  des collections.

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
- **Le tri** ordonne les fichiers trouvés ; chaque collection se place là où se trouve son premier
  fichier. Dans une collection ouverte (et dans la visionneuse), les fichiers sont toujours classés
  par nom de fichier, quel que soit le tri :
  - « Classique » — par **note** de la collection (les plus hautes d'abord), puis par **nom** de la
    collection tel que vous le voyez (par ordre alphabétique, sans tenir compte de la casse), puis
    par son chemin (voir plus haut). Par exemple, en
    cherchant « Marie », « Marie/Village/2024 » notée 90 vient avant « Marie/Mer/2024 » notée 10 ;
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

Les résultats s'affichent par pages ; les boutons sous la grille permettent de les parcourir. Une
page contient autant de vignettes que la fenêtre peut en afficher : agrandir la fenêtre ajoute des
colonnes et des lignes, la réduire en retire. La vignette en haut à gauche reste à sa place, si bien
que le numéro de page et le nombre de pages peuvent changer (la première page peut alors être plus
courte que les autres). Dans une fenêtre de moins de 900 pixels de large, la ligne sous la grille
n'affiche que « Page N sur M » ; le nombre de collections et de fichiers est dans son info-bulle. L'info-bulle d'une collection montre son nom,
ses tags, sa note et son nombre de fichiers ; celle d'un fichier, son chemin, sa taille et ses tags.

## Langue de l'interface

Le programme parle ukrainien, anglais et français. Au premier lancement, une fenêtre avec trois
boutons-drapeaux apparaît ; si vous la fermez, la langue est l'ukrainien et la question sera
reposée la fois suivante. Vous pouvez changer de langue à tout moment dans le menu
**Language** — la fenêtre est traduite immédiatement, sans redémarrage. Le choix est enregistré
dans la même base que les galeries.

## Visionneuse

Dans une collection, un clic sur la vignette d'une photo l'ouvre dans la visionneuse.

| Action | Effet |
|---|---|
| ← → (↑ ↓) | photo précédente / suivante de la collection ouverte, en boucle |
| Double-clic | plein écran / retour à la taille précédente |
| Clic droit | rotation de 90° dans le sens inverse des aiguilles d'une montre |
| Suppr | mettre la photo à la Corbeille et la retirer de la galerie (après confirmation) ; la photo suivante s'affiche |
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
  galerie. Le dossier du programme lui-même (exe, bibliothèques, base) et le dossier `thumbs` à
  côté de la base sont également ignorés s'ils se trouvent dans une galerie.
- Les liens vers des dossiers (jonctions NTFS et liens symboliques vers des dossiers) sont ignorés
  par défaut, donc les fichiers vers lesquels ils mènent n'entrent pas dans la galerie. Avec
  **« Entrer dans les dossiers de jonction »** coché dans le menu **Paramètres**, « Analyser » et
  « Ajouter un dossier » les suivent aussi. Activer ou désactiver l'option affiche d'abord une
  fenêtre qui en décrit les conséquences. Dans les deux cas, chaque dossier n'est parcouru qu'une
  fois : un lien qui remonte ne fait pas boucler le programme, et un dossier accessible directement
  et par un lien est enregistré sous son propre chemin. Si l'option est décochée, la prochaine
  analyse retire de la base les fichiers trouvés par des liens, avec leurs tags. « Supprimer les
  fichiers inutiles » n'entre jamais dans ces dossiers, et un lien ne peut pas être choisi comme
  dossier de la galerie où ajouter des fichiers.

## Le menu « Fichier »

- **Analyser (F5)** — synchronise la base avec le disque : ajoute les nouveaux fichiers, retire
  ceux qui ont disparu, met à jour ceux qui ont changé. Si vous avez déplacé ou renommé un
  fichier, le programme le reconnaît à son contenu : vos propres tags sont conservés.
- **Ajouter des fichiers à la galerie…** — choisissez des fichiers (plusieurs à la fois si vous
  voulez), puis le dossier de premier niveau de la galerie où les copier. Vous pouvez saisir un
  nouveau nom, le dossier sera créé. Le nom du dossier devient un tag. Le dernier dossier choisi
  (le même pour « Ajouter des fichiers » et « Ajouter un dossier ») est retenu pour chaque galerie
  et proposé en premier tant qu''il existe.
- **Ajouter un dossier à la galerie…**
  - si le dossier choisi contient des fichiers, le programme demande dans quel dossier de la
    galerie (tag) copier tout son contenu, sous-dossiers compris ;
  - s'il ne contient que des sous-dossiers, chacun d'eux devient un dossier (tag) distinct de la
    galerie.

  Dans les deux cas, les fichiers sont **copiés**, les originaux restent en place. Les doublons
  sont ignorés. Si un fichier du même nom existe déjà avec un contenu différent, la nouvelle copie
  reçoit le suffixe `(1)`, `(2)`… À la fin, vous voyez combien de fichiers ont été copiés et
  combien de doublons ont été ignorés. **« Interrompre »** dans la fenêtre de progression arrête la
  copie après le fichier en cours ; les fichiers déjà copiés restent dans la galerie. Les noms que Windows réserve aux périphériques (`CON`,
  `NUL`, `COM1`…) reçoivent un `_` dans le nom du dossier (`CON_`), tandis que le tag reste sans
  lui (« Con »).
- **Télécharger depuis Internet…** — le gestionnaire de téléchargement, voir
  [Téléchargement depuis Internet](#téléchargement-depuis-internet).
- **Rechercher les doublons…** — des groupes de fichiers au contenu identique. Après le chemin de
  chaque fichier, sa collection est indiquée entre parenthèses ; son info-bulle montre le chemin
  virtuel complet (ses tags). Cochez ceux qui
  sont en trop et cliquez sur « Supprimer » : les fichiers vont dans la **Corbeille**. Si vous
  cochez tous les fichiers d'un groupe, le premier est conservé.
- **Supprimer les fichiers inutiles…** — supprime `Thumbs.db`, `desktop.ini` et les dossiers
  vides de la galerie. La suppression est **définitive, sans passer par la Corbeille** : une liste
  et une confirmation s'affichent donc d'abord. Le dossier du programme n'est pas touché.

Un fichier isolé peut être supprimé dans la fenêtre des métadonnées (clic droit sur la
vignette d'un fichier) : le bouton **« Supprimer le fichier… »** le met à la Corbeille. Dans la visionneuse, la touche Suppr fait de même (sauf dans une visionneuse ouverte depuis la fenêtre des métadonnées : elle y explique seulement pourquoi). Ici, dans « Rechercher les doublons… » et lors de la suppression
d'une collection, un fichier qui ne peut pas aller à la Corbeille (trop gros pour elle, ou un disque
sans Corbeille) n'est jamais supprimé en silence : le programme demande d'abord s'il faut le
supprimer définitivement.

Dans la même fenêtre, le bouton **« Exporter… »** copie le fichier hors de la galerie, à
l'emplacement de votre choix. Si cet emplacement se trouve dans la galerie actuelle, le programme
demande d'abord si vous voulez vraiment un doublon ; la copie rejoint alors la galerie selon les
règles habituelles d'ajout : tags tirés des dossiers et, si le nom est déjà pris,
`photo (1).jpg`. Les tags ajoutés à la main ne sont pas repris sur la copie.

Le champ **« Nom du fichier : »** de la même fenêtre renomme le fichier sur le disque lorsque vous
cliquez sur « Enregistrer » ; ses tags sont conservés. Si le renommage échoue (nom déjà
pris ou caractères interdits), le fichier garde son ancien nom et le programme affiche une erreur.

Tant que **« Conserver l'extension lors du renommage »** est activé dans le menu **Paramètres**
(c'est le cas par défaut), le champ n'affiche que le nom sans l'extension, et celle-ci ne peut pas
être modifiée. Si vous le désactivez, le champ affiche le nom complet et l'extension devient
modifiable.

## Téléchargement depuis Internet

**Fichier → Télécharger depuis Internet…** ouvre le gestionnaire de téléchargement. Il télécharge
une page web, ou les fichiers vers lesquels elle pointe, dans la galerie actuelle. Il n'envoie que
des requêtes GET, une à la fois. La fenêtre a deux onglets : **« Profil »** (la liste des profils et
leurs réglages, affiché en premier) et **« Téléchargement »** (l'adresse de la page et l'accès, les
tags, le dossier racine et le bouton de lancement).

Les **profils** décrivent comment trouver les fichiers sur une page. Ils fonctionnent comme les
sessions enregistrées de PuTTY : saisissez un nom et cliquez sur **« Enregistrer »** (un profil de
ce nom est remplacé après confirmation) ; sélectionnez un profil dans la liste et cliquez sur
**« Charger »** (ou double-cliquez dessus) pour remplir les champs ; **« Supprimer »** retire le
profil sélectionné. Les profils appartiennent au programme, pas à une galerie.
**« Exporter… »** enregistre tous les profils dans un fichier JSON, **« Importer… »** ajoute les
profils d'un tel fichier (les profils portant le même nom sont remplacés).

Un profil contient :

- **User-Agent** — envoyé dans l'en-tête `User-Agent` ; vide, c'est celui de Qt par défaut.
- **Mode** :
  - **Télécharger la page** — la page elle-même est enregistrée sous `page_1.html` (ou
    `page_1.bin` si le serveur indique que ce n'est pas du `text/html`). Les autres champs du
    profil ne servent pas et sont verrouillés.
  - **Télécharger les fichiers de la page** — le programme cherche des liens sur la page selon
    ces champs :
    - **Balise** — n'importe laquelle, `img` ou `a`.
    - **Source des liens** — le nom de l'attribut (`src`, `href`, `data-original`…) d'où le lien
      est tiré. Vide : le texte à l'intérieur de la balise est pris (balises internes retirées).
    - **Classe** — des classes séparées par des espaces : la balise doit les avoir toutes, dans
      n'importe quel ordre. La casse compte, comme en HTML. Vide : n'importe quelle balise.
    - **Attributs** — des paires « attribut = valeur » que la balise doit aussi avoir ; la valeur
      doit être exactement celle-ci, sans tenir compte de la casse. Les espaces autour des noms et
      des valeurs sont retirés à l'enregistrement.

Les liens tirés d'un attribut peuvent être relatifs (`/img/1.jpg`, `../a.jpg`) : ils sont complétés
par l'adresse de la page. Seuls les liens commençant par `http://` ou `https://` sont téléchargés ;
un même lien n'est téléchargé qu'une fois.

Sous le profil :

- **Adresse de la page** — la page à télécharger.
- **Nom d'utilisateur**, **Mot de passe**, **Jeton Bearer** — pour les sites qui demandent une
  connexion. Ils ne sont **jamais enregistrés** et sont effacés à la fermeture de la fenêtre.
  L'authentification Basic est envoyée si le nom ou le mot de passe est rempli, l'en-tête `Bearer`
  si le jeton l'est. Si les deux sont remplis, le jeton accompagne chaque requête et le nom et le
  mot de passe sont fournis quand le serveur les demande. Ils ne sont envoyés **qu'au site de
  l'adresse de la page** : si le serveur redirige vers un autre site (ou de `https` vers `http`
  simple), ils n'y sont pas envoyés.
- **Tags des fichiers téléchargés** — ces tags (et eux seuls) sont donnés à chaque fichier
  téléchargé. « Ajouter des tags » fonctionne comme dans la fenêtre des métadonnées. Un nouveau tag
  que la galerie n'a pas encore reçoit le groupe choisi pour lui quand un téléchargement l'ajoute. La liste est effacée à la fermeture de la fenêtre.
- **Dossier racine** — un dossier de premier niveau de la galerie (ou un nouveau nom). Le dossier
  choisi est retenu pour chaque galerie et proposé la fois suivante (le premier de la liste s'il
  n'existe plus). Chaque
  téléchargement va dans un nouveau dossier numéroté à l'intérieur : le programme trouve les
  dossiers dont le nom commence par un nombre (`1-mer`, `004 - village - 2`, `05 - été`), prend le
  plus grand, ajoute 1 et ajoute `-collection` : ici `006-collection`. En dessous de 1000, le
  nombre a des zéros en tête.

**« Démarrer le téléchargement »** lance le travail. Avant chaque fichier, une pause aléatoire de
0,1 à 3 secondes évite de surcharger le site et de déclencher sa protection contre les robots.
Pendant ce temps, la fenêtre affiche une barre de progression et tout est verrouillé sauf
**« Arrêter le téléchargement »**. Ce qui a été téléchargé avant l'arrêt est tout de même ajouté à
la galerie. Une requête sans données pendant 60 secondes échoue ; les réponses d'erreur (404, etc.)
ne sont pas enregistrées. À la fin, vous voyez combien de liens ont été trouvés, téléchargés et en
erreur, et lesquels.

Un fichier prend le nom de la dernière partie de son lien, sans les paramètres : de
`https://site.com/1280/7/279/82840051/82840051_018_7358.jpg?ver=1` vient `82840051_018_7358.jpg`.
Un lien sans extension (`https://site.com/1280/7/279/82840051`) donne `file_007.jpg`, où 007 est
le numéro du lien sur la page et l'extension suit le `Content-Type` de la réponse du serveur
(`.bin` s'il n'indique rien de connu). Un nom déjà pris reçoit `(1)`, `(2)`… Les doublons ne sont
**pas** ignorés ici : mieux vaut un doublon qu'un fichier manquant ; « Rechercher les doublons… »
les retire.

Les dossiers (racine et `006-collection`) ne donnent aucun tag aux fichiers téléchargés. Une
analyse ne les ajoute pas non plus, car les fichiers sont déjà connus ; ils ne deviennent des tags
que si la galerie est recréée à partir de son dossier (et, comme toujours, un fichier déplacé
reçoit les tags de ses nouveaux dossiers).

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

Tous les tags, collections et galeries sont enregistrés dans un seul fichier, `xgallery.sqlite`. Pour
faire une sauvegarde, copiez-le pendant que le programme est fermé. Une nouvelle version du
programme peut mettre la base à jour à son premier lancement ; une version plus ancienne ignore
ces changements et ne fonctionnera pas correctement avec une telle base. Faites donc une copie
avant de mettre le programme à jour. À partir de cette version, le programme refuse d'ouvrir une
base mise à jour par une version plus récente et le signale. Les fichiers qu'une version plus
ancienne que celle-ci a ajoutés ou supprimés sont remis en ordre (collections) au lancement suivant.

À côté de la base se trouve le dossier **`thumbs`** : des vignettes toutes prêtes, pour que les
pages aux grandes photos s'ouvrent vite. Chaque vignette porte le nom du contenu du fichier (et se range dans
l'un des 256 sous-dossiers, pour qu'aucun dossier ne grossisse trop) : les doublons en partagent donc une, supprimée avec le dernier fichier qui l'utilise. Ce dossier n'a pas
besoin d'être sauvegardé et peut être supprimé à tout moment : le programme refait simplement les
vignettes. Si le dossier ne peut pas être créé ou écrit (par exemple sur une clé USB protégée en
écriture), le programme fonctionne sans lui, simplement plus lentement.

## Licence

Ce projet est distribué sous la [licence MIT](LICENSE).

## Mentions relatives aux tiers et distribution

- [Qt](https://www.qt.io/) — framework d'application ([LGPL v3](licenses/lgpl-3.0.txt))
- [xxHash](https://github.com/Cyan4973/xxHash) 0.8.3 — hachage des tags d'une collection
  ([BSD 2-Clause](third_party/xxhash/LICENSE))
