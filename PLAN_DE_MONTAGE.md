# Plan de montage (écran tactile 2,8", sans bouton)

## Matériel
ESP32 (câble USB data), écran 2,8" 320x240 TFT LCD V1.0 (12 broches : V, G, CK, SI, SO, CS, RT, DC, BL, TC, IT, SC), fils Dupont ou soudure, boîtier imprimé en 3D, alimentation USB 5 V / 1 A.

## 1. Branchement

| Broche écran | ESP32 | Remarque |
|---|---|---|
| V | 3V3 | Si l'écran reste très sombre, essayer VIN (5 V) |
| G | GND | |
| CK | D18 (GPIO 18) | |
| SI | D23 (GPIO 23) | |
| SO | D19 (GPIO 19) | **Obligatoire** : le tactile répond par cette broche |
| CS | D5 (GPIO 5) | |
| RT | D22 (GPIO 22) | |
| DC | D21 (GPIO 21) | |
| TC | D4 (GPIO 4) | Sélection du tactile |
| IT | rien | Non utilisé |
| SC | 3V3 | Maintient la carte SD inactive |
| BL | 3V3 | Rétroéclairage toujours allumé |

Les étiquettes de la carte ESP32 peuvent s'écrire D18, G18, IO18 ou GPIO18 : c'est la même broche. Évite les broches 6 à 11 et TX0/RX0.

## 2. Test à plat (breadboard)
1. Câbler tous les fils ci-dessus.
2. Téléverser l'exemple *Adafruit ILI9341 > graphicstest* (changer les broches CS=5, DC=21, RST=22 si besoin) : l'écran doit s'allumer.
3. Téléverser le programme complet : calibrage (3 croix), puis connexion des comptes.
4. Vérifier : onglets, flèches, toucher d'une ligne, Actu, Réglages.

## 3. Boîtier
1. Essayer l'écran et l'ESP32 dans le support **sans visser**.
2. Prévoir la sortie du câble USB et l'accès au bouton **EN/RST** de la carte.
3. L'écran doit rester entièrement accessible au doigt (pas de cadre qui gêne les bords : les onglets sont en haut et le menu en haut à droite).

## 4. Montage définitif
1. Souder, ou utiliser des Dupont courts avec un point de colle chaude sur chaque connexion.
2. Fixer l'écran, puis l'ESP32 (USB vers la sortie du boîtier).
3. Tester **avant de fermer** : écran, toucher, WiFi.
4. Fermer le boîtier.

## 5. Check-list finale
- [ ] L'écran s'allume, image dans le bon sens (sinon `ROTATION = 3`, puis recalibrer)
- [ ] Le toucher est précis sur les bords (sinon Réglages > Calibrer)
- [ ] WiFi 2,4 GHz connecté, icône verte
- [ ] 3 comptes liés, événements affichés
- [ ] Après débranchement/rebranchement : ni calibrage, ni code à ressaisir

## 6. Option : veille de l'écran
Relier BL à D27 au lieu de 3V3 et mettre `PIN_BL = 27` dans le code. Si l'écran reste noir, revenir à 3V3 (le module n'a pas de transistor sur BL).
