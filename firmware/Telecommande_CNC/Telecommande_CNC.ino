/*
  ============================================================
   TÉLÉCOMMANDE CNC Wi-Fi  –  TwoTrees TTC450 (carte MKS DLC32 / GRBL)
   Écran 2,8" tactile + ESP32-S3 Freenove
  ============================================================
   Carte       : Freenove ESP32-S3-WROOM
   Dans l'IDE  : Outils > Type de carte > "ESP32S3 Dev Module"
                 Flash Size 8MB, PSRAM "OPI PSRAM", USB CDC On Boot "Disabled",
                 Partition Scheme "8M with spiffs (3MB APP/1.5MB SPIFFS)"
   Bibliothèque: LovyanGFX (Gestionnaire de bibliothèques)
   Écran       : 2,8" SPI 320x240 (ILI9341) avec tactile résistif XPT2046
                 -> si l'écran est en ST7789, changer ECRAN_ST7789 plus bas

   La télécommande se connecte au Wi-Fi de la CNC et lui parle directement
   (port 8080 ou 23, trouvé tout seul). Les déplacements "jog" s'arrêtent dès qu'on lâche le joystick.

   BROCHAGE (boutons : une patte sur la broche, l'autre au GND)
   ---------------------------------------------------------------
   Joystick X/Y    VRx -> GPIO1   VRy -> GPIO2   SW -> GPIO6 (change le pas)
   Joystick Z      VRy -> GPIO4                  SW -> GPIO7 (rapide / lent)
   Potentiomètre AVANCE %  curseur -> GPIO5  (extrémités sur 3V3 et GND)
   Joysticks et potentiomètre alimentés en 3V3 (PAS en 5 V !)
   Touche ZÉRO XY  -> GPIO15  (maintenir 1 s)
   Touche ZÉRO Z   -> GPIO16  (maintenir 1 s)
   Touche PAUSE    -> GPIO17  (pause / reprise)
   Bouton ARRÊT    -> GPIO18  (arrêt immédiat + reset GRBL)
   Voyant LED      -> GPIO8 via une résistance de 220 ohms (470 ohms possible)
   Charge batterie -> GPIO42 relié à la broche CHRG du TP4056 (le voyant "respire" pendant la charge)
   Écran  SCK 12  MOSI(SDI) 11  MISO(SDO) 13  CS 10  DC 9  RESET 14  LED(BL) 21
   Tactile T_CLK 12  T_DIN 11  T_DO 13  T_CS 39  T_IRQ 40
   (ne pas utiliser les GPIO 35/36/37 = mémoire PSRAM, ni 19/20 = USB)

   Sur l'écran : PAS, VITESSE, ORIGINE, RETOUR 0, SONDE Z, DÉBLOQUER.
   MENU (toucher la barre du haut) : choisir le Wi-Fi (recherche des réseaux +
   clavier), voir les adresses IP, changer l'IP de la CNC, chercher la CNC
   automatiquement sur le réseau, recalibrer l'écran. Tout est mémorisé.
   SONDE Z : palpage automatique avec la plaque de touche de la TTC450.

   MODE IMPRIMANTE 3D (Creality K2, Klipper) : MENU > bouton du haut "MACHINE".
   Joysticks = déplacements de la tête et du plateau (bloqués pendant une impression),
   ORIGINE = G28, CHAUFFE PLA / STOP CHAUFFE, ANNULER IMP., MOTEURS OFF / REDEMARRER,
   PAUSE = pause / reprise de l'impression, ARRÊT = arrêt d'urgence Klipper,
   potentiomètre = vitesse d'impression (%).

   Calibrage du tactile : se fait tout seul au premier démarrage.
   Pour le refaire : maintenir PAUSE en allumant la télécommande.

   MISES À JOUR SANS LES BOUTONS BOOT / RESET (carte retournée dans le boîtier) :
   1) Par Wi-Fi (le plus simple) : dans l'IDE Arduino, Outils > Port, choisir le
      port réseau "telecommande-cnc" puis Téléverser. Mot de passe : MDP_MAJ.
   2) Par câble sur la prise USB-UART : le téléversement est automatique
      (la carte se met seule en mode programmation).
   3) Secours (si le programme ne démarre plus bien) : maintenir ZÉRO XY + ZÉRO Z
      en allumant -> la carte redémarre en mode programmation ; téléverser par
      la prise USB-OTG. Trous BOOT / RST sous le boîtier pour un trombone en dernier recours.

   ATTENTION : le bouton ARRÊT passe par le Wi-Fi. Ce n'est PAS un arrêt
   d'urgence. Garde l'interrupteur de la machine à portée de main.
*/

#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <lwip/sockets.h>
#include <LovyanGFX.hpp>
#include "soc/rtc_cntl_reg.h"

// ================== RÉGLAGES À MODIFIER ==================
const char* WIFI_NOM   = "MonWifi";        // valeurs de départ : modifiables ensuite dans le MENU
const char* WIFI_MDP   = "MotDePasse";
const char* CNC_IP     = "192.168.1.50";   // adresse IP de la TTC450
const uint16_t CNC_PORT = 8080;          // MKS DLC32 : 8080 (ou 23 si telnet activé) – trouvé tout seul
// Si la CNC est en point d'accès (Wi-Fi MKS_DLC...) : ce nom de Wi-Fi et l'IP 192.168.4.1

const float VIT_XY_RAPIDE = 800;   // mm/min
const float VIT_XY_LENTE  = 150;
const float VIT_Z_RAPIDE  = 300;
const float VIT_Z_LENTE   = 60;
// Sens des joysticks : se règle dans MENU > SENS JOYSTICKS (mémorisé)
const int   ZONE_MORTE = 450;      // zone morte des joysticks (sur ±2048) : augmenter si un axe glisse
const float Z_SECURITE = 5.0;      // remontée avant RETOUR 0 (mm)

// ---- Sonde Z (plaque de touche) ----
const float SONDE_EPAISSEUR = 19.0;  // épaisseur de la plaque de sonde (mesurée : 19 mm)
const float SONDE_COURSE    = 30.0;  // descente maximale pendant la recherche (mm)
const float SONDE_VIT_RAPIDE = 100;  // 1re touche (mm/min)
const float SONDE_VIT_LENTE  = 25;   // 2e touche, précise (mm/min)
const float SONDE_DEGAGEMENT = 5.0;  // remontée après la mesure (mm)

// ---- Imprimante 3D Creality K2 (Klipper / Moonraker) ----
const char* IMP_IP = "192.168.1.128";  // adresse IP de la K2 (modifiable dans le MENU)
const uint16_t IMP_PORT = 7125;        // port Moonraker
const float IMP_VIT_XY_RAPIDE = 3000;  // mm/min
const float IMP_VIT_XY_LENTE  = 600;
const float IMP_VIT_Z_RAPIDE  = 600;
const float IMP_VIT_Z_LENTE   = 120;
const int   IMP_SENS_Z = -1;         // -1 = Z inversé par rapport à la CNC (plateau de la K2) ; mettre 1 pour revenir
const int   CHAUFFE_BUSE    = 210;     // préchauffe PLA (°C)
const int   CHAUFFE_PLATEAU = 60;

const bool UTILISER_POT = true;      // false si le potentiomètre n'est pas câblé
const char* MDP_MAJ = "ttc450";      // mot de passe des mises à jour par Wi-Fi
// #define ECRAN_ST7789              // décommenter si l'écran est un ST7789
// ==========================================================

// Zone tactile à l'écran (déclarée ici, avant toute fonction, pour l'IDE Arduino)
struct BoutonEcran { int x, y, w, h; };

// Pile plus grande pour la tâche principale (requêtes HTTP vers l'imprimante).
// Placée APRÈS la structure ci-dessus : l'IDE Arduino insère ses déclarations avant la 1re fonction.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// ---- Broches ----
#define PIN_JOY_X    1
#define PIN_JOY_Y    2
#define PIN_JOY_Z    4
#define PIN_POT      5
#define PIN_BTN_PAS  6
#define PIN_BTN_VIT  7
#define PIN_LED      8
#define PIN_ZERO_XY 15
#define PIN_ZERO_Z  16
#define PIN_PAUSE   17
#define PIN_ARRET   18
#define PIN_CHARGE  42   // patte 7 (CHRG) du TP4056 : à 0 V pendant la charge
#define PIN_BATT     3   // tension batterie via pont 2 résistances égales (après l interrupteur)

// ============================================================
//  Configuration de l'écran (LovyanGFX)
// ============================================================
class LGFX : public lgfx::LGFX_Device {
#ifdef ECRAN_ST7789
  lgfx::Panel_ST7789  _panel;
#else
  lgfx::Panel_ILI9341 _panel;
#endif
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;
  lgfx::Touch_XPT2046 _touch;
public:
  LGFX() {
    { auto c = _bus.config();
      c.spi_host = SPI2_HOST; c.spi_mode = 0;
      c.freq_write = 40000000; c.freq_read = 16000000;
      c.spi_3wire = false; c.use_lock = true; c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk = 12; c.pin_mosi = 11; c.pin_miso = 13; c.pin_dc = 9;
      _bus.config(c); _panel.setBus(&_bus); }
    { auto c = _panel.config();
      c.pin_cs = 10; c.pin_rst = 14; c.pin_busy = -1;
      c.panel_width = 240; c.panel_height = 320;
      c.offset_x = 0; c.offset_y = 0; c.offset_rotation = 0;
      c.readable = true; c.invert = false; c.rgb_order = false;
      c.dlen_16bit = false; c.bus_shared = true;
      _panel.config(c); }
    { auto c = _light.config();
      c.pin_bl = 21; c.invert = false; c.freq = 44100; c.pwm_channel = 7;
      _light.config(c); _panel.setLight(&_light); }
    { auto c = _touch.config();
      c.x_min = 300; c.x_max = 3900; c.y_min = 300; c.y_max = 3900;
      c.pin_int = 40; c.bus_shared = true; c.offset_rotation = 0;
      c.spi_host = SPI2_HOST; c.freq = 1000000;
      c.pin_sclk = 12; c.pin_mosi = 11; c.pin_miso = 13; c.pin_cs = 39;
      _touch.config(c); _panel.setTouch(&_touch); }
    setPanel(&_panel);
  }
};
LGFX ecran;
Preferences memoire;
WiFiClient cnc;

// ---- Réglages mémorisés (modifiables depuis le MENU) ----
String wifiNom, wifiMdp, cncIp;
uint16_t cncPort = CNC_PORT;
int srcX = 0, sgnX = 1, sgnY = 1, sgnZ = 1;   // orientation des joysticks (MENU > SENS JOYSTICKS)
bool cncAReponduConnexion = false;   // la CNC a-t-elle répondu depuis la connexion ?
float battVolts = 0;                 // tension batterie filtrée
int   battPct = -1;                  // niveau batterie en % (-1 = pas encore mesuré)
// ---- Machine pilotée : CNC (GRBL) ou imprimante 3D (Klipper) ----
enum { M_CNC, M_IMP };
int machine = M_CNC;
String impIp;
bool impOk = false;                  // l'imprimante répond ?
String impEtat = "?";                // standby / printing / paused / complete / cancelled / error
String klippyEtat = "?";             // ready / shutdown / error / startup
String impAxes = "";                 // axes ayant fait leur origine ("xyz")
float tBuse = 0, cBuse = 0, tPlat = 0, cPlat = 0, progression = 0;
int vitImp = 100;                    // vitesse d'impression (%)
enum { E_PRINCIPAL, E_MENU, E_WIFI, E_CLAVIER, E_IP };
int ecranActif = E_PRINCIPAL;

// Couleurs
const uint16_t C_FOND   = TFT_BLACK;
const uint16_t C_BOUTON = 0x2945;   // gris bleuté
const uint16_t C_TEXTE  = TFT_WHITE;
const uint16_t C_AXE[3] = {0xF9A6, 0x47E8, 0x3D9F};  // rouge, vert, bleu clair

