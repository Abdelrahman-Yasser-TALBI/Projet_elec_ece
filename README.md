# ESP32-Outlook-Sync

**Un petit écran tactile posé sur un bureau qui affiche les prochains rendez-vous de trois personnes (agendas Outlook), tout seul, sans ordinateur.**

Projet réalisé à l'ECE Engineering School (ING3, Digital Transformation & Innovation) par :

| Membre | Couleur sur l'écran |
|---|---|
| **Yasser** | Bleu |
| **Hanane** | Orange |
| **Rayane** | Violet |

## L'idée en 30 secondes

1. L'ESP32 (le « cerveau ») se connecte au WiFi.
2. Il demande à Microsoft : « Peux-tu me donner l'agenda de cette personne ? »
3. Microsoft répond : « Prouve que la personne est d'accord. » L'écran affiche alors **un code**. La personne le saisit sur son téléphone, une seule fois.
4. L'ESP32 retient cette autorisation (comme une clé mémorisée) et relit les agendas toutes les 5 minutes.
5. Les rendez-vous s'affichent triés par heure, avec **une couleur par personne**. Tout se commande **au doigt**, sans bouton.

## Ce qu'il faut

- 1 ESP32, 1 écran tactile 2,8" 320x240 (ILI9341 + tactile XPT2046), des fils
- Arduino IDE avec 4 bibliothèques : **ArduinoJson**, **Adafruit GFX Library**, **Adafruit ILI9341**, **XPT2046_Touchscreen**
- Une application Azure (un seul « Client ID » sert aux trois comptes)

## Branchement

| Broche écran | ESP32 | Rôle |
|---|---|---|
| V | 3V3 | Alimentation |
| G | GND | Masse |
| CK | GPIO 18 (D18) | Horloge |
| SI | GPIO 23 (D23) | Données vers l'écran |
| **SO** | GPIO 19 (D19) | Données du tactile (obligatoire) |
| CS | GPIO 5 (D5) | Sélection de l'écran |
| RT | GPIO 22 (D22) | Reset |
| DC | GPIO 21 (D21) | Commande/données |
| TC | GPIO 4 (D4) | Sélection du tactile |
| IT | rien | Non utilisé |
| SC | 3V3 | Carte SD désactivée |
| BL | 3V3 | Rétroéclairage |

Le détail pas à pas est dans `PLAN_DE_MONTAGE.md`.

## Installation

1. Installer les 4 bibliothèques (Tools → Manage Libraries).
2. Ouvrir `ESP32_Outlook_Sync.ino` dans Arduino IDE (le dossier doit s'appeler `ESP32_Outlook_Sync`).
3. Choisir la carte : *Select other board and port* → **ESP32 Dev Module** → port COM de la carte.
4. Modifier en haut du fichier `WIFI_SSID` et `WIFI_PASS` (WiFi en **2,4 GHz**). Les prénoms sont déjà réglés : Yasser, Hanane, Rayane.
5. Téléverser (flèche →).
6. **Calibrage** : au premier démarrage, toucher les 3 croix affichées. C'est enregistré.
7. **Comptes** : l'écran affiche un code pour chaque personne. Chacune l'ouvre sur `microsoft.com/devicelogin`, saisit le code, se connecte avec **son** compte Microsoft et accepte. Le bouton **Passer** saute un compte.
8. C'est terminé : à chaque rallumage, l'appareil redémarre seul, sans calibrage ni nouvelle connexion.

## Utilisation (tout au doigt)

- **Onglets en haut** : TOUS, 1 (Yasser), 2 (Hanane), 3 (Rayane).
- **Flèches** à droite : faire défiler. **Toucher une ligne** : détails en bas.
- **Actu** (en bas à droite) : actualiser tout de suite.
- **Menu** (trois traits en haut à droite) : Réglages (relier un compte, calibrer, tout oublier).
- **Icône WiFi** : vert = tout va bien, jaune = données un peu anciennes, rouge = pas de WiFi.
- **Point de couleur** à gauche de chaque ligne : à qui appartient l'événement.
- **Veille (facultatif)** : brancher BL sur GPIO 27 et mettre `PIN_BL = 27` dans le code. L'écran s'éteint après 2 minutes, un toucher le rallume.

## En cas de problème

| Symptôme | Solution |
|---|---|
| « Missing FQBN » à la compilation | Aucune carte sélectionnée : choisir ESP32 Dev Module |
| « No such file » à la compilation | Une bibliothèque manque : l'installer, puis relancer |
| Rien à l'écran | Vérifier V, G, CS, DC, RT. Image à l'envers : `ROTATION = 3`, puis recalibrer |
| Le tactile ne répond pas ou est décalé | Vérifier SO sur GPIO 19 et TC sur GPIO 4, puis Réglages > Calibrer |
| Bloqué sur « Connexion WiFi » | WiFi en 2,4 GHz ; vérifier nom et mot de passe |
| Erreur sur la page Microsoft | Saisir le **dernier** code affiché, sans recharger la page |
| Agenda vide | Créer un événement dans les 14 prochains jours |
| Un compte redemande un code | Normal si Microsoft a révoqué l'accès : saisir le nouveau code |

## Pour la présence Teams (facultatif)

Elle ne fonctionne qu'avec un compte professionnel ou scolaire (par exemple ECE). Il faut ajouter la permission `Presence.Read` dans Azure, puis mettre `true` pour ce compte dans `PRESENCE`.

## Où est quoi dans le code ?

| Section | Rôle (version simple) |
|---|---|
| 1. Réglages | Les options à modifier |
| 4. Outils texte | Enlever les accents, raccourcir un texte, formater les dates |
| 5. Réseau | Une fonction qui parle à Internet |
| 6. Authentification | Obtenir et renouveler la « clé » donnée par Microsoft |
| 8. Données | Lire les rendez-vous, trier, mélanger les 3 agendas |
| 9. Tactile | Calibrage, conversion du toucher en position sur l'écran |
| 10 à 12. Affichage et logique | Écran principal, réglages, horloge, rafraîchissement, reconnexion WiFi |

Le détail complet est dans `EXPLICATION_CODE.md`.
