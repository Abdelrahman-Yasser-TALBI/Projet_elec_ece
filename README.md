# ESP32-Outlook-Sync

Un petit écran posé sur un bureau qui affiche **les prochains rendez-vous de 3 personnes** (agendas Outlook), tout seul, sans ordinateur.

## L'idée en 30 secondes

1. L'ESP32 (le « cerveau ») se connecte au WiFi.
2. Il demande à Microsoft : « Peux-tu me donner l'agenda de cette personne ? »
3. Microsoft répond : « Prouve que la personne est d'accord. » L'écran affiche alors **un code**. La personne le saisit sur son téléphone, une seule fois.
4. Ensuite, l'ESP32 retient cette autorisation (comme une clé mémorisée) et lit les agendas toutes les 5 minutes.
5. Les rendez-vous s'affichent sur l'écran, triés par heure, avec **une couleur par personne**.

## Ce qu'il faut

- 1 ESP32, 1 écran tactile 2,8" 320x240 (ILI9341 + tactile XPT2046), des fils. **Aucun bouton** : tout se fait au doigt.
- Arduino IDE avec 4 bibliothèques : **ArduinoJson**, **Adafruit GFX Library**, **Adafruit ILI9341**, **XPT2046_Touchscreen**
- Une application Azure (déjà créée : un seul « Client ID » pour tout le monde)

## Branchement

| Broche écran | ESP32 | Rôle |
|---|---|---|
| V | 3V3 | Alimentation |
| G | GND | Masse |
| CK | GPIO 18 (D18) | Horloge SPI |
| SI | GPIO 23 (D23) | Données vers l'écran |
| **SO** | GPIO 19 (D19) | Données du tactile (**obligatoire**) |
| CS | GPIO 5 (D5) | Sélection écran |
| RT | GPIO 22 (D22) | Reset |
| DC | GPIO 21 (D21) | Commande/données |
| TC | GPIO 4 (D4) | Sélection tactile |
| IT | rien | Non utilisé |
| SC | 3V3 | Carte SD désactivée |
| BL | 3V3 | Rétroéclairage (ou GPIO 27, voir veille) |

Le détail pas à pas est dans `PLAN_DE_MONTAGE.md`.

## Installation

1. Ouvrir `ESP32_Outlook_Sync.ino` dans Arduino IDE (le dossier doit s'appeler `ESP32_Outlook_Sync`).
2. Modifier les réglages en haut du fichier : `WIFI_SSID`, `WIFI_PASS`, et les prénoms dans `NOMS`.
3. Carte « ESP32 Dev Module », bon port COM, puis téléverser.
4. **Calibrage** : au premier démarrage, toucher 3 croix affichées à l'écran. Il est enregistré.
5. **Comptes** : l'écran affiche un code pour chaque compte : l'ouvrir sur `microsoft.com/devicelogin`, saisir le code, se connecter, accepter. Le bouton **Passer** (à l'écran) saute un compte.
6. C'est terminé : à chaque rallumage, l'appareil redémarre seul, sans calibrage ni connexion.

## Utilisation (tout au doigt)

- **Onglets en haut** : TOUS, 1, 2, 3 pour choisir quel agenda afficher.
- **Flèches** à droite de la liste : défiler. **Toucher une ligne** : afficher ses détails en bas.
- **Actu** (en bas à droite) : actualiser tout de suite.
- **Menu** (trois traits en haut à droite) : Réglages.
  - toucher un compte : le lier ou le relier ;
  - Calibrer le tactile ;
  - Oublier tous les comptes (deux appuis pour confirmer) ;
  - Retour.
- Icône WiFi : **vert** = tout va bien, **jaune** = données un peu anciennes, **rouge** = pas de WiFi.
- Le point de couleur à gauche de chaque ligne indique à qui appartient l'événement.
- **Veille (facultatif)** : branche BL sur GPIO 27 et mets `PIN_BL = 27` : l'écran s'éteint après 2 minutes et un toucher le rallume.

## En cas de problème

| Symptôme | Solution |
|---|---|
| Rien à l'écran | Vérifier V, G, CS, DC, RT. Si l'image est à l'envers : `ROTATION = 3`, puis recalibrer |
| Le tactile ne répond pas ou décalé | Vérifier SO sur GPIO 19 et TC sur GPIO 4, puis Réglages > Calibrer le tactile |
| Bloqué sur « Connexion WiFi » | Le WiFi doit être en **2,4 GHz** ; vérifier nom et mot de passe |
| Message d'erreur sur la page Microsoft | Prendre le **dernier** code affiché, sans recharger la page |
| Agenda vide | Créer un événement dans les 14 prochains jours |
| Un compte demande de se reconnecter | Normal si Microsoft a révoqué l'accès : saisir le nouveau code |

## Pour la présence Teams (facultatif)

Elle ne fonctionne qu'avec un compte professionnel ou scolaire (ex. ECE). Il faut ajouter la permission `Presence.Read` dans Azure, puis mettre `true` pour ce compte dans `PRESENCE`.

## Où est quoi dans le code ?

| Section | Rôle (version simple) |
|---|---|
| 1. Réglages | Les options à modifier |
| 4. Outils texte | Enlever les accents, raccourcir un texte, formater les dates |
| 5. Réseau | Une fonction qui parle à Internet |
| 6. Authentification | Obtenir et renouveler la « clé » donnée par Microsoft |
| 8. Données | Lire les rendez-vous, trier, mélanger les 3 agendas |
| 9. Affichage | Dessiner l'écran |
| 9. Tactile | Calibrage, conversion du toucher en position écran |
| 10 à 12. Affichage et logique | Écran principal, réglages, horloge, rafraîchissement, reconnexion WiFi |

Le détail complet est dans `EXPLICATION_CODE.md`.