// ============================================================
//  Boutons physiques (anti-rebond + appui long)
// ============================================================
struct Bouton {
  uint8_t pin;
  bool etat = false, dernierLu = false, longFait = false;
  unsigned long tChange = 0, tAppui = 0;
  bool appuye = false, appuiLong = false;
  void init(uint8_t p) { pin = p; pinMode(pin, INPUT_PULLUP); }
  void lire() {
    appuye = false; appuiLong = false;
    bool lu = (digitalRead(pin) == LOW);
    if (lu != dernierLu) { dernierLu = lu; tChange = millis(); }
    if (millis() - tChange > 25 && lu != etat) {
      etat = lu;
      if (etat) { appuye = true; tAppui = millis(); longFait = false; }
    }
    if (etat && !longFait && millis() - tAppui > 1000) { appuiLong = true; longFait = true; }
  }
  bool maintenu() { return etat && !longFait; }
};
Bouton bPas, bVit, bZeroXY, bZeroZ, bPause, bArret;

// ============================================================
//  État de la machine
// ============================================================
String etatGrbl = "?";
float pos[3] = {0, 0, 0}, mpos[3] = {0, 0, 0}, wco[3] = {0, 0, 0};
int ovAvance = 100;            // avance réelle (%) lue dans GRBL
int ovLocal  = 100;            // estimation locale entre deux rapports
float vitesseAvance = 0;       // FS: avance réelle (mm/min)
unsigned long tDernierStatut = 0, tDernierEnvoi = 0, tInterro = 0;
int enAttente = 0;
String tampon = "";

const float PAS[] = {0, 10, 1, 0.1};
const char* NOM_PAS[] = {"CONTINU", "10 mm", "1 mm", "0.1 mm"};
int modePas = 0;
bool rapide = true;
bool jogActif = false;
unsigned long tJog = 0, tPasXY = 0, tPasZ = 0;
bool xyAuRepos = true, zAuRepos = true;

String message = "";
unsigned long tMessage = 0;
void afficherMessage(const String& m) { message = m; tMessage = millis(); }

int centre[3] = {2048, 2048, 2048};
const uint8_t PIN_AXE[3] = {PIN_JOY_X, PIN_JOY_Y, PIN_JOY_Z};

// ---- Sonde Z : petite machine à états ----
enum EtapeSonde { SONDE_RIEN, SONDE_RAPIDE, SONDE_LENTE };
EtapeSonde sonde = SONDE_RIEN;
unsigned long tSonde = 0;

// ============================================================
//  Envoi vers la CNC
// ============================================================
void envoyer(const String& cmd) {
  if (!cnc.connected()) return;
  cnc.print(cmd); cnc.print("\n");
  enAttente++; tDernierEnvoi = millis();
}
void envoyerTempsReel(uint8_t c) { if (cnc.connected()) cnc.write(c); }

// ============================================================
//  Réponses GRBL
// ============================================================
void lireNombres(const String& s, const char* cle, float* d, int n) {
  int i = s.indexOf(cle);
  if (i < 0) return;
  if (n == 3) sscanf(s.c_str() + i + strlen(cle), "%f,%f,%f", &d[0], &d[1], &d[2]);
  else        sscanf(s.c_str() + i + strlen(cle), "%f,%f", &d[0], &d[1]);
}

void finSonde(bool ok) {
  if (ok) {
    envoyer("G10 L20 P1 Z" + String(SONDE_EPAISSEUR, 3));     // Z0 = dessus de la pièce
    envoyer("G91 G0 Z" + String(SONDE_DEGAGEMENT, 1));        // on remonte
    envoyer("G90");
    afficherMessage("SONDE OK : Z0 regle");
  } else {
    envoyer("G90");
    afficherMessage("SONDE : pas de contact");
  }
  sonde = SONDE_RIEN;
}

void traiterLigne(String l) {
  l.trim();
  if (l.length() == 0) return;

  if (l.startsWith("<")) {
    int fin = l.indexOf('|');
    String e = l.substring(1, fin > 0 ? fin : l.length() - 1);
    int dp = e.indexOf(':');
    if (dp > 0) e = e.substring(0, dp);
    etatGrbl = e;
    tDernierStatut = millis();
    if (!cncAReponduConnexion && memoire.getUShort("port", 0) != cncPort) memoire.putUShort("port", cncPort);
    cncAReponduConnexion = true;
    if (l.indexOf("WCO:") >= 0) lireNombres(l, "WCO:", wco, 3);
    if (l.indexOf("WPos:") >= 0) lireNombres(l, "WPos:", pos, 3);
    else if (l.indexOf("MPos:") >= 0) {
      lireNombres(l, "MPos:", mpos, 3);
      for (int a = 0; a < 3; a++) pos[a] = mpos[a] - wco[a];
    }
    if (l.indexOf("Ov:") >= 0) {
      int i = l.indexOf("Ov:");
      ovAvance = l.substring(i + 3).toInt();
      ovLocal = ovAvance;
    }
    if (l.indexOf("FS:") >= 0) { float fs[2] = {0, 0}; lireNombres(l, "FS:", fs, 2); vitesseAvance = fs[0]; }
    else if (l.indexOf("|F:") >= 0) { vitesseAvance = l.substring(l.indexOf("|F:") + 3).toFloat(); }
  }
  else if (l == "ok") { if (enAttente > 0) enAttente--; }
  else if (l.startsWith("error")) {
    if (enAttente > 0) enAttente--;
    afficherMessage("ERREUR " + l.substring(6));
    if (sonde != SONDE_RIEN) finSonde(false);
  }
  else if (l.startsWith("ALARM")) {
    afficherMessage("ALARME " + l.substring(6) + " > DEBLOQUER");
    if (sonde != SONDE_RIEN) { sonde = SONDE_RIEN; afficherMessage("SONDE : pas de contact"); }
  }
  else if (l.startsWith("[PRB:")) {                 // résultat du palpage
    bool ok = l.endsWith(":1]");
    if (sonde == SONDE_RAPIDE) {
      if (!ok) { finSonde(false); return; }
      // 1re touche trouvée : on remonte de 2 mm et on palpe lentement
      envoyer("G91 G0 Z2");
      envoyer("G91 G38.2 Z-4 F" + String((int)SONDE_VIT_LENTE));
      sonde = SONDE_LENTE;
      afficherMessage("Sonde : mesure fine...");
    } else if (sonde == SONDE_LENTE) {
      finSonde(ok);
    }
  }
}

void lireCNC() {
  while (cnc.connected() && cnc.available()) {
    char c = cnc.read();
    if (c == '\n') { traiterLigne(tampon); tampon = ""; }
    else if (c != '\r' && tampon.length() < 200) tampon += c;
  }
  if (enAttente > 0 && sonde == SONDE_RIEN && millis() - tDernierEnvoi > 2000) enAttente = 0;
  if (sonde != SONDE_RIEN && millis() - tSonde > 60000) { sonde = SONDE_RIEN; afficherMessage("SONDE : abandon"); }
}

void lancerSonde() {
  if (!cnc.connected() || etatGrbl != "Idle") { afficherMessage("Machine pas prete"); return; }
  envoyer("G91 G38.2 Z-" + String(SONDE_COURSE, 1) + " F" + String((int)SONDE_VIT_RAPIDE));
  sonde = SONDE_RAPIDE;
  tSonde = millis();
  afficherMessage("Sonde : descente...");
}

// ============================================================
//  Wi-Fi / connexion
// ============================================================
unsigned long tTentative = 0;
void gererConnexion() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (cnc.connected() && millis() - tDernierStatut > 3000 && millis() - tTentative > 3000) {
    cnc.stop();
    if (!cncAReponduConnexion) cncPort = (cncPort == 8080) ? 23 : 8080;   // pas de réponse : essayer l'autre port
  }
  if (!cnc.connected() && millis() - tTentative > 2000) {
    tTentative = millis();
    if (cnc.connect(cncIp.c_str(), cncPort, 1500)) {
      cnc.setNoDelay(true);
      cncAReponduConnexion = false;
      enAttente = 0; tDernierStatut = millis(); tampon = ""; etatGrbl = "?";
      afficherMessage("CNC connectee");
    } else {
      cncPort = (cncPort == 8080) ? 23 : 8080;                            // refusé : essayer l'autre port
    }
  }
  if (cnc.connected() && millis() - tInterro > 200) { tInterro = millis(); envoyerTempsReel('?'); }
}

// ============================================================
//  Imprimante 3D (Creality K2 : Klipper + Moonraker en HTTP)
// ============================================================
unsigned long tImpPoll = 0, tImpEnvoi = 0;

String encoderUrl(const String& t) {
  const char* hex = "0123456789ABCDEF";
  String o = "";
  for (unsigned int i = 0; i < t.length(); i++) {
    char ch = t[i];
    if (isalnum((unsigned char)ch) || ch == '-' || ch == '_' || ch == '.') o += ch;
    else { o += '%'; o += hex[(ch >> 4) & 15]; o += hex[ch & 15]; }
  }
  return o;
}

// requête HTTP vers Moonraker ; renvoie le texte de la réponse, code = code HTTP (ou < 0)
// Client réseau et client HTTP déclarés ensemble et une seule fois (globaux) :
// le client HTTP ne doit jamais survivre au client réseau qu'il utilise.
WiFiClient clientImp;
HTTPClient httpImp;

String requeteImp(const String& chemin, bool post, int delaiMs, int& code) {
  code = -1;
  if (WiFi.status() != WL_CONNECTED) return "";
  httpImp.setReuse(false);
  if (!httpImp.begin(clientImp, "http://" + impIp + ":" + String(IMP_PORT) + chemin)) return "";
  httpImp.setConnectTimeout(delaiMs);
  httpImp.setTimeout(delaiMs);
  code = post ? httpImp.POST("") : httpImp.GET();
  String rep = "";
  if (code > 0) {
    int taille = httpImp.getSize();
    if (taille < 0 || taille < 4000) rep = httpImp.getString();   // réponses courtes seulement
  }
  httpImp.end();
  clientImp.stop();
  return rep;
}

// valeur d'une clé dans un texte JSON simple (nombre ou texte)
String valeurJson(const String& s, const String& cle, int depuis) {
  int i = s.indexOf("\"" + cle + "\"", depuis);
  if (i < 0) return "";
  i = s.indexOf(':', i);
  if (i < 0) return "";
  i++;
  while (i < (int)s.length() && s[i] == ' ') i++;
  if (i < (int)s.length() && s[i] == '"') {
    int f = s.indexOf('"', i + 1);
    return f > 0 ? s.substring(i + 1, f) : "";
  }
  int f = i;
  while (f < (int)s.length() && s[f] != ',' && s[f] != '}' && s[f] != ']') f++;
  return s.substring(i, f);
}

void messageErreurImp(const String& rep) {
  String m = valeurJson(rep, "message", 0);
  m.replace("\\n", " ");
  if (m.indexOf("home") >= 0 || m.indexOf("Must home") >= 0) m = "Faire ORIGINE d'abord";
  else if (m.indexOf("out of range") >= 0) m = "Hors limites";
  if (m.length() > 30) m = m.substring(0, 30);
  afficherMessage(m.length() ? m : String("Refuse par l'imprimante"));
}

// envoie du G-code ; délai court pour les commandes longues (G28...) : elles continuent quand même
bool gcodeImp(const String& script, int delaiMs) {
  int code;
  String rep = requeteImp("/printer/gcode/script?script=" + encoderUrl(script), true, delaiMs, code);
  tImpEnvoi = millis();
  if (code >= 400) { messageErreurImp(rep); return false; }
  return true;
}

bool actionImp(const String& chemin) {
  int code;
  String rep = requeteImp(chemin, true, 2000, code);
  if (code >= 400) { messageErreurImp(rep); return false; }
  return code > 0;
}

