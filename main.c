/*
  ESP32-Outlook-Sync : afficheur autonome d'agendas Microsoft 365 (3 comptes), 100 % tactile

  Bibliotheques (Gestionnaire de bibliotheques) :
    - ArduinoJson (Benoit Blanchon) 7.x
    - Adafruit GFX Library
    - Adafruit ILI9341
    - XPT2046_Touchscreen (Paul Stoffregen)
  Carte : "ESP32 Dev Module"

  CABLAGE (ecran 2.8" 320x240 ILI9341 + tactile XPT2046, bus SPI partage)
    V  -> 3V3        G  -> GND
    CK -> GPIO18     SI -> GPIO23     SO -> GPIO19   (SO est necessaire pour le tactile)
    CS -> GPIO5      RT -> GPIO22     DC -> GPIO21
    TC -> GPIO4      IT -> libre (non utilise)       SC -> 3V3 (carte SD desactivee)
    BL -> 3V3        (ou GPIO27 pour l'economie d'energie : voir PIN_BL)
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>

// ======================= 1. REGLAGES A MODIFIER =======================
#define NB 3                                             // nombre de comptes
const char* WIFI_SSID = "TUFA145879";                   // WiFi 2,4 GHz
const char* WIFI_PASS = "5W138[o2";
const String CLIENT_ID = "6ee3338f-14f0-44dd-978f-380b0af534b2";  // ID de l'app Azure
const char* NOMS[NB] = {"te", "hanane", "Rayane"};        // 8 lettres max conseillees
const bool PRESENCE[NB] = {false, false, false};         // true = lire la presence Teams (compte pro/ECE uniquement)
const unsigned long REFRESH_MS = 300000UL;               // mise a jour des agendas : 5 min
const int ROTATION = 1;                                  // 1 ou 3 si l'ecran est a l'envers (puis recalibrer)
const int PIN_BL = -1;                                   // -1 : BL sur 3V3. 27 : BL sur GPIO27 = veille de l'ecran
const unsigned long ECO_MS = 120000UL;                   // veille apres 2 min sans toucher (si PIN_BL >= 0)

#define PIN_CS 5
#define PIN_DC 21
#define PIN_RST 22
#define PIN_TCS 4

const String LOGIN = "https://login.microsoftonline.com/common/oauth2/v2.0/";

// ======================= 2. COULEURS (RGB565) =======================
#define C565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
const uint16_t C_BG = C565(11, 17, 32), C_HEAD = C565(21, 30, 50), C_PANEL = C565(20, 28, 45),
               C_SEL = C565(38, 52, 82), C_TXT = C565(240, 244, 250), C_DIM = C565(140, 152, 172),
               C_OK = C565(34, 197, 94), C_WARN = C565(250, 204, 21), C_ERR = C565(239, 68, 68),
               C_CYAN = C565(34, 211, 238);
const uint16_t COUL[NB] = {C565(59, 130, 246), C565(251, 146, 60), C565(168, 85, 247)};  // une couleur par compte

// ======================= 3. DONNEES =======================
struct Evt { char titre[48]; char lieu[32]; char deb[17]; char fin[17]; bool jour; };  // deb/fin = "AAAA-MM-JJTHH:MM"
struct Ref { uint8_t c, k; };                                                           // reference (compte, numero)
struct Compte { String refresh; String access; unsigned long expire; bool reco; };

struct Cal { uint8_t ok, sw, rot; int16_t x1, x2, y1, y2; };    // calibration du tactile (gardee en flash)
Adafruit_ILI9341 tft(PIN_CS, PIN_DC, PIN_RST);
XPT2046_Touchscreen ts(PIN_TCS);
Cal cal;
Preferences prefs;                    // memoire flash : garde les connexions apres coupure
Compte comptes[NB];
Evt evts[NB][15];
int nb[NB];                           // nombre d'evenements par compte
uint8_t presence[NB];                 // 0 inconnu, 1 dispo, 2 occupe, 3 absent, 4 hors ligne
Ref vue[NB * 15];                     // liste affichee (filtree et triee)
int nVue = 0, sel = 0, top = 0, vueCompte = -1;   // vueCompte : -1 = tous, sinon 0..NB-1
unsigned long dernierRefresh = 0, dernierOk = 0, dernierWifi = 0;
unsigned long dernierTouch = 0;
bool ecranEteint = false;
int derniereMin = -1, etatWifiPrec = -1;
#define EV(r) evts[(r).c][(r).k]

// ======================= 4. OUTILS TEXTE =======================
// Encodage URL (les jetons contiennent des caracteres speciaux)
String enc(const String& s) {
  String r; char b[4];
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') r += c;
    else { snprintf(b, sizeof b, "%%%02X", (uint8_t)c); r += b; }
  }
  return r;
}

// Retire les accents (la police de l'ecran ne gere que l'ASCII)
String ascii(const char* s) {
  static const char T[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
  String r;
  while (*s) {
    uint8_t c = *s++;
    if (c < 0x80) { r += (char)c; continue; }
    if (c == 0xC3 && ((uint8_t)*s & 0xC0) == 0x80) { r += T[(uint8_t)*s++ - 0x80]; continue; }
    if (c == 0xE2 && (uint8_t)s[0] == 0x80 && s[1]) {   // apostrophes et tirets typographiques
      uint8_t d = s[1]; s += 2;
      r += (d == 0x98 || d == 0x99) ? '\'' : (d == 0x93 || d == 0x94) ? '-' : (d == 0x9C || d == 0x9D) ? '"' : '.';
      continue;
    }
    while (((uint8_t)*s & 0xC0) == 0x80) s++;           // autre symbole : ignore
  }
  return r;
}

String cle(int i) { return String("rt") + i; }           // cle de stockage du jeton du compte i
String scope(int i) { return String("Calendars.Read%20offline_access") + (PRESENCE[i] ? "%20Presence.Read" : ""); }

// Raccourcit un texte avec ".." pour qu'il tienne dans maxW pixels (la police doit deja etre choisie)
String tronque(String s, int maxW) {
  int16_t x, y; uint16_t w, h;
  tft.getTextBounds(s, 0, 0, &x, &y, &w, &h);
  if (w <= maxW) return s;
  while (s.length() > 1) {
    s.remove(s.length() - 1);
    tft.getTextBounds(s + "..", 0, 0, &x, &y, &w, &h);
    if (w <= maxW) break;
  }
  return s + "..";
}

void centrerDans(const String& s, int x, int w, int base) {   // centre un texte dans une zone de largeur w
  int16_t x1, y1; uint16_t tw, th;
  tft.getTextBounds(s, 0, 0, &x1, &y1, &tw, &th);
  tft.setCursor(x + (w - (int)tw) / 2 - x1, base);
  tft.print(s);
}
void centrer(const String& s, int y) { centrerDans(s, 0, 320, y); }

// Bouton tactile : rectangle arrondi + 1 ou 2 lignes de texte
void bouton(int x, int y, int w, int h, uint16_t fond, uint16_t bord, uint16_t txt, const String& l1, const String& l2 = "") {
  tft.fillRoundRect(x, y, w, h, 8, fond);
  tft.drawRoundRect(x, y, w, h, 8, bord);
  tft.setTextColor(txt);
  tft.setFont(&FreeSansBold9pt7b);
  if (l2 == "") centrerDans(l1, x, w, y + h / 2 + 6);
  else { centrerDans(l1, x, w, y + h / 2 - 3); tft.setFont(&FreeSans9pt7b); centrerDans(l2, x, w, y + h / 2 + 15); }
}

// "AAAA-MM-JJTHH:MM" -> "Auj.", "Dem." ou "Lun 12/10"
String libelleJour(const char* dt) {
  struct tm t = {};
  t.tm_year = atoi(dt) - 1900; t.tm_mon = atoi(dt + 5) - 1; t.tm_mday = atoi(dt + 8); t.tm_hour = 12; t.tm_isdst = -1;
  time_t e = mktime(&t);
  time_t now = time(nullptr); struct tm n; localtime_r(&now, &n);
  n.tm_hour = 12; n.tm_min = 0; n.tm_sec = 0; n.tm_isdst = -1;
  int diff = (int)((e - mktime(&n) + 43200) / 86400);
  if (diff <= 0) return "Auj.";
  if (diff == 1) return "Dem.";
  localtime_r(&e, &t);
  static const char* J[] = {"Dim", "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam"};
  char b[16]; snprintf(b, sizeof b, "%s %02d/%02d", J[t.tm_wday], t.tm_mday, t.tm_mon + 1);
  return String(b);
}

// ======================= 5. RESEAU =======================
// Une seule fonction pour tout : body vide = GET, sinon POST. jeton = "Bearer" Microsoft Graph.
int appel(const String& url, const String& body, const String& jeton, JsonDocument& rep, const JsonDocument* filtre = nullptr) {
  WiFiClientSecure c; c.setInsecure();      // HTTPS sans verification du certificat (prototype)
  HTTPClient h;
  if (!h.begin(c, url)) return -1;
  h.setTimeout(10000);
  if (jeton != "") {
    h.addHeader("Authorization", "Bearer " + jeton);
    h.addHeader("Prefer", "outlook.timezone=\"Europe/Paris\"");   // heures renvoyees en heure de Paris
  }
  int code;
  if (body != "") { h.addHeader("Content-Type", "application/x-www-form-urlencoded"); code = h.POST(body); }
  else code = h.GET();
  if (code > 0) {
    String txt = h.getString();
    if (filtre) deserializeJson(rep, txt, DeserializationOption::Filter(*filtre));
    else deserializeJson(rep, txt);
  }
  h.end();
  Serial.printf("[HTTP %d] %s\n", code, url.substring(0, 70).c_str());
  return code;
}

// ======================= 6. AUTHENTIFICATION =======================
// Range les jetons recus ; le refresh_token est garde en flash pour ne plus jamais se reconnecter
bool enregistrer(int i, JsonDocument& r) {
  if (!r["access_token"].is<const char*>()) return false;
  Compte& c = comptes[i];
  c.access = r["access_token"].as<String>();
  c.expire = millis() + ((r["expires_in"] | 3600) - 120) * 1000UL;   // marge de 2 min
  if (r["refresh_token"].is<const char*>()) {
    c.refresh = r["refresh_token"].as<String>();
    prefs.putString(cle(i).c_str(), c.refresh);
  }
  c.reco = false;
  return true;
}

// Renvoie true si on a un jeton d'acces valable (le renouvelle avec le refresh_token si besoin)
bool tokenValide(int i) {
  Compte& c = comptes[i];
  if (c.access != "" && (long)(millis() - c.expire) < 0) return true;
  if (c.refresh == "") return false;
  JsonDocument r;
  int code = appel(LOGIN + "token", "grant_type=refresh_token&client_id=" + CLIENT_ID + "&refresh_token=" + enc(c.refresh) + "&scope=" + scope(i), "", r);
  if (code == 200 && enregistrer(i, r)) return true;
  if (code == 400 || code == 401) c.reco = true;    // jeton refuse : il faudra se reconnecter
  return false;
}

// ======================= 7. AFFICHAGE : ECRANS DE SERVICE =======================
void ecranMessage(const char* titre, String l1 = "", String l2 = "") {
  tft.fillScreen(C_BG);
  tft.setFont(&FreeSansBold12pt7b); tft.setTextColor(C_TXT); centrer(titre, 95);
  tft.setFont(&FreeSans9pt7b); tft.setTextColor(C_DIM);
  centrer(l1, 130); centrer(l2, 155);
}

void ecranCode(int i, const String& code) {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, 320, 34, COUL[i]);
  tft.setFont(&FreeSansBold12pt7b); tft.setTextColor(C_BG);
  centrer("Compte " + String(i + 1) + " : " + ascii(NOMS[i]), 25);
  tft.setFont(&FreeSans9pt7b); tft.setTextColor(C_TXT);
  centrer("1. Ouvrez sur votre telephone ou PC", 62);
  tft.setFont(&FreeSansBold9pt7b); tft.setTextColor(C_CYAN);
  centrer("microsoft.com/devicelogin", 86);
  tft.setFont(&FreeSans9pt7b); tft.setTextColor(C_TXT);
  centrer("2. Saisissez ce code", 118);
  tft.fillRoundRect(30, 128, 260, 56, 8, C_PANEL);
  tft.drawRoundRect(30, 128, 260, 56, 8, COUL[i]);
  tft.setFont(&FreeSansBold18pt7b); tft.setTextColor(C_TXT);
  centrer(code, 168);
  tft.setFont(&FreeSans9pt7b); tft.setTextColor(C_TXT);
  tft.setCursor(10, 212); tft.print("3. Connectez-vous");
  bouton(226, 196, 88, 38, C_PANEL, C_DIM, C_TXT, "Passer");
}

void ecranCodeReste(int minutes) {
  tft.fillRect(0, 222, 220, 18, C_BG);
  tft.setFont(&FreeSans9pt7b); tft.setTextColor(C_DIM);
  tft.setCursor(10, 236); tft.print("Valable " + String(minutes) + " min");
}

// Connexion d'un compte SANS clavier : Device Code Flow. Renvoie false si l'utilisateur passe.
bool connecterCompte(int i) {
  while (true) {
    JsonDocument d;
    int code = appel(LOGIN + "devicecode", "client_id=" + CLIENT_ID + "&scope=" + scope(i), "", d);
    if (code != 200) { ecranMessage("Erreur reseau", "Nouvel essai dans 5 s"); delay(5000); continue; }
    String dc = d["device_code"].as<String>(), uc = d["user_code"].as<String>();
    int interval = d["interval"] | 5, duree = d["expires_in"] | 900;
    unsigned long fin = millis() + duree * 1000UL;
    Serial.println("Compte " + String(i + 1) + " : code " + uc);
    ecranCode(i, uc);
    while ((long)(millis() - fin) < 0) {
      ecranCodeReste((fin - millis()) / 60000 + 1);
      for (int s = 0; s < interval * 10; s++) {               // attente, avec possibilite de passer
        if (toucheDans(226, 196, 88, 38)) { while (ts.touched()) delay(10); return false; }   // bouton Passer
        delay(100);
      }
      JsonDocument t;
      int c2 = appel(LOGIN + "token", "grant_type=urn:ietf:params:oauth:grant-type:device_code&client_id=" + CLIENT_ID + "&device_code=" + enc(dc), "", t);
      if (c2 == 200 && enregistrer(i, t)) return true;
      String err = t["error"].as<String>();
      if (err == "slow_down") interval += 5;
      else if (err != "authorization_pending") break;        // refuse ou expire : nouveau code
    }
  }
}

// ======================= 8. DONNEES : CALENDRIER ET PRESENCE =======================
bool lireCalendrier(int i) {
  if (time(nullptr) < 100000 || !tokenValide(i)) return false;
  time_t t = time(nullptr); char a[25], b[25];
  strftime(a, sizeof a, "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
  t += 14 * 86400;                                           // fenetre de 14 jours
  strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
  String url = "https://graph.microsoft.com/v1.0/me/calendarView?startDateTime=" + String(a) + "&endDateTime=" + String(b) +
               "&$select=subject,start,end,location,isAllDay&$orderby=start/dateTime&$top=15";
  JsonDocument f;                                            // filtre : on ne garde que les champs utiles
  f["value"][0]["subject"] = true; f["value"][0]["isAllDay"] = true;
  f["value"][0]["start"]["dateTime"] = true; f["value"][0]["end"]["dateTime"] = true;
  f["value"][0]["location"]["displayName"] = true;
  JsonDocument r;
  int code = appel(url, "", comptes[i].access, r, &f);
  if (code == 401) comptes[i].access = "";
  if (code != 200) return false;
  int n = 0;
  for (JsonObject e : r["value"].as<JsonArray>()) {
    if (n >= 15) break;
    Evt& x = evts[i][n++];
    snprintf(x.titre, sizeof x.titre, "%s", ascii(e["subject"] | "(sans titre)").c_str());
    snprintf(x.lieu, sizeof x.lieu, "%s", ascii(e["location"]["displayName"] | "").c_str());
    strncpy(x.deb, e["start"]["dateTime"] | "", 16); x.deb[16] = 0;
    strncpy(x.fin, e["end"]["dateTime"] | "", 16);   x.fin[16] = 0;
    x.jour = e["isAllDay"] | false;
  }
  nb[i] = n;
  return true;
}

void lirePresence(int i) {
  if (!PRESENCE[i] || !tokenValide(i)) return;
  JsonDocument r;
  if (appel("https://graph.microsoft.com/v1.0/me/presence", "", comptes[i].access, r) != 200) { presence[i] = 0; return; }
  String a = r["availability"].as<String>();
  presence[i] = (a == "Available" || a == "AvailableIdle") ? 1 :
                (a == "Busy" || a == "BusyIdle" || a == "DoNotDisturb") ? 2 :
                (a == "Away" || a == "BeRightBack") ? 3 : 4;
}

// Construit la liste affichee : filtre par compte puis tri par date de debut
void construireVue() {
  nVue = 0;
  for (int c = 0; c < NB; c++)
    if (vueCompte < 0 || vueCompte == c)
      for (int k = 0; k < nb[c]; k++) vue[nVue++] = {(uint8_t)c, (uint8_t)k};
  for (int a = 1; a < nVue; a++) {
    Ref x = vue[a]; int b = a - 1;
    while (b >= 0 && strcmp(EV(vue[b]).deb, EV(x).deb) > 0) { vue[b + 1] = vue[b]; b--; }
    vue[b + 1] = x;
  }
  if (sel >= nVue) sel = nVue > 0 ? nVue - 1 : 0;
  if (top > sel) top = sel;
}

// ======================= 9. TACTILE =======================
// Echantillon brut : attend un appui, fait la moyenne, attend le relachement (sert au calibrage)
bool echantillon(int& rx, int& ry) {
  while (!ts.touched()) delay(10);
  delay(80);
  long sx = 0, sy = 0; int n = 0;
  for (int i = 0; i < 12; i++) { if (ts.touched()) { TS_Point p = ts.getPoint(); sx += p.x; sy += p.y; n++; } delay(8); }
  while (ts.touched()) delay(10);
  delay(150);
  if (n < 4) return false;
  rx = sx / n; ry = sy / n;
  return true;
}

void croix(int x, int y, uint16_t c) { tft.drawFastHLine(x - 10, y, 21, c); tft.drawFastVLine(x, y - 10, 21, c); tft.drawCircle(x, y, 6, c); }

// Calibrage en 3 points (haut-gauche, haut-droite, bas-gauche) : detecte aussi l'inversion des axes
void calibrer() {
  const int PX[3] = {20, 300, 20}, PY[3] = {20, 20, 220};
  bool valide = false;
  while (!valide) {
    int rx[3], ry[3];
    for (int i = 0; i < 3; i++) {
      bool ok = false;
      while (!ok) {
        tft.fillScreen(C_BG);
        tft.setFont(&FreeSansBold9pt7b); tft.setTextColor(C_TXT); centrer("Calibrage du tactile", 100);
        tft.setFont(&FreeSans9pt7b); tft.setTextColor(C_DIM); centrer("Touchez le centre de la croix " + String(i + 1) + "/3", 128);
        croix(PX[i], PY[i], C_CYAN);
        ok = echantillon(rx[i], ry[i]);
      }
    }
    cal.sw = abs(ry[1] - ry[0]) > abs(rx[1] - rx[0]);        // axes echanges ?
    if (!cal.sw) { cal.x1 = rx[0]; cal.x2 = rx[1]; cal.y1 = ry[0]; cal.y2 = ry[2]; }
    else         { cal.x1 = ry[0]; cal.x2 = ry[1]; cal.y1 = rx[0]; cal.y2 = rx[2]; }
    valide = abs(cal.x2 - cal.x1) > 400 && abs(cal.y2 - cal.y1) > 400;
  }
  cal.ok = 1; cal.rot = ROTATION;
  prefs.putBytes("cal", &cal, sizeof cal);
  tft.fillScreen(C_BG); tft.setFont(&FreeSansBold12pt7b); tft.setTextColor(C_OK); centrer("Calibrage enregistre", 125);
  delay(800);
}

// Point touche converti en pixels ecran (true si un doigt est pose)
bool pointTouche(int& x, int& y) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  int a = cal.sw ? p.y : p.x, b = cal.sw ? p.x : p.y;
  x = constrain(map(a, cal.x1, cal.x2, 20, 300), 0, 319);
  y = constrain(map(b, cal.y1, cal.y2, 20, 220), 0, 239);
  return true;
}

bool toucheDans(int bx, int by, int bw, int bh) {
  int x, y;
  return pointTouche(x, y) && x >= bx && x < bx + bw && y >= by && y < by + bh;
}

// Un seul evenement par appui (anti-rebond 150 ms)
bool toucher(int& x, int& y) {
  static bool avant = false; static unsigned long t0 = 0;
  if (!ts.touched()) { avant = false; return false; }
  if (avant || millis() - t0 < 150) return false;
  avant = true; t0 = millis();
  delay(25);                                                 // le doigt se stabilise
  return pointTouche(x, y);
}

// ======================= 10. AFFICHAGE : ECRAN PRINCIPAL =======================
// 320x240 : en-tete 34 px (onglets, heure, WiFi, menu) | liste 4 x 36 px + fleches | detail 62 px + bouton Actu
int etatWifi() {                      // 0 ok, 1 donnees anciennes, 2 hors ligne
  if (WiFi.status() != WL_CONNECTED) return 2;
  if (dernierOk == 0 || millis() - dernierOk > 3UL * REFRESH_MS) return 1;
  return 0;
}

uint16_t couleurPresence(int p) {
  return p == 1 ? C_OK : p == 2 ? C_ERR : p == 3 ? C_WARN : p == 4 ? C565(107, 114, 128) : C565(55, 65, 81);
}

void dessinerEntete(bool synchro) {
  tft.fillRect(0, 0, 320, 34, C_HEAD);
  for (int v = -1; v < NB; v++) {                            // onglets : TOUS, 1, 2, 3
    int x = v < 0 ? 4 : 62 + v * 42, w = v < 0 ? 54 : 38;
    uint16_t col = v < 0 ? C_TXT : COUL[v];
    bool actif = (v == vueCompte);
    if (actif) tft.fillRoundRect(x, 4, w, 26, 6, col); else tft.drawRoundRect(x, 4, w, 26, 6, col);
    tft.setFont(&FreeSansBold9pt7b); tft.setTextColor(actif ? C_BG : col);
    centrerDans(v < 0 ? String("TOUS") : String(v + 1), x, w, 22);
    if (v >= 0 && PRESENCE[v]) tft.fillCircle(x + w - 5, 9, 4, couleurPresence(presence[v]));   // presence Teams
  }
  String h = "--:--";
  time_t now = time(nullptr);
  if (now > 100000) { struct tm t; localtime_r(&now, &t); char b[6]; snprintf(b, sizeof b, "%02d:%02d", t.tm_hour, t.tm_min); h = b; }
  tft.setFont(&FreeSansBold9pt7b); tft.setTextColor(C_TXT);
  if (synchro) { h = "Synchro"; tft.setFont(&FreeSans9pt7b); tft.setTextColor(C_WARN); }
  int16_t x1, y1; uint16_t w, hh;
  tft.getTextBounds(h, 0, 0, &x1, &y1, &w, &hh);
  tft.setCursor(250 - w - x1, 22); tft.print(h);
  int e = etatWifi();
  uint16_t cw = e == 0 ? C_OK : e == 1 ? C_WARN : C_ERR;
  for (int b = 0; b < 4; b++) tft.fillRect(258 + b * 5, 24 - (3 + b * 3), 3, 3 + b * 3, cw);   // icone WiFi
  for (int b = 0; b < 3; b++) tft.fillRect(290, 9 + b * 7, 20, 3, C_TXT);                       // icone menu
}

void dessinerFleches() {
  bool haut = top > 0, bas = top + 4 < nVue;
  tft.fillRoundRect(284, 38, 32, 66, 6, C_PANEL);
  tft.fillRoundRect(284, 108, 32, 66, 6, C_PANEL);
  tft.fillTriangle(300, 62, 291, 80, 309, 80, haut ? C_TXT : C_SEL);
  tft.fillTriangle(291, 132, 309, 132, 300, 150, bas ? C_TXT : C_SEL);
}

void dessinerListe() {
  tft.fillRect(0, 34, 320, 144, C_BG);
  dessinerFleches();
  if (nVue == 0) {
    tft.setFont(&FreeSans9pt7b); tft.setTextColor(C_DIM);
    centrerDans("Aucun evenement a venir", 0, 280, 112);
    return;
  }
  for (int r = 0; r < 4 && top + r < nVue; r++) {
    int k = top + r, y = 34 + r * 36;
    Ref R = vue[k]; Evt& e = EV(R);
    tft.fillRect(0, y, 280, 36, k == sel ? C_SEL : C_BG);
    tft.fillRoundRect(2, y + 4, 5, 28, 2, COUL[R.c]);                       // pastille couleur du compte
    tft.setFont(&FreeSansBold9pt7b); tft.setTextColor(C_TXT);
    tft.setCursor(14, y + 15); tft.print(e.jour ? String("Jour") : String(e.deb + 11));
    tft.setCursor(68, y + 15); tft.print(tronque(e.titre, 206));
    String suite = vueCompte < 0 ? ascii(NOMS[R.c]) : String(e.lieu);
    String l2 = libelleJour(e.deb);
    if (suite.length()) l2 += "  -  " + suite;
    tft.setFont(&FreeSans9pt7b); tft.setTextColor(C_DIM);
    tft.setCursor(14, y + 32); tft.print(tronque(l2, 262));
  }
}

void dessinerDetail(bool synchro) {
  tft.fillRect(0, 178, 320, 62, C_PANEL);
  bouton(246, 186, 70, 46, C_SEL, C_DIM, C_TXT, synchro ? "..." : "Actu");
  if (nVue == 0) return;
  Ref R = vue[sel]; Evt& e = EV(R);
  tft.drawFastHLine(0, 178, 320, COUL[R.c]);
  tft.setFont(&FreeSansBold9pt7b); tft.setTextColor(C_TXT);
  tft.setCursor(8, 198); tft.print(tronque(e.titre, 232));
  String l = libelleJour(e.deb) + " " + (e.jour ? String("Journee") : String(e.deb + 11) + "-" + String(e.fin + 11)) + "  " + ascii(NOMS[R.c]);
  tft.setFont(&FreeSans9pt7b); tft.setTextColor(C_DIM);
  tft.setCursor(8, 218); tft.print(tronque(l, 232));
  if (e.lieu[0]) {
    tft.setFont(); tft.setTextSize(1); tft.setTextColor(C_DIM);
    tft.setCursor(8, 228); tft.print(String("Lieu : ") + String(e.lieu).substring(0, 34));
  }
}

void dessinerTout() { dessinerEntete(false); dessinerListe(); dessinerDetail(false); }

// ======================= 11. REGLAGES =======================
void dessinerReglages(bool confirmer) {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, 320, 34, C_HEAD);
  tft.setFont(&FreeSansBold9pt7b); tft.setTextColor(C_TXT); tft.setCursor(10, 22); tft.print("Reglages");
  for (int i = 0; i < NB; i++)
    bouton(8 + i * 104, 46, 96, 50, C_PANEL, COUL[i], C_TXT, ascii(NOMS[i]), comptes[i].refresh == "" ? "non lie" : "lie");
  bouton(8, 108, 148, 50, C_PANEL, C_CYAN, C_TXT, "Calibrer", "le tactile");
  bouton(164, 108, 148, 50, confirmer ? C_ERR : C_PANEL, C_ERR, C_TXT, confirmer ? "Confirmer ?" : "Oublier", confirmer ? "toucher a nouveau" : "tous les comptes");
  bouton(8, 176, 304, 52, C_SEL, C_DIM, C_TXT, "Retour");
}

void actualiser();   // defini plus bas

void ecranReglages() {
  bool conf = false, change = false;
  dessinerReglages(false);
  while (true) {
    int x, y;
    if (!toucher(x, y)) { delay(10); continue; }
    if (y >= 46 && y < 96) {                                 // lier / relier un compte
      for (int i = 0; i < NB; i++)
        if (x >= 8 + i * 104 && x < 104 + i * 104) { connecterCompte(i); change = true; conf = false; dessinerReglages(false); }
    } else if (y >= 108 && y < 158) {
      if (x < 160) { calibrer(); conf = false; dessinerReglages(false); }
      else if (!conf) { conf = true; dessinerReglages(true); }                     // 1er appui : demande de confirmation
      else {                                                                        // 2e appui : on oublie tout
        for (int i = 0; i < NB; i++) { prefs.remove(cle(i).c_str()); comptes[i].refresh = ""; comptes[i].access = ""; nb[i] = 0; presence[i] = 0; }
        change = true; conf = false; dessinerReglages(false);
      }
    } else if (y >= 176) break;
  }
  if (change) actualiser(); else { construireVue(); dessinerTout(); }
}

// ======================= 12. LOGIQUE =======================
void actualiser() {
  dessinerEntete(true);
  dessinerDetail(true);
  bool ok = false;
  for (int i = 0; i < NB; i++) {
    if (comptes[i].refresh == "") continue;                 // compte non lie : ignore
    if (lireCalendrier(i)) { ok = true; lirePresence(i); }
    if (comptes[i].reco) {                                  // jeton refuse : reconnexion sur l'ecran
      if (connecterCompte(i) && lireCalendrier(i)) { ok = true; lirePresence(i); }
    }
  }
  if (ok) dernierOk = millis();
  dernierRefresh = ok ? millis() : millis() - REFRESH_MS + 30000UL;   // echec : nouvel essai dans 30 s
  construireVue();
  dessinerTout();
}

void choisirVue(int v) { vueCompte = v; sel = 0; top = 0; construireVue(); dessinerTout(); }

void traiterTouche(int x, int y) {
  if (y < 34) {                                              // en-tete : onglets ou menu
    if (x >= 278) { ecranReglages(); return; }
    if (x < 60) choisirVue(-1);
    else { int v = (x - 60) / 42; if (v >= 0 && v < NB) choisirVue(v); }
    return;
  }
  if (y >= 178) { if (x >= 240) actualiser(); return; }      // bouton Actu
  if (x >= 280) {                                            // fleches haut / bas
    if (y < 106) { if (top > 0) top--; } else { if (top + 4 < nVue) top++; }
    if (sel < top) sel = top;
    if (sel > top + 3) sel = top + 3;
    if (sel >= nVue) sel = nVue > 0 ? nVue - 1 : 0;
    dessinerListe(); dessinerDetail(false);
    return;
  }
  int r = (y - 34) / 36;                                     // appui sur une ligne
  if (top + r < nVue) { sel = top + r; dessinerListe(); dessinerDetail(false); }
}

void connecterWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  ecranMessage("Connexion WiFi", WIFI_SSID, "Patientez...");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    if (millis() - t0 > 20000) { WiFi.disconnect(); WiFi.begin(WIFI_SSID, WIFI_PASS); t0 = millis(); }
  }
}

void setup() {
  Serial.begin(115200);
  if (PIN_BL >= 0) { pinMode(PIN_BL, OUTPUT); digitalWrite(PIN_BL, HIGH); }
  tft.begin(); tft.setRotation(ROTATION); tft.fillScreen(C_BG);
  ts.begin();
  prefs.begin("outlook", false);
  size_t n = prefs.getBytes("cal", &cal, sizeof cal);
  if (n != sizeof cal || !cal.ok || cal.rot != ROTATION) calibrer();      // premier demarrage : calibrage du tactile

  connecterWiFi();
  ecranMessage("Mise a l'heure", "Serveur NTP...");
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com");   // heure de Paris, ete/hiver auto
  unsigned long t0 = millis();
  while (time(nullptr) < 100000 && millis() - t0 < 15000) delay(200);

  for (int i = 0; i < NB; i++) comptes[i].refresh = prefs.getString(cle(i).c_str(), "");
  for (int i = 0; i < NB; i++) if (comptes[i].refresh == "") connecterCompte(i);   // premier demarrage uniquement
  ecranMessage("Synchronisation", "Lecture des agendas...");
  actualiser();
  dernierTouch = millis();
}

void loop() {
  int x, y;
  if (toucher(x, y)) {
    dernierTouch = millis();
    if (ecranEteint) { digitalWrite(PIN_BL, HIGH); ecranEteint = false; }   // 1er appui : reveil seulement
    else traiterTouche(x, y);
  }

  time_t now = time(nullptr); struct tm t; localtime_r(&now, &t);
  int e = etatWifi();
  if (t.tm_min != derniereMin || e != etatWifiPrec) { derniereMin = t.tm_min; etatWifiPrec = e; dessinerEntete(false); }

  if (millis() - dernierRefresh > REFRESH_MS) actualiser();

  if (WiFi.status() != WL_CONNECTED && millis() - dernierWifi > 15000) {   // reconnexion automatique
    dernierWifi = millis(); WiFi.disconnect(); WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
  if (PIN_BL >= 0 && !ecranEteint && millis() - dernierTouch > ECO_MS) { digitalWrite(PIN_BL, LOW); ecranEteint = true; }   // veille
  delay(10);
}
