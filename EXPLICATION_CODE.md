# Explication détaillée du code

## 1. Vue d'ensemble

Le programme enchaîne trois phases :

1. **Démarrage** (`setup`) : écran, WiFi, heure, chargement des jetons en mémoire flash, connexion des comptes manquants.
2. **Synchronisation** (`actualiser`) : pour chaque compte, lecture du calendrier (et de la présence si activée).
3. **Boucle** (`loop`) : lecture du toucher, mise à jour de l'horloge, rafraîchissement toutes les 5 minutes, reconnexion WiFi.

## 2. Les trois structures de données

- `Evt` : un événement (titre, lieu, début, fin, journée entière). Les dates sont gardées en texte `AAAA-MM-JJTHH:MM` : le tri alphabétique donne directement l'ordre chronologique, sans conversion de fuseau.
- `Compte` : les jetons d'un compte (`refresh`, `access`), l'heure d'expiration et un indicateur « reconnexion nécessaire ».
- `Ref` : une référence « (compte, numéro) » vers un événement. La liste affichée est un tableau de `Ref`, ce qui permet de filtrer et trier sans copier les événements.

## 3. Authentification (sections 6 et 7)

### Pourquoi ce fonctionnement ?
L'ESP32 n'a ni clavier ni navigateur. Le **Device Code Flow** (OAuth 2.0) est fait pour cela : l'appareil obtient un code, l'utilisateur le saisit sur un autre appareil.