void interrogerImp() {
  int code;
  String rep = requeteImp("/printer/objects/query?print_stats=state&toolhead=position,homed_axes"
                          "&extruder=temperature,target&heater_bed=temperature,target"
                          "&virtual_sdcard=progress&gcode_move=speed_factor", false, 800, code);
  if (code <= 0) { impOk = false; return; }
  impOk = true;
  if (code != 200) {                               // Klipper arrêté (après ARRÊT ou erreur)
    klippyEtat = (rep.indexOf("shutdown") >= 0) ? "shutdown" : "error";
    return;
  }
  klippyEtat = "ready";
  impEtat = valeurJson(rep, "state", rep.indexOf("\"print_stats\""));
  impAxes = valeurJson(rep, "homed_axes", 0);
  int i = rep.indexOf("\"position\"");
  if (i >= 0) {
    i = rep.indexOf('[', i);
    if (i >= 0) sscanf(rep.c_str() + i + 1, "%f, %f, %f", &pos[0], &pos[1], &pos[2]);
  }
  int e = rep.indexOf("\"extruder\""), b = rep.indexOf("\"heater_bed\"");
  if (e >= 0) { tBuse = valeurJson(rep, "temperature", e).toFloat(); cBuse = valeurJson(rep, "target", e).toFloat(); }
  if (b >= 0) { tPlat = valeurJson(rep, "temperature", b).toFloat(); cPlat = valeurJson(rep, "target", b).toFloat(); }
  progression = valeurJson(rep, "progress", 0).toFloat();
  String sf = valeurJson(rep, "speed_factor", 0);
  if (sf.length()) vitImp = (int)(sf.toFloat() * 100 + 0.5);
}

void gererImprimante() {
  if (WiFi.status() != WL_CONNECTED) { impOk = false; return; }
  unsigned long periode = impOk ? 500 : 3000;
  if (millis() - tImpEnvoi < 400) return;          // on laisse passer les déplacements en cours
  if (millis() - tImpPoll > periode) { tImpPoll = millis(); interrogerImp(); }
}

bool impEnImpression() { return impEtat == "printing" || impEtat == "paused"; }
bool peutBougerImp() {
  if (!impOk || klippyEtat != "ready") return false;
  if (impEnImpression()) { afficherMessage("Impression en cours : bloque"); return false; }
  return true;
}

// ============================================================
//  Joysticks et potentiomètre
// ============================================================
// Recentrage automatique : quand un joystick reste immobile près du centre pendant
// 1,5 s, c'est sa vraie position de repos -> on la mémorise (évite qu'un axe "glisse").
void recentrer(int a, int brut) {
  static int ref[3] = {-1, -1, -1};
  static unsigned long t0[3] = {0, 0, 0};
  if (abs(brut - centre[a]) > ZONE_MORTE * 2) { ref[a] = -1; return; }      // vraiment poussé
  if (ref[a] < 0 || abs(brut - ref[a]) > 40) { ref[a] = brut; t0[a] = millis(); return; }  // ça bouge encore
  if (millis() - t0[a] > 1500) { centre[a] = (centre[a] + brut) / 2; t0[a] = millis(); }
}

float lireAxe(int a) {
  long s = 0;
  for (int i = 0; i < 8; i++) s += analogRead(PIN_AXE[a]);    // moyenne de 8 lectures (moins de bruit)
  int brut = s / 8;
  recentrer(a, brut);
  int v = brut - centre[a];
  if (abs(v) < ZONE_MORTE) return 0;
  float n = (abs(v) - ZONE_MORTE) / (float)(2048 - ZONE_MORTE);
  if (n > 1) n = 1;
  n = n * n;
  return v > 0 ? n : -n;
}

bool peutBouger() {
  return cnc.connected() && sonde == SONDE_RIEN && (etatGrbl == "Idle" || etatGrbl == "Jog");
}
String nb(float v) { return String(v, 3); }

void gererJog() {
  float nx = lireAxe(0), ny = lireAxe(1), nz = lireAxe(2);
  { float bx = nx, by = ny;                       // orientation réglée dans le menu
    nx = sgnX * (srcX == 0 ? bx : by);
    ny = sgnY * (srcX == 0 ? by : bx);
    nz = sgnZ * nz; }
  if (machine == M_IMP) { jogImprimante(nx, ny, nz); return; }
  float vXY = rapide ? VIT_XY_RAPIDE : VIT_XY_LENTE;
  float vZ  = rapide ? VIT_Z_RAPIDE  : VIT_Z_LENTE;

  if (PAS[modePas] == 0) {                       // continu
    bool bouge = (nx != 0 || ny != 0 || nz != 0);
    if (bouge) {
      if (peutBouger() && enAttente < 2 && millis() - tJog > 60) {
        tJog = millis();
        const float dt = 0.15;
        float vx = nx * vXY, vy = ny * vXY, vz = nz * vZ;
        float f = sqrt(vx * vx + vy * vy + vz * vz);
        if (f < 1) return;
        String c = "$J=G91 G21";
        if (nx != 0) c += " X" + nb(vx / 60.0 * dt);
        if (ny != 0) c += " Y" + nb(vy / 60.0 * dt);
        if (nz != 0) c += " Z" + nb(vz / 60.0 * dt);
        c += " F" + String((int)f);
        envoyer(c);
        jogActif = true;
      }
    } else if (jogActif) { envoyerTempsReel(0x85); jogActif = false; }
    return;
  }

  float pas = PAS[modePas];                      // pas à pas
  if (nx == 0 && ny == 0) xyAuRepos = true;
  else {
    bool rep = !xyAuRepos && millis() - tPasXY > 400 && etatGrbl == "Idle";
    if ((xyAuRepos || rep) && peutBouger() && enAttente == 0) {
      String c = "$J=G91 G21 ";
      if (fabs(nx) >= fabs(ny)) c += "X" + nb(nx > 0 ? pas : -pas);
      else                      c += "Y" + nb(ny > 0 ? pas : -pas);
      envoyer(c + " F" + String((int)vXY));
      tPasXY = millis(); xyAuRepos = false;
    }
  }
  if (nz == 0) zAuRepos = true;
  else {
    bool rep = !zAuRepos && millis() - tPasZ > 400 && etatGrbl == "Idle";
    if ((zAuRepos || rep) && peutBouger() && enAttente == 0) {
      envoyer("$J=G91 G21 Z" + nb(nz > 0 ? pas : -pas) + " F" + String((int)vZ));
      tPasZ = millis(); zAuRepos = false;
    }
  }
}

// Déplacements de l'imprimante : petits mouvements relatifs G1 envoyés tant qu'on pousse
void jogImprimante(float nx, float ny, float nz) {
  nz = nz * IMP_SENS_Z;                          // sens du Z propre à l'imprimante
  float vXY = rapide ? IMP_VIT_XY_RAPIDE : IMP_VIT_XY_LENTE;
  float vZ  = rapide ? IMP_VIT_Z_RAPIDE  : IMP_VIT_Z_LENTE;
  if (PAS[modePas] == 0) {                       // continu
    if (nx == 0 && ny == 0 && nz == 0) return;
    if (millis() - tJog < 180 || !peutBougerImp()) return;
    tJog = millis();
    const float dt = 0.2;
    float vx = nx * vXY, vy = ny * vXY, vz = nz * vZ;
    float f = sqrt(vx * vx + vy * vy + vz * vz);
    if (f < 1) return;
    String c = "G91\nG1";
    if (nx != 0) c += " X" + nb(vx / 60.0 * dt);
    if (ny != 0) c += " Y" + nb(vy / 60.0 * dt);
    if (nz != 0) c += " Z" + nb(vz / 60.0 * dt);
    gcodeImp(c + " F" + String((int)f) + "\nG90", 1000);
    return;
  }
  float pas = PAS[modePas];                      // pas à pas
  if (nx == 0 && ny == 0) xyAuRepos = true;
  else if (xyAuRepos || millis() - tPasXY > 500) {
    if (peutBougerImp()) {
      String c = "G91\nG1 ";
      if (fabs(nx) >= fabs(ny)) c += "X" + nb(nx > 0 ? pas : -pas);
      else                      c += "Y" + nb(ny > 0 ? pas : -pas);
      gcodeImp(c + " F" + String((int)vXY) + "\nG90", 1000);
    }
    tPasXY = millis(); xyAuRepos = false;
  }
  if (nz == 0) zAuRepos = true;
  else if (zAuRepos || millis() - tPasZ > 500) {
    if (peutBougerImp()) gcodeImp("G91\nG1 Z" + nb(nz > 0 ? pas : -pas) + " F" + String((int)vZ) + "\nG90", 1000);
    tPasZ = millis(); zAuRepos = false;
  }
}

// Potentiomètre : moitié gauche 10 -> 100 %, moitié droite 100 -> 200 %, zone 100 % au milieu
int consignePot() {
  static int cons = 100;
  static int brutPrec = -1000;
  long s = 0;
  for (int i = 0; i < 8; i++) s += analogRead(PIN_POT);
  int b = s / 8;
  if (abs(b - brutPrec) < 40) return cons;        // hystérésis
  brutPrec = b;
  int c;
  if (b < 1850)      c = map(b, 0, 1850, 10, 100);
  else if (b > 2250) c = map(b, 2250, 4095, 100, 200);
  else               c = 100;
  cons = constrain((c + 2) / 5 * 5, 10, 200);
  return cons;
}

void gererAvance() {
  static unsigned long t = 0;
  if (machine == M_IMP) {                          // imprimante : vitesse d'impression M220
    static int envoye = -1;
    if (!UTILISER_POT || !impOk || klippyEtat != "ready" || millis() - t < 300) return;
    t = millis();
    int c = consignePot();
    if (envoye < 0) { envoye = c; if (c == vitImp) return; }
    if (c != envoye) { if (gcodeImp("M220 S" + String(c), 800)) { envoye = c; vitImp = c; } }
    return;
  }
  if (!UTILISER_POT || !cnc.connected() || millis() - t < 60) return;
  t = millis();
  int diff = consignePot() - ovLocal;
  if (diff == 0) return;
  if (diff >= 10)       { envoyerTempsReel(0x91); ovLocal += 10; }
  else if (diff <= -10) { envoyerTempsReel(0x92); ovLocal -= 10; }
  else if (diff > 0)    { envoyerTempsReel(0x93); ovLocal += 1; }
  else                  { envoyerTempsReel(0x94); ovLocal -= 1; }
}

// ============================================================
//  Écran : dessin
// ============================================================
// Boutons tactiles (colonne de droite) – structure BoutonEcran définie en haut du fichier
const int NB_BE = 6;
BoutonEcran be[NB_BE];
enum { BE_PAS, BE_VIT, BE_ORIGINE, BE_RETOUR, BE_SONDE, BE_DEBLOQUER };

// Fenêtre de confirmation
int confirmation = -1;              // -1 = aucune, sinon numéro du bouton à confirmer
BoutonEcran beOui = {50, 150, 100, 44}, beNon = {170, 150, 100, 44};

bool aRedessiner = true;
String cacheHaut = "", cacheBas = "", cacheVal[3] = {"", "", ""};

String texteBouton(int i) {
  if (machine == M_IMP) switch (i) {
    case BE_PAS:       return String("PAS ") + NOM_PAS[modePas];
    case BE_VIT:       return rapide ? "RAPIDE" : "LENT";
    case BE_ORIGINE:   return "ORIGINE";
    case BE_RETOUR:    return (cBuse > 0 || cPlat > 0) ? "STOP CHAUFFE" : "CHAUFFE PLA";
    case BE_SONDE:     return "ANNULER IMP.";
    default:           return (klippyEtat == "shutdown" || klippyEtat == "error") ? "REDEMARRER" : "MOTEURS OFF";
  }
  switch (i) {
    case BE_PAS:       return String("PAS ") + NOM_PAS[modePas];
    case BE_VIT:       return rapide ? "RAPIDE" : "LENT";
    case BE_ORIGINE:   return "ORIGINE";
    case BE_RETOUR:    return "RETOUR 0";
    case BE_SONDE:     return "SONDE Z";
    default:           return "DEBLOQUER";
  }
}
uint16_t couleurBouton(int i) {
  if (machine == M_IMP) {
    if (i == BE_RETOUR) return 0x8A22;
    if (i == BE_SONDE) return 0x7800;
    if (i == BE_DEBLOQUER) return (klippyEtat == "shutdown" || klippyEtat == "error") ? 0x7800 : C_BOUTON;
    return C_BOUTON;
  }
  if (i == BE_SONDE) return 0x8A22;      // orange foncé
  if (i == BE_DEBLOQUER) return 0x7800;  // rouge foncé
  return C_BOUTON;
}

void dessinerBouton(const BoutonEcran& b, const String& t, uint16_t coul) {
  ecran.fillRoundRect(b.x, b.y, b.w, b.h, 6, coul);
  ecran.setTextColor(C_TEXTE, coul);
  ecran.setTextDatum(textdatum_t::middle_center);
  int nl = t.indexOf('\n');
  if (nl >= 0) {                                   // texte sur 2 lignes
    String l1 = t.substring(0, nl), l2 = t.substring(nl + 1);
    ecran.setFont(&fonts::FreeSansBold9pt7b);
    if (ecran.textWidth(l1) > b.w - 12 || ecran.textWidth(l2) > b.w - 12) ecran.setFont(&fonts::FreeSans9pt7b);
    if (ecran.textWidth(l1) > b.w - 12 || ecran.textWidth(l2) > b.w - 12) ecran.setFont(&fonts::Font2);
    ecran.drawString(l1, b.x + b.w / 2, b.y + b.h / 2 - 8);
    ecran.drawString(l2, b.x + b.w / 2, b.y + b.h / 2 + 9);
    return;
  }
  // police la plus grande qui tient dans le bouton (marge de 6 px de chaque côté)
  ecran.setFont(&fonts::FreeSansBold9pt7b);
  if (ecran.textWidth(t) > b.w - 12) ecran.setFont(&fonts::FreeSans9pt7b);
  if (ecran.textWidth(t) > b.w - 12) ecran.setFont(&fonts::Font2);
  ecran.drawString(t, b.x + b.w / 2, b.y + b.h / 2 + 1);
}

void couleursEtat(String& txt, uint16_t& coul) {
  if (machine == M_IMP) {
    coul = 0x4208; txt = impEtat;
    if (klippyEtat == "shutdown")     { txt = "ARRETEE > REDEMARRER"; coul = 0xB000; }
    else if (klippyEtat == "error")   { txt = "ERREUR IMPRIMANTE";    coul = 0xB000; }
    else if (impEtat == "printing")   { txt = "IMPRESSION " + String((int)(progression * 100)) + "%"; coul = 0x001F; }
    else if (impEtat == "paused")     { txt = "PAUSE";    coul = 0xC300; }
    else if (impEtat == "complete")   { txt = "TERMINEE"; coul = 0x0400; }
    else if (impEtat == "cancelled")  { txt = "ANNULEE";  coul = 0x0400; }
    else if (impEtat == "error")      { txt = "ERREUR";   coul = 0xB000; }
    else                              { txt = "PRETE";    coul = 0x0400; }
    return;
  }
  String e = etatGrbl;
  coul = 0x4208; txt = e;
  if (e == "Idle")       { txt = "PRET";        coul = 0x0400; }
  else if (e == "Run")   { txt = "USINAGE";     coul = 0x001F; }
  else if (e == "Jog")   { txt = "DEPLACEMENT"; coul = 0x0410; }
  else if (e == "Hold")  { txt = "PAUSE";       coul = 0xC300; }
  else if (e == "Alarm") { txt = "ALARME";      coul = 0xB000; }
  else if (e == "Home")  { txt = "ORIGINE...";  coul = 0x8010; }
  else if (e == "Door")  { txt = "PORTE";       coul = 0xC300; }
  if (sonde != SONDE_RIEN) { txt = "SONDE Z..."; coul = 0x8A22; }
}

void dessinerTout() {
  ecran.fillScreen(C_FOND);
  for (int i = 0; i < NB_BE; i++) dessinerBouton(be[i], texteBouton(i), couleurBouton(i));
  ecran.setFont(&fonts::FreeSansBold18pt7b);
  const char* lettres = "XYZ";
  for (int a = 0; a < 3; a++) {
    ecran.setTextColor(C_AXE[a], C_FOND);
    ecran.setTextDatum(textdatum_t::middle_left);
    ecran.drawString(String(lettres[a]), 6, 56 + a * 46);
  }
  cacheHaut = ""; cacheBas = "";
  for (int a = 0; a < 3; a++) cacheVal[a] = "";
  aRedessiner = false;
}

void dessinerConfirmation() {
  ecran.fillRoundRect(20, 50, 280, 160, 10, 0x18E3);
  ecran.drawRoundRect(20, 50, 280, 160, 10, TFT_WHITE);
  ecran.setFont(&fonts::FreeSansBold12pt7b);
  ecran.setTextColor(TFT_WHITE, 0x18E3);
  ecran.setTextDatum(textdatum_t::middle_center);
  ecran.drawString(texteBouton(confirmation) + " ?", 160, 80);
  ecran.setFont(&fonts::FreeSans9pt7b);
  if (machine == M_IMP) {
    if (confirmation == BE_ORIGINE)     ecran.drawString("Origine des 3 axes (G28)", 160, 115);
    else if (confirmation == BE_RETOUR) ecran.drawString("Buse " + String(CHAUFFE_BUSE) + " C, plateau " + String(CHAUFFE_PLATEAU) + " C", 160, 115);
    else                                ecran.drawString("L'impression sera perdue", 160, 115);
  } else if (confirmation == BE_SONDE) {
    ecran.drawString("Plaque sous la fraise ?", 160, 108);
    ecran.drawString("Pince sur la fraise ?", 160, 128);
  } else if (confirmation == BE_ORIGINE) {
    ecran.drawString("Prise d'origine machine", 160, 115);
  } else {
    ecran.drawString("Remonte Z puis va en X0 Y0", 160, 115);
  }
  dessinerBouton(beOui, "OUI", 0x0400);
  dessinerBouton(beNon, "NON", 0xB000);
}

void majEcran() {
  static unsigned long t = 0;
  if (millis() - t < 120) return;
  t = millis();
  if (confirmation >= 0) return;        // la fenêtre de confirmation reste affichée
  if (aRedessiner) dessinerTout();

  // Barre d'état en haut
  String txt; uint16_t coul;
  if (WiFi.status() != WL_CONNECTED)  { txt = "Wi-Fi " + wifiNom + "..."; coul = 0x4208; }
  else if (machine == M_IMP && !impOk) { txt = "Recherche imprim. " + impIp; coul = 0x4208; }
  else if (machine == M_CNC && !cnc.connected()) { txt = "Recherche CNC " + cncIp; coul = 0x4208; }
  else couleursEtat(txt, coul);
  bool msg = message.length() && millis() - tMessage < 2500;
  lireBatterie();
  String haut = (msg ? message : txt) + "|" + String(coul) + "|" + String(battPct) + String(enCharge());
  if (machine == M_IMP) {                         // libellés qui dépendent de l'état de l'imprimante
    static String etiquettes = "";
    String e = texteBouton(BE_RETOUR) + texteBouton(BE_DEBLOQUER);
    if (e != etiquettes) { etiquettes = e; rafraichirBouton(BE_RETOUR); rafraichirBouton(BE_DEBLOQUER); }
  }
  if (haut != cacheHaut) {
    cacheHaut = haut;
    ecran.fillRect(0, 0, 320, 26, msg ? 0x2124 : coul);
    ecran.setFont(&fonts::FreeSansBold9pt7b);
    ecran.setTextColor(TFT_WHITE);
    ecran.setTextDatum(textdatum_t::middle_left);
    ecran.setClipRect(0, 0, 214, 26);              // le texte ne déborde pas sur la batterie
    ecran.drawString(msg ? message : txt, 6, 13);
    ecran.clearClipRect();
    ecran.setTextDatum(textdatum_t::middle_right);
    ecran.drawString("MENU", 314, 13);
    dessinerBatterie(218, 5, msg ? 0x2124 : coul);
  }

  // Cotes X Y Z
  ecran.setFont(&fonts::FreeSansBold18pt7b);
  ecran.setTextDatum(textdatum_t::middle_right);
  for (int a = 0; a < 3; a++) {
    char s[12];
    snprintf(s, sizeof(s), "%.2f", pos[a]);
    if (cacheVal[a] != s) {
      cacheVal[a] = s;
      ecran.setTextColor(TFT_WHITE, C_FOND);
      ecran.setTextPadding(156);
      ecran.drawString(s, 192, 56 + a * 46);
    }
  }
  ecran.setTextPadding(0);

  // Ligne du bas : avance
  String bas1, bas2;
  if (machine == M_IMP) {
    bas1 = "Buse " + String((int)tBuse) + (cBuse > 0 ? "/" + String((int)cBuse) : String("")) + "  Plat. "
         + String((int)tPlat) + (cPlat > 0 ? "/" + String((int)cPlat) : String(""));
    bas2 = "Vitesse " + String(vitImp) + "%";
    if (impEnImpression()) bas2 = String((int)(progression * 100)) + "%   " + bas2;
    if (impAxes.length() < 3 && !impEnImpression()) bas2 += "  (pas d'origine)";
  } else {
    int cons = UTILISER_POT ? consignePot() : ovAvance;
    bas1 = "Avance " + String(ovAvance) + "%";                     // 1re ligne
    if (UTILISER_POT && cons != ovAvance) bas1 += " > " + String(cons) + "%";
    bas2 = (etatGrbl == "Run") ? String((int)vitesseAvance) + " mm/min" : "";   // 2e ligne
  }
  if (bas1 + bas2 != cacheBas) {
    cacheBas = bas1 + bas2;
    ecran.fillRect(0, 194, 196, 46, C_FOND);
    ecran.setFont(&fonts::FreeSans9pt7b);
    ecran.setTextColor(0xBDF7, C_FOND);
    ecran.setTextDatum(textdatum_t::middle_left);
    if (ecran.textWidth(bas1) > 188 || ecran.textWidth(bas2) > 188) ecran.setFont(&fonts::Font2);
    ecran.drawString(bas1, 6, 206);
    ecran.drawString(bas2, 6, 228);
  }
}

void rafraichirBouton(int i) { dessinerBouton(be[i], texteBouton(i), couleurBouton(i)); }

// ============================================================
//  Tactile
// ============================================================
bool dedans(const BoutonEcran& b, int x, int y) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

void executer(int i) {
  if (machine == M_IMP) {
    if (i == BE_ORIGINE) {
      if (impEnImpression()) { afficherMessage("Impression en cours"); return; }
      gcodeImp("G28", 400); afficherMessage("Origine en cours...");
    } else if (i == BE_RETOUR) {
      gcodeImp("M140 S" + String(CHAUFFE_PLATEAU) + "\nM104 S" + String(CHAUFFE_BUSE), 1500);
      afficherMessage("Prechauffe PLA");
    } else if (i == BE_SONDE) {
      if (actionImp("/printer/print/cancel")) afficherMessage("Impression annulee");
    }
    return;
  }
  switch (i) {
    case BE_ORIGINE:
      if (cnc.connected()) { envoyer("$H"); afficherMessage("Prise d'origine"); }
      break;
    case BE_RETOUR:
      if (peutBouger()) {
        envoyer("G90 G0 Z" + String(Z_SECURITE, 1));
        envoyer("G0 X0 Y0");
        afficherMessage("Retour X0 Y0");
      }
      break;
    case BE_SONDE:
      lancerSonde();
      break;
  }
}