### `connecterCompte(i)`
1. POST sur `/devicecode` avec le Client ID et les permissions → Microsoft renvoie `device_code` (secret de l'ESP32), `user_code` (à afficher), `interval` et `expires_in`.
2. `ecranCode` affiche le code sur l'écran TFT.
3. Toutes les `interval` secondes, POST sur `/token` avec le `device_code`.
   - `authorization_pending` : l'utilisateur n'a pas encore validé, on attend.
   - `slow_down` : on allonge l'intervalle.
   - autre erreur (code expiré, refusé) : on redemande un nouveau code.
   - HTTP 200 : on a les jetons.
4. Le bouton 4 permet de passer le compte.

### `enregistrer(i, r)`
Range `access_token` (valable ~1 h) et `refresh_token` (valable ~90 jours, renouvelé à chaque usage). Le `refresh_token` est écrit en flash avec `Preferences` : c'est ce qui rend l'appareil **autonome** après une coupure de courant.

### `tokenValide(i)`
Renvoie vrai si le jeton d'accès est encore valable (marge de 2 minutes). Sinon, il envoie le `refresh_token` pour obtenir un nouveau jeton. Si Microsoft répond 400 ou 401, le compte est marqué `reco` : l'écran affichera un nouveau code.

## 4. Réseau (`appel`)

Une seule fonction pour tous les échanges :
- `body` rempli → requête **POST** (formulaire encodé) ; vide → **GET**.
- `jeton` rempli → en-tête `Authorization: Bearer ...` et `Prefer: outlook.timezone="Europe/Paris"` (heures renvoyées en heure de Paris).
- `setInsecure()` : HTTPS sans vérification du certificat. C'est un choix de prototype (économie de mémoire et de configuration) ; en production, on embarquerait le certificat racine.
- Un **filtre ArduinoJson** ne conserve que les champs utiles, ce qui réduit fortement la mémoire utilisée.
- Une requête coûte environ 1 à 2 s (négociation TLS), d'où le rafraîchissement toutes les 5 minutes.

## 5. Lecture des données (section 8)

### `lireCalendrier(i)`
- Endpoint : `GET /me/calendarView?startDateTime=...&endDateTime=...` (fenêtre de 14 jours à partir de maintenant, en UTC).
- `calendarView` renvoie aussi les événements **en cours** et déplie les événements récurrents.
- `$select` limite les champs ; `$orderby` trie ; `$top=15` borne le volume.
- Chaque événement est copié dans `evts[i][n]` après suppression des accents (`ascii`), car la police de l'écran ne gère que l'ASCII.

### `lirePresence(i)`
Appelle `/me/presence` (permission `Presence.Read`) chaque minute et traduit `availability`/`activity` en statuts : disponible, occupé, en réunion, en appel, ne pas déranger, absent, hors ligne. Si l'appel échoue (compte personnel, consentement refusé), `presKo` évite de réessayer et `majStatuts` déduit « En réunion » ou « Libre » à partir de l'agenda. Le choix « Sans Teams » est mémorisé en flash (`ps<i>`).

### `construireVue()`
Rassemble les événements du compte choisi (ou des trois), puis les trie par **tri par insertion** sur la date de début. Complexité sans importance : 45 éléments au maximum.

## 6. Tactile (section 9)

L'écran possède une puce tactile XPT2046 qui partage le bus SPI de l'écran (SCK, MOSI, MISO) et possède sa propre sélection (TC, GPIO 4). La broche SC (carte SD) est reliée à 3V3 pour que la SD reste inactive.

- **Calibrage en 3 points** (`calibrer`) : l'utilisateur touche trois croix (haut-gauche, haut-droite, bas-gauche). `echantillon` fait la moyenne de 12 mesures. Le programme déduit si les axes sont échangés (`sw`) et les valeurs brutes correspondant aux bords de l'écran. Le résultat est enregistré en flash : il n'est demandé qu'une fois. Il se refait aussi depuis les Réglages.
- **Conversion** (`pointTouche`) : un `map` transforme la mesure brute en pixels 0 à 319 et 0 à 239.
- **Un événement par appui** (`toucher`) : détection du front montant, anti-rebond de 150 ms et courte attente (25 ms) pour que le doigt se stabilise.
- `toucheDans` teste si le doigt est dans un rectangle (utilisé par le bouton « Passer » pendant la connexion d'un compte).

## 7. Affichage tactile (section 10)

Écran 320 x 240, paysage, zones tactiles d'au moins 32 px :

| Zone | Contenu | Action |
|---|---|---|
| En-tête | Nom/statut, heure, Actu, WiFi, menu | Actualiser, Réglages |
| Vue Équipe | 3 cartes : statut Teams coloré + prochain rendez-vous | Ouvre l'agenda de la personne |
| Liste | 4 lignes de 44 px, flèches haut/bas | Ouvre le détail |
| Détail | Titre sur plusieurs lignes (retour à la ligne), horaires, lieu | Retour |
| Barre d'onglets (bas) | Équipe, Tous, 1, 2, 3 | Changer de vue |

Choix : une couleur par personne, redessin partiel (sans clignotement), texte raccourci à la largeur réelle, jour relatif (« Auj. », « Dem. »), flèches grisées en bout de liste.

## 8. Réglages (section 11)

L'écran Réglages (icône à trois traits) remplace les anciens boutons physiques :
- toucher un compte lance sa connexion (Device Code Flow) ;
- « Calibrer le tactile » relance le calibrage ;
- « Oublier tous les comptes » demande une confirmation par un second appui, puis efface les jetons de la flash.

## 9. Logique principale (section 12)

- La boucle lit le toucher, redessine l'horloge à chaque changement de minute et lance la synchronisation toutes les 5 minutes.
- **Robustesse** : si une synchronisation échoue, nouvel essai après 30 s ; si le WiFi tombe, `WiFi.begin` est relancé toutes les 15 s ; l'heure passe par NTP avec le fuseau `CET-1CEST` (heure d'été/hiver automatique).
- **Veille de l'écran** (facultative, `PIN_BL`) : après 2 minutes sans toucher, le rétroéclairage s'éteint ; le premier toucher le rallume sans déclencher d'action.

## 10. Limites connues

- Le TLS sans vérification de certificat est acceptable pour un prototype, pas pour un produit.
- La présence Teams réelle exige un compte pro/scolaire et parfois un consentement admin ; sinon statut déduit de l'agenda.
- Le calibrage dépend de la rotation de l'écran : il se refait si `ROTATION` change.
- Les caractères accentués sont simplifiés (é devient e).
- Un compte non utilisé pendant plus de 90 jours perd son jeton : il faut le reconnecter.