void gererTactile() {
  static bool touchePrec = false;
  int32_t x, y;
  bool touche = ecran.getTouch(&x, &y);
  if (touche && !touchePrec) {
    if (ecranActif != E_PRINCIPAL) { toucheMenus(x, y); touchePrec = touche; return; }
    if (confirmation < 0 && y < 26) {                 // barre du haut -> MENU
      if (jogActif) { envoyerTempsReel(0x85); jogActif = false; }
      ouvrirMenu();
      touchePrec = touche; return;
    }
    if (confirmation >= 0) {
      if (dedans(beOui, x, y))      { int c = confirmation; confirmation = -1; aRedessiner = true; executer(c); }
      else if (dedans(beNon, x, y)) { confirmation = -1; aRedessiner = true; }
    } else {
      for (int i = 0; i < NB_BE; i++) if (dedans(be[i], x, y)) {
        if (i == BE_PAS)            { modePas = (modePas + 1) % 4; rafraichirBouton(i); }
        else if (i == BE_VIT)       { rapide = !rapide; rafraichirBouton(i); }
        else if (machine == M_IMP && i == BE_DEBLOQUER) {
          if (klippyEtat == "shutdown" || klippyEtat == "error") { actionImp("/printer/firmware_restart"); afficherMessage("Redemarrage..."); }
          else if (impEnImpression()) afficherMessage("Impression en cours");
          else { gcodeImp("M84", 1500); afficherMessage("Moteurs coupes"); }
        }
        else if (machine == M_IMP && i == BE_RETOUR && (cBuse > 0 || cPlat > 0)) {
          gcodeImp("M104 S0\nM140 S0", 1500); afficherMessage("Chauffe arretee");
        }
        else if (machine == M_IMP && i == BE_SONDE && !impEnImpression()) afficherMessage("Pas d'impression en cours");
        else if (i == BE_DEBLOQUER) { envoyer("$X"); afficherMessage("Deblocage"); }
        else                        { confirmation = i; dessinerConfirmation(); }
      }
    }
  }
  touchePrec = touche;
}


// ============================================================
//  MENU RÉGLAGES (toucher la barre du haut de l'écran)
// ============================================================
const BoutonEcran bmRetour  = {4, 4, 100, 30};
const BoutonEcran bmMachine = {110, 4, 206, 30};
const BoutonEcran bmWifi    = {4, 150, 102, 40};
const BoutonEcran bmIp      = {109, 150, 102, 40};
const BoutonEcran bmCherche = {214, 150, 102, 40};
const BoutonEcran bmCalib   = {4, 196, 102, 40};
const BoutonEcran bmJoy     = {109, 196, 102, 40};
const BoutonEcran bmTest    = {214, 196, 102, 40};
const uint16_t C_TITRE = 0x18E3;
String cacheMenu = "";

void titreEcran(const String& t) {
  ecran.fillScreen(C_FOND);
  ecran.fillRect(0, 0, 320, 38, C_TITRE);
  ecran.setFont(&fonts::FreeSansBold9pt7b);
  ecran.setTextColor(TFT_WHITE, C_TITRE);
  ecran.setTextDatum(textdatum_t::middle_center);
  ecran.drawString(t, 212, 19);
  dessinerBouton(bmRetour, "< RETOUR", C_BOUTON);
}

void texteInfo(const String& t, int y, uint16_t coul) {
  ecran.setFont(&fonts::FreeSans9pt7b);
  ecran.setTextColor(coul, C_FOND);
  ecran.setTextDatum(textdatum_t::middle_left);
  ecran.fillRect(0, y - 11, 320, 22, C_FOND);
  ecran.drawString(t, 10, y);
}

void ouvrirMenu() {
  ecranActif = E_MENU;
  titreEcran("");
  dessinerBouton(bmMachine, machine == M_IMP ? "MACHINE : IMPRIMANTE 3D" : "MACHINE : CNC", machine == M_IMP ? 0x001F : 0x0400);
  dessinerBouton(bmWifi, "CHOISIR\nWI-FI", 0x0410);
  dessinerBouton(bmIp, machine == M_IMP ? "IP DE\nL'IMPRIM." : "IP DE\nLA CNC", C_BOUTON);
  dessinerBouton(bmCherche, machine == M_IMP ? "CHERCHER\nL'IMPRIM." : "CHERCHER\nLA CNC", 0x8A22);
  dessinerBouton(bmCalib, "CALIBRER\nECRAN", C_BOUTON);
  dessinerBouton(bmJoy, "SENS\nJOYSTICKS", 0x0410);
  dessinerBouton(bmTest, "TEST\nCABLAGE", 0x8A22);
  cacheMenu = "";
  majMenu();
}

void majMenu() {            // infos rafraîchies pendant que le menu est ouvert
  static unsigned long t = 0;
  if (millis() - t < 500 && cacheMenu.length()) return;
  t = millis();
  bool w = WiFi.status() == WL_CONNECTED;
  lireBatterie();
  String l1 = "Wi-Fi : " + wifiNom + (w ? "  (connecte)" : "  (non connecte)");
  String l2 = "IP telecommande : " + (w ? WiFi.localIP().toString() : String("-"));
  bool okM = machine == M_IMP ? impOk : (cncAReponduConnexion && cnc.connected());
  String l3 = machine == M_IMP ? "IP imprimante : " + impIp + ":" + String(IMP_PORT)
                               : "IP CNC : " + cncIp + ":" + String(cncPort);
  l3 += okM ? "  (OK)" : "  (introuvable)";
  String l4 = (w ? "Wi-Fi " + String(WiFi.RSSI()) + " dBm" : String("Wi-Fi -"))
             + "  |  Batt. " + String(battVolts, 2) + "V " + (enCharge() ? String("(charge)") : String(battPct) + "%");
  String tout = l1 + l2 + l3 + l4;
  if (tout == cacheMenu) return;
  cacheMenu = tout;
  texteInfo(l1, 56, w ? TFT_WHITE : TFT_ORANGE);
  texteInfo(l2, 80, TFT_WHITE);
  texteInfo(l3, 104, okM ? TFT_GREENYELLOW : TFT_ORANGE);
  texteInfo(l4, 128, 0xBDF7);
}

void fermerMenu() {
  if (WiFi.status() != WL_CONNECTED) WiFi.begin(wifiNom.c_str(), wifiMdp.c_str());
  ecranActif = E_PRINCIPAL;
  aRedessiner = true;
}

// ---------- Liste des réseaux Wi-Fi ----------
const int MAX_RESEAUX = 20;
String reseaux[MAX_RESEAUX];
int rssiReseau[MAX_RESEAUX];
bool reseauProtege[MAX_RESEAUX];
int nbReseaux = 0, pageWifi = 0;
String reseauChoisi = "";
const BoutonEcran bwPrec = {222, 4, 46, 30}, bwSuiv = {270, 4, 46, 30};
const int LIGNES_WIFI = 5;

void scannerWifi() {
  ecranActif = E_WIFI;
  titreEcran("RESEAUX WI-FI");
  texteInfo("Recherche des reseaux...", 120, TFT_WHITE);
  if (WiFi.status() != WL_CONNECTED) WiFi.disconnect();   // libère la radio pour la recherche
  int n = WiFi.scanNetworks();
  nbReseaux = 0;
  for (int i = 0; i < n && nbReseaux < MAX_RESEAUX; i++) {
    String nom = WiFi.SSID(i);
    if (nom.length() == 0) continue;
    bool deja = false;
    for (int j = 0; j < nbReseaux; j++) if (reseaux[j] == nom) deja = true;
    if (deja) continue;
    reseaux[nbReseaux] = nom;
    rssiReseau[nbReseaux] = WiFi.RSSI(i);
    reseauProtege[nbReseaux] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    nbReseaux++;
  }
  WiFi.scanDelete();
  pageWifi = 0;
  dessinerListeWifi();
}

BoutonEcran ligneWifi(int k) { BoutonEcran b = {4, 42 + k * 39, 312, 35}; return b; }

void dessinerListeWifi() {
  titreEcran("WI-FI");
  int pages = (nbReseaux + LIGNES_WIFI - 1) / LIGNES_WIFI;
  if (pages > 1) { dessinerBouton(bwPrec, "<", C_BOUTON); dessinerBouton(bwSuiv, ">", C_BOUTON); }
  if (nbReseaux == 0) { texteInfo("Aucun reseau trouve. RETOUR puis", 110, TFT_ORANGE); texteInfo("CHOISIR WI-FI pour relancer.", 134, TFT_ORANGE); return; }
  for (int k = 0; k < LIGNES_WIFI; k++) {
    int i = pageWifi * LIGNES_WIFI + k;
    if (i >= nbReseaux) break;
    BoutonEcran b = ligneWifi(k);
    uint16_t fond = (reseaux[i] == wifiNom) ? 0x0410 : C_BOUTON;
    ecran.fillRoundRect(b.x, b.y, b.w, b.h, 6, fond);
    ecran.setFont(&fonts::FreeSans9pt7b);
    ecran.setTextColor(TFT_WHITE, fond);
    ecran.setTextDatum(textdatum_t::middle_left);
    String nom = reseaux[i];
    if (nom.length() > 24) nom = nom.substring(0, 23) + ".";
    ecran.drawString(nom, b.x + 10, b.y + b.h / 2 + 1);
    // force du signal (4 barres) + cadenas
    int r = rssiReseau[i];
    int barres = r > -55 ? 4 : r > -65 ? 3 : r > -75 ? 2 : 1;
    for (int j = 0; j < 4; j++) {
      int hb = 6 + j * 5;
      ecran.fillRect(b.x + b.w - 52 + j * 7, b.y + b.h - 6 - hb, 5, hb, j < barres ? TFT_WHITE : 0x4208);
    }
    if (reseauProtege[i]) {
      int cx = b.x + b.w - 14, cy = b.y + b.h / 2 + 3;
      ecran.drawRoundRect(cx - 4, cy - 12, 8, 10, 3, TFT_WHITE);
      ecran.fillRect(cx - 6, cy - 5, 12, 9, TFT_WHITE);
    }
  }
}

// ---------- Clavier AZERTY à l'écran ----------
String saisie = "";
int modeClavier = 0;                       // 0 minuscules, 1 majuscules, 2 chiffres et symboles
const char* RANGEES[3][3] = {
  {"azertyuiop", "qsdfghjklm", "wxcvbn"},
  {"AZERTYUIOP", "QSDFGHJKLM", "WXCVBN"},
  {"1234567890", "-_.@#&*!?/", "+=$%()"}};
const BoutonEcran bkMaj = {0, 162, 48, 38}, bkEff = {240, 162, 80, 38};
const BoutonEcran bkMode = {0, 202, 64, 38}, bkEsp = {64, 202, 192, 38}, bkOk = {256, 202, 64, 38};
const BoutonEcran bkAnnuler = {4, 4, 100, 30};

BoutonEcran toucheClavier(int rangee, int k) {
  BoutonEcran b;
  if (rangee < 2) { b = {k * 32, 82 + rangee * 40, 32, 38}; }
  else            { b = {48 + k * 32, 162, 32, 38}; }
  return b;
}

void dessinerSaisie() {
  ecran.fillRect(0, 40, 320, 40, C_FOND);
  ecran.drawRoundRect(4, 42, 312, 36, 6, TFT_WHITE);
  ecran.setFont(&fonts::FreeSans9pt7b);
  ecran.setTextColor(TFT_WHITE, C_FOND);
  ecran.setTextDatum(textdatum_t::middle_left);
  String vue = saisie;
  while (vue.length() && ecran.textWidth(vue + "_") > 296) vue = vue.substring(1);   // on montre la fin
  ecran.drawString(vue + "_", 12, 61);
}

void dessinerClavier() {
  ecran.fillScreen(C_FOND);
  ecran.fillRect(0, 0, 320, 38, C_TITRE);
  dessinerBouton(bkAnnuler, "ANNULER", 0x7800);
  ecran.setFont(&fonts::FreeSans9pt7b);
  ecran.setTextColor(TFT_WHITE, C_TITRE);
  ecran.setTextDatum(textdatum_t::middle_center);
  String t = "Mot de passe : " + reseauChoisi;
  if (t.length() > 26) t = t.substring(0, 25) + ".";
  ecran.drawString(t, 212, 19);
  dessinerSaisie();
  for (int r = 0; r < 3; r++) {
    const char* lettres = RANGEES[modeClavier][r];
    for (int k = 0; lettres[k]; k++) {
      BoutonEcran b = toucheClavier(r, k);
      BoutonEcran bi = {b.x + 1, b.y + 1, b.w - 2, b.h - 2};
      dessinerBouton(bi, String(lettres[k]), C_BOUTON);
    }
  }
  dessinerBouton(bkMaj, modeClavier == 1 ? "maj" : "MAJ", modeClavier == 1 ? 0x0410 : 0x4208);
  dessinerBouton(bkEff, "EFFACER", 0x7800);
  dessinerBouton(bkMode, modeClavier == 2 ? "abc" : "123", 0x4208);
  dessinerBouton(bkEsp, "espace", 0x4208);
  dessinerBouton(bkOk, "OK", 0x0400);
}

void ouvrirClavier(const String& depart) {
  ecranActif = E_CLAVIER;
  saisie = depart;
  modeClavier = 0;
  dessinerClavier();
}

void connecterWifi(const String& nom, const String& mdp) {
  wifiNom = nom; wifiMdp = mdp;
  memoire.putString("ssid", wifiNom);
  memoire.putString("mdp", wifiMdp);
  cnc.stop();
  titreEcran("CONNEXION");
  texteInfo("Connexion a " + wifiNom + "...", 100, TFT_WHITE);
  WiFi.disconnect();
  delay(200);
  WiFi.begin(wifiNom.c_str(), wifiMdp.c_str());
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) delay(200);
  if (WiFi.status() == WL_CONNECTED) texteInfo("Connecte : " + WiFi.localIP().toString(), 130, TFT_GREENYELLOW);
  else texteInfo("Echec : verifier le mot de passe", 130, TFT_ORANGE);
  delay(1800);
  ouvrirMenu();
}

// ---------- Pavé numérique pour l'IP de la CNC ----------
const char* PAVE[3][4] = {{"1", "2", "3", "<"}, {"4", "5", "6", "."}, {"7", "8", "9", "0"}};
const BoutonEcran bpEff = {2, 206, 156, 32}, bpOk = {162, 206, 156, 32};

BoutonEcran touchePave(int r, int c) { BoutonEcran b = {2 + c * 80, 84 + r * 40, 76, 36}; return b; }

void dessinerPave() {
  ecran.fillScreen(C_FOND);
  ecran.fillRect(0, 0, 320, 38, C_TITRE);
  dessinerBouton(bkAnnuler, "ANNULER", 0x7800);
  ecran.setFont(&fonts::FreeSansBold9pt7b);
  ecran.setTextColor(TFT_WHITE, C_TITRE);
  ecran.setTextDatum(textdatum_t::middle_center);
  ecran.drawString(machine == M_IMP ? "IP DE L'IMPRIMANTE" : "IP DE LA CNC", 212, 19);
  dessinerSaisie();
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 4; c++)
      dessinerBouton(touchePave(r, c), PAVE[r][c], PAVE[r][c][0] == '<' ? 0x7800 : C_BOUTON);
  dessinerBouton(bpEff, "TOUT EFFACER", 0x4208);
  dessinerBouton(bpOk, "OK", 0x0400);
}

// ---------- Recherche automatique de la CNC sur le réseau ----------
// 1) on ouvre 8 connexions à la fois (4 adresses x ports 8080 et 23) pour repérer
//    rapidement les appareils dont le port répond ;
// 2) on vérifie ensuite chaque candidat en prenant le temps (connexion 2 s,
//    question "?" à GRBL, réponse attendue jusqu'à 2,5 s), comme une connexion manuelle.
const int MAX_CANDIDATS = 12;
uint8_t candHote[MAX_CANDIDATS];
uint16_t candPort[MAX_CANDIDATS];
int nbCandidats = 0;

uint16_t portsScan[2] = {8080, 23};
int nbPortsScan = 2;

void balayerLot(IPAddress moi, int h0, int nbHotes, int delaiMs) {
  const uint16_t* ports = portsScan;
  int fd[8], hote[8];
  uint16_t port[8];
  int n = 0;
  for (int i = 0; i < nbHotes; i++) {
    int h = h0 + i;
    if (h < 1 || h > 254 || h == moi[3]) continue;
    for (int k = 0; k < nbPortsScan; k++) {
      int f = socket(AF_INET, SOCK_STREAM, 0);
      if (f < 0) continue;
      fcntl(f, F_SETFL, fcntl(f, F_GETFL, 0) | O_NONBLOCK);
      struct sockaddr_in adr;
      memset(&adr, 0, sizeof(adr));
      adr.sin_family = AF_INET;
      adr.sin_port = htons(ports[k]);
      adr.sin_addr.s_addr = (uint32_t)moi[0] | ((uint32_t)moi[1] << 8) | ((uint32_t)moi[2] << 16) | ((uint32_t)h << 24);
      connect(f, (struct sockaddr*)&adr, sizeof(adr));
      fd[n] = f; hote[n] = h; port[n] = ports[k]; n++;
    }
  }
  unsigned long t0 = millis();
  int restants = n;
  while (restants > 0 && millis() - t0 < (unsigned long)delaiMs) {
    fd_set ecr; FD_ZERO(&ecr);
    int maxfd = -1;
    for (int i = 0; i < n; i++) if (fd[i] >= 0) { FD_SET(fd[i], &ecr); if (fd[i] > maxfd) maxfd = fd[i]; }
    struct timeval tv = {0, 50000};
    if (select(maxfd + 1, NULL, &ecr, NULL, &tv) > 0) {
      for (int i = 0; i < n; i++) {
        if (fd[i] < 0 || !FD_ISSET(fd[i], &ecr)) continue;
        int err = 0; socklen_t l = sizeof(err);
        getsockopt(fd[i], SOL_SOCKET, SO_ERROR, &err, &l);
        if (err == 0 && nbCandidats < MAX_CANDIDATS) { candHote[nbCandidats] = hote[i]; candPort[nbCandidats] = port[i]; nbCandidats++; }
        close(fd[i]); fd[i] = -1; restants--;
      }
    }
  }
  for (int i = 0; i < n; i++) if (fd[i] >= 0) close(fd[i]);
}

bool estUneCnc(IPAddress ip, uint16_t port) {
  WiFiClient c;
  if (!c.connect(ip, port, 2000)) return false;
  c.setNoDelay(true);
  String rep = "";
  unsigned long t = millis();
  bool ok = false;
  for (int essai = 0; essai < 3 && !ok; essai++) {
    c.write('?');                                   // demande d'état GRBL
    unsigned long t1 = millis();
    while (millis() - t1 < 800) {
      while (c.available()) rep += (char)c.read();
      if (rep.indexOf('<') >= 0 || rep.indexOf("Grbl") >= 0 || rep.indexOf("[MSG") >= 0) { ok = true; break; }
      delay(10);
    }
  }
  c.stop();
  delay(300);                                       // laisse la carte libérer la connexion
  return ok;
}

bool estUneImprimante(IPAddress ip) {
  String ancien = impIp;
  impIp = ip.toString();
  int code;
  String rep = requeteImp("/server/info", false, 2000, code);
  impIp = ancien;
  return code == 200 && rep.indexOf("klippy") >= 0;
}

void chercherImprimante() {
  if (WiFi.status() != WL_CONNECTED) { texteInfo("Pas de Wi-Fi : choisir le Wi-Fi d'abord", 128, TFT_ORANGE); return; }
  IPAddress moi = WiFi.localIP();
  titreEcran("RECHERCHE IMPRIMANTE");
  texteInfo("Toucher l'ecran pour arreter", 210, 0xBDF7);
  portsScan[0] = IMP_PORT; nbPortsScan = 1;
  nbCandidats = 0;
  bool arret = false;
  for (int h = 1; h < 255 && !arret; h += 8) {
    texteInfo("Balayage : " + String(moi[0]) + "." + String(moi[1]) + "." + String(moi[2]) + "." + String(h) + " ...", 80, TFT_WHITE);
    ecran.fillRect(10, 110, 300 * h / 254, 14, 0x8A22);
    ecran.drawRect(10, 110, 300, 14, TFT_WHITE);
    balayerLot(moi, h, 8, 900);
    texteInfo("Appareils a verifier : " + String(nbCandidats), 145, 0xBDF7);
    int32_t tx, ty;
    if (ecran.getTouch(&tx, &ty)) arret = true;
  }
  portsScan[0] = 8080; portsScan[1] = 23; nbPortsScan = 2;
  bool trouve = false;
  for (int i = 0; i < nbCandidats && !trouve; i++) {
    IPAddress ip(moi[0], moi[1], moi[2], candHote[i]);
    texteInfo("Verification : " + ip.toString(), 80, TFT_WHITE);
    if (estUneImprimante(ip)) { impIp = ip.toString(); memoire.putString("ipImp", impIp); trouve = true; }
  }
  if (trouve) texteInfo("Imprimante trouvee : " + impIp, 175, TFT_GREENYELLOW);
  else texteInfo("Aucune imprimante Klipper (port 7125)", 175, TFT_ORANGE);
  delay(2500);
  impOk = false; tImpPoll = 0;
  ouvrirMenu();
}

void chercherCnc() {
  if (machine == M_IMP) { chercherImprimante(); return; }
  if (WiFi.status() != WL_CONNECTED) { texteInfo("Pas de Wi-Fi : choisir le Wi-Fi d'abord", 128, TFT_ORANGE); return; }
  cnc.stop();
  jogActif = false;
  delay(300);
  IPAddress moi = WiFi.localIP();
  titreEcran("RECHERCHE CNC");
  texteInfo("Toucher l'ecran pour arreter", 210, 0xBDF7);
  nbCandidats = 0;
  bool arret = false;
  // Étape 1 : balayage rapide du réseau
  for (int h = 1; h < 255 && !arret; h += 4) {
    texteInfo("Balayage : " + String(moi[0]) + "." + String(moi[1]) + "." + String(moi[2]) + "." + String(h) + " ...", 80, TFT_WHITE);
    ecran.fillRect(10, 110, 300 * h / 254, 14, 0x8A22);
    ecran.drawRect(10, 110, 300, 14, TFT_WHITE);
    balayerLot(moi, h, 4, 900);
    texteInfo("Appareils a verifier : " + String(nbCandidats), 145, 0xBDF7);
    int32_t tx, ty;
    if (ecran.getTouch(&tx, &ty)) arret = true;
  }
  // Étape 2 : vérification de chaque candidat (le port 8080 d'abord)
  bool trouve = false;
  for (int passe = 0; passe < 2 && !trouve; passe++) {
    uint16_t p = passe == 0 ? 8080 : 23;
    for (int i = 0; i < nbCandidats && !trouve; i++) {
      if (candPort[i] != p) continue;
      IPAddress ip(moi[0], moi[1], moi[2], candHote[i]);
      texteInfo("Verification : " + ip.toString() + ":" + String(p), 80, TFT_WHITE);
      if (estUneCnc(ip, p)) {
        cncIp = ip.toString();
        cncPort = p;
        memoire.putString("ip", cncIp);
        memoire.putUShort("port", cncPort);
        trouve = true;
      }
    }
  }
  if (trouve) texteInfo("CNC trouvee : " + cncIp + ":" + String(cncPort), 175, TFT_GREENYELLOW);
  else if (nbCandidats == 0) texteInfo("Aucun appareil ne repond (8080 / 23)", 175, TFT_ORANGE);
  else texteInfo("Pas de reponse GRBL (logiciel PC connecte ?)", 175, TFT_ORANGE);
  delay(2500);
  cncAReponduConnexion = false;
  tTentative = 0;                                     // reconnexion immédiate
  ouvrirMenu();
}


// ---------- Réglage du sens des joysticks (assistant) ----------
// attend que le joystick soit poussé franchement ; renvoie l'axe brut (0 = VRx gauche,
// 1 = VRy gauche, 2 = joystick Z) et le signe ; -1 si rien au bout de 15 s
int attendrePoussee(int a0, int a1, int& signe) {
  unsigned long t = millis();
  while (millis() - t < 20000) {
    for (int a = a0; a <= a1; a++) {
      // valeur brute affichée en direct (barre + chiffre) pour voir ce que reçoit la broche
      int brut = analogRead(PIN_AXE[a]);
      int y = 150 + (a - a0) * 24;
      ecran.setFont(&fonts::FreeSans9pt7b);
      ecran.setTextColor(TFT_WHITE, C_FOND);
      ecran.setTextDatum(textdatum_t::middle_left);
      ecran.drawString(a == 2 ? "GPIO4" : (a == 0 ? "GPIO1" : "GPIO2"), 6, y);
      ecran.fillRect(70, y - 7, 180, 14, 0x2104);
      ecran.fillRect(70, y - 7, 180 * brut / 4095, 14, 0x0410);
      ecran.drawFastVLine(70 + 180 * centre[a] / 4095, y - 9, 18, TFT_ORANGE);   // repère du centre
      ecran.setTextDatum(textdatum_t::middle_right);
      ecran.setTextPadding(50);
      ecran.drawString(String(brut), 316, y);
      ecran.setTextPadding(0);
      if (abs(brut - centre[a]) > 900) { signe = brut > centre[a] ? 1 : -1; return a; }
    }
    int32_t tx, ty;
    if (ecran.getTouch(&tx, &ty)) return -1;       // toucher l'écran = annuler
    delay(30);
  }
  return -1;
}
void attendreRelache() {
  unsigned long t = millis();
  while (millis() - t < 5000 && (fabs(lireAxe(0)) > 0.05 || fabs(lireAxe(1)) > 0.05 || fabs(lireAxe(2)) > 0.05)) delay(20);
  delay(300);
}
void consigneJoy(const String& l1, const String& l2) {
  titreEcran("SENS JOYSTICKS");
  texteInfo(l1, 66, TFT_WHITE);
  texteInfo(l2, 92, TFT_GREENYELLOW);
  texteInfo("(toucher l'ecran pour annuler)", 222, 0xBDF7);
}
void reglerSensJoysticks() {
  int signe = 1;
  { int32_t tx, ty; unsigned long t = millis();            // attendre qu'on lâche l'écran
    while (ecran.getTouch(&tx, &ty) && millis() - t < 3000) delay(20); delay(200); }
  // recentrage : on remesure le repos des 3 axes (utile après un changement de fil)
  titreEcran("SENS JOYSTICKS");
  texteInfo("Ne pas toucher les joysticks...", 110, TFT_WHITE);
  delay(800);
  for (int a = 0; a < 3; a++) {
    long somme = 0;
    for (int i = 0; i < 64; i++) { somme += analogRead(PIN_AXE[a]); delay(3); }
    centre[a] = somme / 64;
  }
  consigneJoy("Joystick GAUCHE : pousser vers", "la DROITE et maintenir");
  int a = attendrePoussee(0, 1, signe);
  if (a < 0) return;
  srcX = a; sgnX = signe;
  texteInfo("OK, relacher", 200, TFT_GREENYELLOW); attendreRelache();

  consigneJoy("Joystick GAUCHE : pousser vers", "le HAUT (loin de toi) et maintenir");
  a = attendrePoussee(0, 1, signe);
  if (a < 0) return;
  if (a == srcX) { texteInfo("Meme axe que la droite : recommencer", 200, TFT_ORANGE); delay(2500); return; }
  sgnY = signe;
  texteInfo("OK, relacher", 200, TFT_GREENYELLOW); attendreRelache();

  consigneJoy("Joystick DROIT : pousser vers", "le HAUT (monter la broche)");
  a = attendrePoussee(2, 2, signe);
  if (a < 0) {
    titreEcran("SENS JOYSTICKS");
    texteInfo("GPIO4 n'a pas bouge (joystick droit).", 90, TFT_ORANGE);
    texteInfo("Le fil doit etre sur la sortie qui varie", 115, TFT_ORANGE);
    texteInfo("en HAUT/BAS : essayer VRx <-> VRy.", 140, TFT_ORANGE);
    texteInfo("(X et Y sont enregistres)", 175, 0xBDF7);
  } else {
    sgnZ = signe;
    titreEcran("SENS JOYSTICKS");
    texteInfo("Reglage enregistre !", 110, TFT_GREENYELLOW);
  }
  memoire.putInt("srcX", srcX); memoire.putInt("sgnX", sgnX);
  memoire.putInt("sgnY", sgnY); memoire.putInt("sgnZ", sgnZ);
  attendreRelache();
  delay(2500);
}


// ---------- Test du câblage : valeurs brutes en direct ----------
void testCablage() {
  { int32_t tx, ty; unsigned long t = millis();
    while (ecran.getTouch(&tx, &ty) && millis() - t < 3000) delay(20); delay(200); }
  titreEcran("TEST CABLAGE");
  texteInfo("Toucher l'ecran pour quitter", 228, 0xBDF7);
  const char* noms[4] = {"GPIO1  X/Y VRx", "GPIO2  X/Y VRy", "GPIO4  Z", "GPIO5  pot."};
  const uint8_t pins[4] = {PIN_JOY_X, PIN_JOY_Y, PIN_JOY_Z, PIN_POT};
  const char* nb[6] = {"SW XY", "SW Z", "Z.XY", "Z.Z", "PAUSE", "ARRET"};
  const uint8_t pb[6] = {PIN_BTN_PAS, PIN_BTN_VIT, PIN_ZERO_XY, PIN_ZERO_Z, PIN_PAUSE, PIN_ARRET};
  ecran.setFont(&fonts::FreeSans9pt7b);
  while (true) {
    int32_t tx, ty;
    if (ecran.getTouch(&tx, &ty)) break;
    for (int i = 0; i < 4; i++) {
      int v = analogRead(pins[i]);
      int y = 50 + i * 30;
      ecran.setTextColor(TFT_WHITE, C_FOND);
      ecran.setTextDatum(textdatum_t::middle_left);
      ecran.drawString(noms[i], 6, y);
      ecran.fillRect(130, y - 8, 140, 16, 0x2104);
      ecran.fillRect(130, y - 8, 140 * v / 4095, 16, i < 3 ? 0x0410 : 0x8A22);
      ecran.setTextDatum(textdatum_t::middle_right);
      ecran.setTextPadding(44);
      ecran.drawString(String(v), 316, y);
      ecran.setTextPadding(0);
    }
    for (int i = 0; i < 6; i++) {
      bool appui = digitalRead(pb[i]) == LOW;
      BoutonEcran b = {4 + i * 53, 172, 50, 34};
      dessinerBouton(b, nb[i], appui ? 0x0400 : 0x2104);
    }
    delay(60);
  }
}

// ---------- Changement de machine (CNC <-> imprimante 3D) ----------
void changerMachine(int m) {
  if (jogActif) envoyerTempsReel(0x85);
  jogActif = false;
  cnc.stop();
  machine = m;
  memoire.putInt("mach", machine);
  impOk = false; impEtat = "?"; klippyEtat = "?"; tImpPoll = 0;
  etatGrbl = "?"; cncAReponduConnexion = false; tTentative = 0;
  for (int a = 0; a < 3; a++) pos[a] = 0;
  ouvrirMenu();
}

// ---------- Touches dans les menus ----------
void toucheMenus(int x, int y) {
  if (ecranActif == E_MENU) {
    if (dedans(bmRetour, x, y))       fermerMenu();
    else if (dedans(bmMachine, x, y)) changerMachine(machine == M_CNC ? M_IMP : M_CNC);
    else if (dedans(bmWifi, x, y))    scannerWifi();
    else if (dedans(bmIp, x, y))      { ecranActif = E_IP; saisie = (machine == M_IMP) ? impIp : cncIp; dessinerPave(); }
    else if (dedans(bmCherche, x, y)) chercherCnc();
    else if (dedans(bmCalib, x, y))   { calibrerTactile(); ouvrirMenu(); }
    else if (dedans(bmJoy, x, y))     { reglerSensJoysticks(); ouvrirMenu(); }
    else if (dedans(bmTest, x, y))    { testCablage(); ouvrirMenu(); }
  }
  else if (ecranActif == E_WIFI) {
    int pages = (nbReseaux + LIGNES_WIFI - 1) / LIGNES_WIFI;
    if (dedans(bmRetour, x, y)) { ouvrirMenu(); return; }
    if (pages > 1 && dedans(bwPrec, x, y)) { pageWifi = (pageWifi + pages - 1) % pages; dessinerListeWifi(); return; }
    if (pages > 1 && dedans(bwSuiv, x, y)) { pageWifi = (pageWifi + 1) % pages; dessinerListeWifi(); return; }
    for (int k = 0; k < LIGNES_WIFI; k++) {
      int i = pageWifi * LIGNES_WIFI + k;
      if (i < nbReseaux && dedans(ligneWifi(k), x, y)) {
        reseauChoisi = reseaux[i];
        if (reseauProtege[i]) ouvrirClavier(reseauChoisi == wifiNom ? wifiMdp : String(""));
        else connecterWifi(reseauChoisi, "");
        return;
      }
    }
  }
  else if (ecranActif == E_CLAVIER) {
    if (dedans(bkAnnuler, x, y)) { ouvrirMenu(); return; }
    if (dedans(bkOk, x, y))      { connecterWifi(reseauChoisi, saisie); return; }
    if (dedans(bkEff, x, y))     { if (saisie.length()) saisie.remove(saisie.length() - 1); dessinerSaisie(); return; }
    if (dedans(bkEsp, x, y))     { saisie += ' '; dessinerSaisie(); return; }
    if (dedans(bkMaj, x, y))     { modeClavier = (modeClavier == 1) ? 0 : 1; dessinerClavier(); return; }
    if (dedans(bkMode, x, y))    { modeClavier = (modeClavier == 2) ? 0 : 2; dessinerClavier(); return; }
    for (int r = 0; r < 3; r++) {
      const char* lettres = RANGEES[modeClavier][r];
      for (int k = 0; lettres[k]; k++)
        if (dedans(toucheClavier(r, k), x, y)) { saisie += lettres[k]; dessinerSaisie(); return; }
    }
  }
  else if (ecranActif == E_IP) {
    if (dedans(bkAnnuler, x, y)) { ouvrirMenu(); return; }
    if (dedans(bpEff, x, y))     { saisie = ""; dessinerSaisie(); return; }
    if (dedans(bpOk, x, y)) {
      IPAddress ip;
      if (ip.fromString(saisie) && machine == M_IMP) {
        impIp = ip.toString();
        memoire.putString("ipImp", impIp);
        impOk = false; tImpPoll = 0;
        ouvrirMenu();
      } else if (ip.fromString(saisie)) {
        cncIp = ip.toString();
        memoire.putString("ip", cncIp);
        cnc.stop();
        tTentative = 0;
        ouvrirMenu();
      } else {
        texteInfo("Adresse invalide (ex : 192.168.1.50)", 61, TFT_ORANGE);
      }
      return;
    }
    for (int r = 0; r < 3; r++)
      for (int c = 0; c < 4; c++)
        if (dedans(touchePave(r, c), x, y)) {
          if (PAVE[r][c][0] == '<') { if (saisie.length()) saisie.remove(saisie.length() - 1); }
          else if (saisie.length() < 15) saisie += PAVE[r][c];
          dessinerSaisie();
          return;
        }
  }
}

// ============================================================
//  Boutons physiques
// ============================================================
void gererBoutons() {
  Bouton* tous[] = {&bPas, &bVit, &bZeroXY, &bZeroZ, &bPause, &bArret};
  for (auto b : tous) b->lire();

  if (machine == M_IMP) {
    if (bArret.appuye) { actionImp("/printer/emergency_stop"); afficherMessage("ARRET ! > REDEMARRER"); return; }
    if (bPas.appuye) { modePas = (modePas + 1) % 4; if (confirmation < 0 && ecranActif == E_PRINCIPAL) rafraichirBouton(BE_PAS); }
    if (bVit.appuye) { rapide = !rapide;            if (confirmation < 0 && ecranActif == E_PRINCIPAL) rafraichirBouton(BE_VIT); }
    if (bPause.appuye) {
      if (impEtat == "paused")        { if (actionImp("/printer/print/resume")) afficherMessage("Reprise"); }
      else if (impEtat == "printing") { if (actionImp("/printer/print/pause"))  afficherMessage("Pause"); }
      else afficherMessage("Pas d'impression en cours");
    }
    if (bZeroXY.appuye || bZeroZ.appuye) afficherMessage("Sans effet sur l'imprimante");
    return;
  }
  if (bArret.appuye) {
    envoyerTempsReel(0x85);
    envoyerTempsReel('!');
    delay(150);
    envoyerTempsReel(0x18);      // reset GRBL (coupe la broche)
    jogActif = false; enAttente = 0; sonde = SONDE_RIEN;
    afficherMessage("ARRET ! > DEBLOQUER");
    return;
  }
  if (bPas.appuye) { modePas = (modePas + 1) % 4; if (confirmation < 0 && ecranActif == E_PRINCIPAL) rafraichirBouton(BE_PAS); }
  if (bVit.appuye) { rapide = !rapide;            if (confirmation < 0 && ecranActif == E_PRINCIPAL) rafraichirBouton(BE_VIT); }
  if (bPause.appuye) {
    if (etatGrbl == "Hold" || etatGrbl == "Door") { envoyerTempsReel('~'); afficherMessage("Reprise"); }
    else                                          { envoyerTempsReel('!'); afficherMessage("Pause"); }
  }
  if (bZeroXY.maintenu() || bZeroZ.maintenu()) afficherMessage("Maintenir 1 s...");
  if (bZeroXY.appuiLong && peutBouger()) { envoyer("G10 L20 P1 X0 Y0"); afficherMessage("ZERO XY fait"); }
  if (bZeroZ.appuiLong && peutBouger())  { envoyer("G10 L20 P1 Z0");    afficherMessage("ZERO Z fait"); }
}

// ============================================================
//  Voyant
// ============================================================
bool enCharge() { return digitalRead(PIN_CHARGE) == LOW; }     // CHRG à 0 V = charge en cours

// ---- Niveau de batterie (18650) ----
const float BATT_DIVISEUR = 2.0;      // pont de 2 résistances égales (100k + 100k ou 10k + 10k)
void lireBatterie() {
  static unsigned long t = 0;
  if (millis() - t < 1000 && battPct >= 0) return;
  t = millis();
  long s = 0;
  for (int i = 0; i < 16; i++) s += analogReadMilliVolts(PIN_BATT);
  float v = s / 16.0 / 1000.0 * BATT_DIVISEUR;
  battVolts = (battPct < 0) ? v : battVolts * 0.8 + v * 0.2;      // lissage
  // courbe de décharge d'une 18650 (tension -> %)
  const float tv[] = {3.30, 3.50, 3.60, 3.70, 3.75, 3.80, 3.85, 3.90, 4.00, 4.10, 4.20};
  const int   tp[] = {   0,    5,   10,   25,   40,   50,   60,   70,   82,   92,  100};
  int p = 0;
  if (battVolts >= tv[10]) p = 100;
  else for (int i = 0; i < 10; i++)
    if (battVolts >= tv[i] && battVolts < tv[i+1]) { p = tp[i] + (battVolts - tv[i]) / (tv[i+1] - tv[i]) * (tp[i+1] - tp[i]); break; }
  battPct = p;
}

void dessinerBatterie(int x, int y, uint16_t fond) {   // icône 34 x 16 dans la barre du haut
  lireBatterie();
  uint16_t c = enCharge() ? TFT_GREEN : battPct <= 15 ? TFT_RED : battPct <= 30 ? TFT_ORANGE : TFT_WHITE;
  ecran.fillRect(x - 2, y - 2, 42, 20, fond);
  ecran.drawRect(x, y, 34, 16, c);
  ecran.fillRect(x + 34, y + 5, 3, 6, c);
  int w = 30 * battPct / 100;
  ecran.fillRect(x + 2, y + 2, w, 12, c);
  ecran.setFont(&fonts::Font0);
  ecran.setTextDatum(textdatum_t::middle_center);
  ecran.setTextColor(battPct > 55 ? TFT_BLACK : TFT_WHITE);
  ecran.drawString(enCharge() ? "+" : String(battPct), x + 17, y + 8);
}

void gererVoyant() {
  bool on;
  if (machine == M_IMP) {
    if (!impOk)                                        on = (millis() / 1000) % 2;
    else if (klippyEtat != "ready" || impEtat == "error") on = (millis() / 150) % 2;
    else if (impEtat == "printing")                    on = (millis() / 400) % 2;
    else                                               on = true;
  }
  else if (!cnc.connected())         on = (millis() / 1000) % 2;   // lent : pas connecté
  else if (etatGrbl == "Alarm")      on = (millis() / 150) % 2;    // rapide : alarme
  else if (etatGrbl == "Run")        on = (millis() / 400) % 2;    // moyen : usinage
  else                               on = true;                    // fixe : prêt
  if (enCharge()) {                                              // charge : le voyant respire
    float ph = (millis() % 3000) / 3000.0;                       // un souffle toutes les 3 s
    float v = (1 - cos(2 * PI * ph)) / 2;
    ledcWrite(PIN_LED, (int)(v * v * 255));
  } else {
    ledcWrite(PIN_LED, on ? 255 : 0);
  }
}

// ============================================================
//  Mises à jour par Wi-Fi (ArduinoOTA) et mode programmation forcé
// ============================================================
bool majPrete = false;
void gererMaj() {
  if (!majPrete && WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.setHostname("telecommande-cnc");
    ArduinoOTA.setPassword(MDP_MAJ);
    ArduinoOTA.onStart([]() {
      envoyerTempsReel(0x85);                    // on arrête tout mouvement
      ecran.fillScreen(TFT_BLACK);
      ecran.setFont(&fonts::FreeSansBold12pt7b);
      ecran.setTextColor(TFT_ORANGE);
      ecran.setTextDatum(textdatum_t::middle_center);
      ecran.drawString("Mise a jour...", 160, 100);
    });
    ArduinoOTA.onProgress([](unsigned int fait, unsigned int total) {
      int w = (int)(280.0 * fait / total);
      ecran.fillRect(20, 140, w, 16, TFT_ORANGE);
      ecran.drawRect(20, 140, 280, 16, TFT_WHITE);
    });
    ArduinoOTA.begin();
    majPrete = true;
  }
  if (majPrete) ArduinoOTA.handle();
}

void redemarrerEnModeProgrammation() {
  ecran.fillScreen(TFT_BLACK);
  ecran.setFont(&fonts::FreeSansBold12pt7b);
  ecran.setTextColor(TFT_ORANGE);
  ecran.setTextDatum(textdatum_t::middle_center);
  ecran.drawString("Mode programmation", 160, 100);
  ecran.setFont(&fonts::FreeSans9pt7b);
  ecran.setTextColor(TFT_WHITE);
  ecran.drawString("Televerser par la prise USB-OTG", 160, 140);
  delay(1500);
  REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);  // prochain démarrage = mode programmation
  esp_restart();
}

// ============================================================
void calibrerTactile() {
  uint16_t cal[8];
  ecran.fillScreen(TFT_BLACK);
  ecran.setFont(&fonts::FreeSansBold9pt7b);
  ecran.setTextColor(TFT_WHITE);
  ecran.setTextDatum(textdatum_t::middle_center);
  ecran.drawString("Touchez les fleches", 160, 110);
  ecran.drawString("(avec un stylet)", 160, 135);
  ecran.calibrateTouch(cal, TFT_WHITE, TFT_BLACK, 20);
  memoire.putBytes("cal", cal, sizeof(cal));
  ecran.setTouchCalibrate(cal);
}

void setup() {
  Serial.begin(115200);
  bPas.init(PIN_BTN_PAS); bVit.init(PIN_BTN_VIT);
  bZeroXY.init(PIN_ZERO_XY); bZeroZ.init(PIN_ZERO_Z);
  bPause.init(PIN_PAUSE); bArret.init(PIN_ARRET);
  ledcAttach(PIN_LED, 5000, 8);                 // voyant en PWM (pour l'effet respiration)
  pinMode(PIN_CHARGE, INPUT_PULLUP);     // CHRG : sortie à collecteur ouvert
  analogSetPinAttenuation(PIN_BATT, ADC_11db);   // mesure jusqu à ~3,1 V

  analogReadResolution(12);
  for (int a = 0; a < 3; a++) {                 // ne pas toucher les joysticks au démarrage
    long s = 0;
    for (int i = 0; i < 64; i++) { s += analogRead(PIN_AXE[a]); delay(2); }
    centre[a] = s / 64;
  }

  ecran.init();
  ecran.setRotation(1);                         // paysage (mettre 3 si l'image est à l'envers)
  ecran.setBrightness(200);

  // ZÉRO XY + ZÉRO Z maintenus à l'allumage -> mode programmation (sans bouton BOOT)
  if (digitalRead(PIN_ZERO_XY) == LOW && digitalRead(PIN_ZERO_Z) == LOW) redemarrerEnModeProgrammation();

  // Calibrage du tactile (premier démarrage, ou PAUSE maintenue à l'allumage)
  memoire.begin("telecnc", false);
  uint16_t cal[8];
  bool dejaCalibre = memoire.getBytes("cal", cal, sizeof(cal)) == sizeof(cal);
  if (!dejaCalibre || digitalRead(PIN_PAUSE) == LOW) calibrerTactile();
  else ecran.setTouchCalibrate(cal);

  // Colonne de boutons tactiles à droite
  for (int i = 0; i < NB_BE; i++) be[i] = {198, 29 + i * 35, 120, 33};

  ecran.fillScreen(TFT_BLACK);
  ecran.setFont(&fonts::FreeSansBold18pt7b);
  ecran.setTextColor(TFT_ORANGE);
  ecran.setTextDatum(textdatum_t::middle_center);
  ecran.drawString(machine == M_IMP ? "K2" : "TTC450", 160, 100);
  ecran.setFont(&fonts::FreeSans9pt7b);
  ecran.setTextColor(TFT_WHITE);
  ecran.drawString(machine == M_IMP ? "Telecommande imprimante 3D" : "Telecommande CNC", 160, 140);

  // Réglages mémorisés (sinon valeurs du haut du programme)
  wifiNom = memoire.getString("ssid", WIFI_NOM);
  wifiMdp = memoire.getString("mdp", WIFI_MDP);
  cncIp   = memoire.getString("ip", CNC_IP);
  cncPort = memoire.getUShort("port", CNC_PORT);
  srcX = memoire.getInt("srcX", 0);
  sgnX = memoire.getInt("sgnX", 1);
  sgnY = memoire.getInt("sgnY", 1);
  sgnZ = memoire.getInt("sgnZ", 1);
  machine = memoire.getInt("mach", M_CNC);
  impIp   = memoire.getString("ipImp", IMP_IP);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(wifiNom.c_str(), wifiMdp.c_str());
  delay(1200);
  aRedessiner = true;

  // Premier démarrage sans Wi-Fi réglé : on propose tout de suite la liste des réseaux
  if (wifiNom == "MonWifi") { ecranActif = E_WIFI; scannerWifi(); }
}

void loop() {
  gererMaj();
  if (machine == M_IMP) gererImprimante();
  else { gererConnexion(); lireCNC(); }
  gererBoutons();
  gererTactile();
  if (ecranActif == E_PRINCIPAL) { gererJog(); majEcran(); }
  else if (ecranActif == E_MENU) majMenu();
  gererAvance();
  gererVoyant();
}
