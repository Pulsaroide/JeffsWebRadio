// ============================================================
//  JEFF'S WEB RADIO — v1.7
//  Pour M5Stack Cardputer ADV (ESP32-S3 / Stamp-S3A)
//
//  CHANGEMENTS v1.7 :
//    - MIGRATION vers ESP32-audioI2S (schreibfaul1 v3.0.13+)
//      → remplace ESP8266Audio qui conflictait avec M5Unified
//      → audio.setVolume(0-21) contrôle réellement l'ES8311
//    - Volume FONCTIONNEL : contrôle réel via I2S natif ESP32
//    - UI améliorée : panneau d'aide complet des touches
//    - Sud Radio SUPPRIMÉE
//    - Nostalgie : URL corrigée (stream.nostalgie.fr)
//    - WiFiManager conservé (portail captif)
//
//  LIBRAIRIES REQUISES (Arduino IDE Library Manager) :
//    - M5Cardputer (M5Stack)
//    - WiFiManager (tzapu)
//    - ESP32-audioI2S par schreibfaul1 v3.0.13+
//    - ArduinoJson
//
//  PARTITION : Huge APP (3MB No OTA / 1MB SPIFFS)
// ============================================================

#include <M5Cardputer.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "Audio.h"   // ESP32-audioI2S par schreibfaul1

// ============================================================
//  PINS I2S — Cardputer ADV (ES8311 codec via Stamp-S3A)
//  Ces pins sont câblées en dur sur le PCB du Cardputer ADV
// ============================================================
#define I2S_BCLK   41
#define I2S_LRCLK  43
#define I2S_DOUT   42

// ============================================================
//  STATIONS RADIO
//  Format : { "Nom", "URL stream", "URL metadata API" }
// ============================================================
struct Station {
    const char* name;
    const char* url;
    const char* metaUrl;
};

Station STATIONS[] = {
    // --- Françaises ---
    { "Nostalgie",       "http://stream.nostalgie.fr/nostalgie.mp3",                          "" },
    { "FIP",             "http://icecast.radiofrance.fr/fip-midfi.mp3",                       "https://api.radiofrance.fr/livemeta/pull/7" },
    // --- Radio Paradise ---
    { "RP Main Mix",     "http://stream.radioparadise.com/mp3-128",                           "https://api.radioparadise.com/api/now_playing?block_id=0&format=json" },
    { "RP Mellow Mix",   "http://stream.radioparadise.com/mellow-128",                        "https://api.radioparadise.com/api/now_playing?block_id=1&format=json" },
    { "RP Rock Mix",     "http://stream.radioparadise.com/rock-128",                          "https://api.radioparadise.com/api/now_playing?block_id=2&format=json" },
    // --- SomaFM ---
    { "SomaFM Groove",   "http://ice.somafm.com/groovesalad-128-mp3",                         "" },
    { "SomaFM Space",    "http://ice.somafm.com/spacestation-128-mp3",                        "" },
    { "SomaFM 80s",      "http://ice.somafm.com/u80s-128-mp3",                                "" },
    // --- Spéciales ---
    { "Nightride FM",    "https://stream.nightride.fm/nightride.mp3",                         "" },
    { "Lofi Hip Hop",    "http://stream.zeno.fm/f3wvbbqmdg8uv",                               "" },
    // --- Blues ---
    { "BluesWave",       "http://blueswave.radio:8000/blueswave",                             "" },
    { "Blues Radio",     "http://198.58.98.83/stream/1/",                                     "" },
    // --- Jazz ---
    { "101 Smooth Jazz", "http://strm112.1.fm/smoothjazz_mobile_mp3",                         "" },
    { "Smooth Jazz DLX", "http://agnes.torontocast.com:8142/stream",                          "" },
    // --- Lounge ---
    { "Chillout Lounge", "http://strm112.1.fm/chilloutlounge_mobile_mp3",                     "" },
    { "Sensual Lounge",  "http://agnes.torontocast.com:8146/stream",                          "" },
};

const int NUM_STATIONS = sizeof(STATIONS) / sizeof(STATIONS[0]);

// ============================================================
//  OBJET AUDIO — ESP32-audioI2S
//  audio.setVolume(0-21) contrôle directement l'ES8311
// ============================================================
Audio audio;

// ============================================================
//  ÉTAT APPLICATION
// ============================================================
int   currentStation = 0;
int   listScrollTop  = 0;
bool  isPlaying      = false;
int   volume         = 10;    // 0-21 (échelle native ESP32-audioI2S)
bool  isMuted        = false;
int   volumeBeforeMute = 10;

String nowPlaying    = "";
String nowArtist     = "";
String statusMsg     = "Pret - Appuie ENTER";

unsigned long lastMetaFetch  = 0;
const long    META_INTERVAL  = 15000;
int   tickerOffset   = 0;
unsigned long lastTickerMove = 0;

// Mode aide
bool  showHelp = false;

// Reconnexion auto
unsigned long lastAudioCheck = 0;
const long    AUDIO_CHECK_INTERVAL = 3000;

// ============================================================
//  COULEURS (palette bleue profonde)
// ============================================================
#define COL_BG      M5.Lcd.color565(8,   8,  18)
#define COL_HDR_BG  M5.Lcd.color565(0,   40,  80)
#define COL_HDR_FG  M5.Lcd.color565(0,  180, 255)
#define COL_SEL_BG  M5.Lcd.color565(20,  50,  90)
#define COL_SEL_FG  M5.Lcd.color565(255, 200,   0)
#define COL_TEXT    M5.Lcd.color565(220, 220, 220)
#define COL_DIM     M5.Lcd.color565(90,  90,  90)
#define COL_PLAY    M5.Lcd.color565(0,  255, 120)
#define COL_STOP    M5.Lcd.color565(255,  60,  60)
#define COL_META_BG M5.Lcd.color565(5,   25,  45)
#define COL_META_FG M5.Lcd.color565(180, 230, 255)
#define COL_SCROLL  M5.Lcd.color565(60,  60, 100)
#define COL_BAR_BG  M5.Lcd.color565(15,  15,  35)
#define COL_HELP_BG M5.Lcd.color565(0,   20,  40)
#define COL_KEY     M5.Lcd.color565(255, 200,   0)
#define COL_VOL_BG  M5.Lcd.color565(0,   35,  70)
#define COL_VOL_FG  M5.Lcd.color565(0,  200, 100)

// ============================================================
//  LAYOUT ÉCRAN (240 x 135)
// ============================================================
#define W             240
#define H             135
#define HEADER_H       18   // Titre + WiFi
#define META_H         20   // Titre morceau en cours
#define VOLBAR_H       10   // Barre de volume
#define LIST_Y        (HEADER_H + META_H + VOLBAR_H)
#define LIST_ITEM_H    15
#define HINT_H         14   // Barre d'aide touches
#define LIST_H        (H - LIST_Y - HINT_H)
#define VISIBLE_ITEMS (LIST_H / LIST_ITEM_H)
#define HINT_Y        (H - HINT_H)

// ============================================================
//  CALLBACKS ESP32-audioI2S
//  Ces fonctions sont appelées automatiquement par la lib
// ============================================================
void audio_info(const char* info) {
    // Appelé pour les infos de stream (bitrate, etc.)
    Serial.printf("[AUDIO INFO] %s\n", info);
}

void audio_id3data(const char* info) {
    // Tags ID3 — récupère titre/artiste du stream
    Serial.printf("[ID3] %s\n", info);
    String s = String(info);
    if (s.startsWith("StreamTitle:")) {
        String title = s.substring(12);
        title.trim();
        // Format "Artiste - Titre" dans le StreamTitle ICY
        int sep = title.indexOf(" - ");
        if (sep > 0) {
            nowArtist  = title.substring(0, sep);
            nowPlaying = title.substring(sep + 3);
        } else {
            nowArtist  = "";
            nowPlaying = title.length() > 0 ? title : String(STATIONS[currentStation].name);
        }
        tickerOffset = 0;
        drawMetaBand();
    }
}

void audio_eof_stream(const char* info) {
    // Fin du stream → reconnexion automatique
    Serial.printf("[EOF] %s\n", info);
    if (isPlaying) {
        statusMsg = "Reconnexion...";
        drawMetaBand();
        delay(1500);
        startStream();
    }
}

void audio_error(const char* info) {
    Serial.printf("[AUDIO ERROR] %s\n", info);
    statusMsg = "Erreur stream";
    isPlaying = false;
    drawUI();
}

// ============================================================
//  WIFI — portail captif WiFiManager
// ============================================================
void startWiFiPortal() {
    M5.Lcd.fillScreen(COL_BG);
    M5.Lcd.setTextFont(1);
    M5.Lcd.setTextSize(1);

    // Logo
    M5.Lcd.setTextColor(COL_HDR_FG, COL_BG);
    M5.Lcd.setTextSize(2);
    M5.Lcd.drawString("Jeff's", 65, 8);
    M5.Lcd.drawString("Web Radio", 45, 30);
    M5.Lcd.setTextSize(1);

    M5.Lcd.setTextColor(COL_SEL_FG, COL_BG);
    M5.Lcd.drawString("v1.7  Cardputer ADV", 42, 58);

    M5.Lcd.setTextColor(COL_DIM, COL_BG);
    M5.Lcd.drawString("WiFi: JeffsRadio-Setup", 28, 78);
    M5.Lcd.drawString("puis 192.168.4.1", 52, 92);

    WiFiManager wm;
    wm.setAPStaticIPConfig(
        IPAddress(192,168,4,1),
        IPAddress(192,168,4,1),
        IPAddress(255,255,255,0)
    );
    wm.setConfigPortalTimeout(180);
    bool ok = wm.autoConnect("JeffsRadio-Setup");

    M5.Lcd.fillScreen(COL_BG);
    M5.Lcd.setTextSize(1);
    if (ok) {
        M5.Lcd.setTextColor(COL_PLAY, COL_BG);
        M5.Lcd.drawString("WiFi connecte!", 30, 55);
        M5.Lcd.setTextColor(COL_TEXT, COL_BG);
        M5.Lcd.drawString(WiFi.localIP().toString(), 60, 72);
        delay(1200);
    } else {
        M5.Lcd.setTextColor(COL_STOP, COL_BG);
        M5.Lcd.drawString("WiFi non configure", 25, 55);
        delay(2500);
    }
}

// ============================================================
//  AUDIO — démarrage / arrêt
// ============================================================
void applyVolume() {
    if (isMuted) {
        audio.setVolume(0);
    } else {
        audio.setVolume(volume);  // 0-21, natif ESP32-audioI2S
    }
}

void stopStream() {
    if (isPlaying) {
        audio.stopSong();
        isPlaying  = false;
        statusMsg  = "Arrete";
        nowPlaying = "";
        nowArtist  = "";
        tickerOffset = 0;
    }
}

void startStream() {
    stopStream();
    statusMsg = "Connexion...";
    drawUI();

    bool ok = audio.connecttohost(STATIONS[currentStation].url);

    if (ok) {
        isPlaying  = true;
        statusMsg  = "En lecture";
        nowPlaying = STATIONS[currentStation].name;
        tickerOffset = 0;
        applyVolume();
        lastAudioCheck = millis();
        fetchMetadata();
    } else {
        statusMsg = "Erreur connexion!";
        isPlaying = false;
    }
    drawUI();
}

// ============================================================
//  METADATA — API JSON pour stations qui la supportent
// ============================================================
void fetchMetadata() {
    if (strlen(STATIONS[currentStation].metaUrl) == 0) return;
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    http.begin(STATIONS[currentStation].metaUrl);
    http.addHeader("User-Agent", "JeffsWebRadio/1.7");
    http.setTimeout(5000);

    if (http.GET() == 200) {
        JsonDocument doc;
        String body = http.getString();
        if (!deserializeJson(doc, body)) {
            String stationName = String(STATIONS[currentStation].name);
            // Radio Paradise (stations 2-4)
            if (currentStation >= 2 && currentStation <= 4) {
                nowPlaying = doc["title"]  | stationName;
                nowArtist  = doc["artist"] | String("");
            }
            // FIP (station 1)
            else if (currentStation == 1) {
                JsonObject now = doc["now"];
                nowPlaying = now["firstLine"]["title"]  | stationName;
                nowArtist  = now["secondLine"]["title"] | String("");
            }
            // Autres avec API
            else {
                nowPlaying = doc["title"]  | stationName;
                nowArtist  = doc["artist"] | String("");
            }
            tickerOffset = 0;
        }
    }
    http.end();
    lastMetaFetch = millis();
}

// ============================================================
//  DESSIN UI — HEADER
// ============================================================
void drawHeader() {
    M5.Lcd.fillRect(0, 0, W, HEADER_H, COL_HDR_BG);
    M5.Lcd.setTextFont(1);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COL_HDR_FG, COL_HDR_BG);
    M5.Lcd.drawString(">> JEFF'S WEB RADIO v1.7", 4, 5);

    // Indicateur WiFi
    bool wifiOk = (WiFi.status() == WL_CONNECTED);
    M5.Lcd.setTextColor(wifiOk ? COL_PLAY : COL_STOP, COL_HDR_BG);
    M5.Lcd.drawString(wifiOk ? "WiFi" : "NoWi", W - 30, 5);
}

// ============================================================
//  DESSIN UI — BANDE METADATA (titre en cours)
// ============================================================
void drawMetaBand() {
    M5.Lcd.fillRect(0, HEADER_H, W, META_H, COL_META_BG);
    M5.Lcd.setTextFont(1);
    M5.Lcd.setTextSize(1);

    String full = "";
    if (nowArtist.length() > 0)
        full = nowArtist + " - " + nowPlaying;
    else if (nowPlaying.length() > 0)
        full = nowPlaying;
    else
        full = statusMsg;

    const int MAX_VIS = 28;
    String display = full;
    if ((int)full.length() > MAX_VIS) {
        String loop = full + "   " + full;
        if (tickerOffset >= (int)(full.length() + 3)) tickerOffset = 0;
        display = loop.substring(tickerOffset, tickerOffset + MAX_VIS);
    }

    if (isPlaying) {
        // Indicateur ♪ animé
        M5.Lcd.setTextColor(COL_PLAY, COL_META_BG);
        M5.Lcd.drawString((millis() / 500) % 2 == 0 ? ">" : " ", 3, HEADER_H + 5);
        M5.Lcd.setTextColor(COL_META_FG, COL_META_BG);
    } else {
        M5.Lcd.setTextColor(COL_DIM, COL_META_BG);
    }
    M5.Lcd.drawString(display, 14, HEADER_H + 5);
}

// ============================================================
//  DESSIN UI — BARRE DE VOLUME
// ============================================================
void drawVolBar() {
    int y = HEADER_H + META_H;
    M5.Lcd.fillRect(0, y, W, VOLBAR_H, COL_VOL_BG);

    // Label
    M5.Lcd.setTextFont(1);
    M5.Lcd.setTextSize(1);

    if (isMuted) {
        M5.Lcd.setTextColor(COL_STOP, COL_VOL_BG);
        M5.Lcd.drawString("MUTE  [M]", 4, y + 1);
    } else {
        M5.Lcd.setTextColor(COL_VOL_FG, COL_VOL_BG);
        M5.Lcd.drawString("Vol", 4, y + 1);

        // Barre graphique volume (0-21 → largeur)
        int barX = 24;
        int barW = 140;
        int barH = 6;
        int barY = y + 2;
        int filled = (volume * barW) / 21;

        M5.Lcd.fillRect(barX, barY, barW, barH, COL_BAR_BG);
        if (filled > 0)
            M5.Lcd.fillRect(barX, barY, filled, barH, COL_VOL_FG);

        // Valeur numérique
        M5.Lcd.setTextColor(COL_TEXT, COL_VOL_BG);
        M5.Lcd.drawString(String(volume) + "/21", 170, y + 1);
    }
    // Aide rapide touches volume
    M5.Lcd.setTextColor(COL_DIM, COL_VOL_BG);
    M5.Lcd.drawString("+/-:vol", W - 46, y + 1);
}

// ============================================================
//  DESSIN UI — LISTE DES STATIONS
// ============================================================
void drawStationList() {
    // Scroll auto pour garder la sélection visible
    if (currentStation < listScrollTop)
        listScrollTop = currentStation;
    if (currentStation >= listScrollTop + VISIBLE_ITEMS)
        listScrollTop = currentStation - VISIBLE_ITEMS + 1;

    M5.Lcd.fillRect(0, LIST_Y, W, LIST_H, COL_BG);

    for (int i = 0; i < VISIBLE_ITEMS; i++) {
        int idx = listScrollTop + i;
        if (idx >= NUM_STATIONS) break;
        int y = LIST_Y + i * LIST_ITEM_H;
        bool sel = (idx == currentStation);

        if (sel) {
            M5.Lcd.fillRect(0, y, W - 4, LIST_ITEM_H, COL_SEL_BG);
            // Barre gauche jaune
            M5.Lcd.fillRect(0, y, 3, LIST_ITEM_H, COL_SEL_FG);
            M5.Lcd.setTextColor(COL_SEL_FG, COL_SEL_BG);
        } else {
            M5.Lcd.setTextColor(COL_DIM, COL_BG);
        }
        M5.Lcd.setTextFont(1);
        M5.Lcd.setTextSize(1);
        M5.Lcd.drawString(String(idx + 1) + ". " + STATIONS[idx].name, 7, y + 3);

        // Badge ON AIR pour la station en cours de lecture
        if (sel && isPlaying) {
            M5.Lcd.fillRect(W - 50, y + 1, 46, LIST_ITEM_H - 2, COL_PLAY);
            M5.Lcd.setTextColor(COL_BG, COL_PLAY);
            M5.Lcd.drawString("ON AIR", W - 46, y + 3);
        }
    }

    // Scrollbar
    if (NUM_STATIONS > VISIBLE_ITEMS) {
        int bH = max(4, LIST_H * VISIBLE_ITEMS / NUM_STATIONS);
        int bY = LIST_Y + (LIST_H - bH) * listScrollTop / max(1, NUM_STATIONS - VISIBLE_ITEMS);
        M5.Lcd.fillRect(W - 3, LIST_Y, 3, LIST_H, COL_BAR_BG);
        M5.Lcd.fillRect(W - 3, bY, 3, bH, COL_SCROLL);
    }
}

// ============================================================
//  DESSIN UI — BARRE D'AIDE TOUCHES (en bas)
// ============================================================
void drawHintBar() {
    M5.Lcd.fillRect(0, HINT_Y, W, HINT_H, COL_BAR_BG);
    M5.Lcd.setTextFont(1);
    M5.Lcd.setTextSize(1);

    if (!isPlaying) {
        // Mode stop : montre comment lancer
        M5.Lcd.setTextColor(COL_KEY, COL_BAR_BG);
        M5.Lcd.drawString("[ENTER]", 4, HINT_Y + 2);
        M5.Lcd.setTextColor(COL_TEXT, COL_BAR_BG);
        M5.Lcd.drawString(":play", 50, HINT_Y + 2);
        M5.Lcd.setTextColor(COL_KEY, COL_BAR_BG);
        M5.Lcd.drawString("[N][P]", 95, HINT_Y + 2);
        M5.Lcd.setTextColor(COL_TEXT, COL_BAR_BG);
        M5.Lcd.drawString(":station", 133, HINT_Y + 2);
        M5.Lcd.setTextColor(COL_KEY, COL_BAR_BG);
        M5.Lcd.drawString("[?]", 191, HINT_Y + 2);
        M5.Lcd.setTextColor(COL_TEXT, COL_BAR_BG);
        M5.Lcd.drawString(":aide", 209, HINT_Y + 2);
    } else {
        // Mode play : montre les contrôles actifs
        M5.Lcd.setTextColor(COL_KEY, COL_BAR_BG);
        M5.Lcd.drawString("[ENTER]", 4, HINT_Y + 2);
        M5.Lcd.setTextColor(COL_TEXT, COL_BAR_BG);
        M5.Lcd.drawString(":stop", 50, HINT_Y + 2);
        M5.Lcd.setTextColor(COL_KEY, COL_BAR_BG);
        M5.Lcd.drawString("[+][-]", 90, HINT_Y + 2);
        M5.Lcd.setTextColor(COL_TEXT, COL_BAR_BG);
        M5.Lcd.drawString(":vol", 126, HINT_Y + 2);
        M5.Lcd.setTextColor(COL_KEY, COL_BAR_BG);
        M5.Lcd.drawString("[M]", 160, HINT_Y + 2);
        M5.Lcd.setTextColor(COL_TEXT, COL_BAR_BG);
        M5.Lcd.drawString(":mute", 178, HINT_Y + 2);
        M5.Lcd.setTextColor(COL_KEY, COL_BAR_BG);
        M5.Lcd.drawString("[?]", 215, HINT_Y + 2);
    }
}

// ============================================================
//  DESSIN UI — ÉCRAN D'AIDE COMPLET (touche ?)
// ============================================================
void drawHelpScreen() {
    M5.Lcd.fillScreen(COL_HELP_BG);
    M5.Lcd.setTextFont(1);
    M5.Lcd.setTextSize(1);

    // Titre
    M5.Lcd.fillRect(0, 0, W, 14, COL_HDR_BG);
    M5.Lcd.setTextColor(COL_HDR_FG, COL_HDR_BG);
    M5.Lcd.drawString("  AIDE — JEFF'S WEB RADIO v1.7", 4, 3);

    // Tableau des touches
    struct HelpLine { const char* key; const char* desc; };
    HelpLine lines[] = {
        { "ENTER",   "Lancer / Arreter la radio" },
        { "N",       "Station suivante" },
        { "P",       "Station precedente" },
        { "+  /  =", "Augmenter le volume" },
        { "-",       "Diminuer le volume" },
        { "M",       "Mute / Unmute" },
        { "R",       "Recharger les metadata" },
        { "S",       "Stop forcé" },
        { "?",       "Afficher / quitter cette aide" },
    };

    int numLines = sizeof(lines) / sizeof(lines[0]);
    for (int i = 0; i < numLines; i++) {
        int y = 18 + i * 12;
        // Fond alterné
        uint16_t rowBg = (i % 2 == 0) ? COL_HELP_BG : M5.Lcd.color565(5, 30, 55);
        M5.Lcd.fillRect(0, y, W, 12, rowBg);
        // Touche
        M5.Lcd.fillRect(2, y + 1, 52, 10, M5.Lcd.color565(0, 50, 100));
        M5.Lcd.setTextColor(COL_KEY, M5.Lcd.color565(0, 50, 100));
        M5.Lcd.drawString(lines[i].key, 4, y + 2);
        // Description
        M5.Lcd.setTextColor(COL_TEXT, rowBg);
        M5.Lcd.drawString(lines[i].desc, 58, y + 2);
    }

    // Bas de page
    M5.Lcd.fillRect(0, H - 12, W, 12, COL_BAR_BG);
    M5.Lcd.setTextColor(COL_DIM, COL_BAR_BG);
    M5.Lcd.drawString("Volume : 0 a 21  |  Appuie ? pour fermer", 4, H - 10);
}

// ============================================================
//  DESSIN UI — COMPLET
// ============================================================
void drawUI() {
    if (showHelp) {
        drawHelpScreen();
        return;
    }
    drawHeader();
    drawMetaBand();
    drawVolBar();
    drawStationList();
    drawHintBar();
}

// ============================================================
//  CLAVIER
// ============================================================
void handleKeyboard() {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

    // ENTER : play/stop
    if (status.enter) {
        showHelp = false;
        if (isPlaying) stopStream();
        else           startStream();
        drawUI();
        return;
    }

    // Touches lettres / symboles
    for (auto key : status.word) {
        switch (key) {

            // Navigation stations
            case 'n': case 'N':
                if (isPlaying) stopStream();
                currentStation = (currentStation + 1) % NUM_STATIONS;
                drawStationList();
                drawHintBar();
                break;

            case 'p': case 'P':
                if (isPlaying) stopStream();
                currentStation = (currentStation - 1 + NUM_STATIONS) % NUM_STATIONS;
                drawStationList();
                drawHintBar();
                break;

            // Volume +
            case '+': case '=':
                if (isMuted) { isMuted = false; }
                volume = min(21, volume + 1);
                applyVolume();
                drawVolBar();
                drawHintBar();
                break;

            // Volume -
            case '-':
                volume = max(0, volume - 1);
                if (isMuted) { isMuted = false; }
                applyVolume();
                drawVolBar();
                drawHintBar();
                break;

            // Mute toggle
            case 'm': case 'M':
                isMuted = !isMuted;
                applyVolume();
                drawVolBar();
                drawHintBar();
                break;

            // Stop forcé
            case 's': case 'S':
                if (isPlaying) {
                    stopStream();
                    drawUI();
                }
                break;

            // Refresh metadata
            case 'r': case 'R':
                if (isPlaying) {
                    fetchMetadata();
                    drawMetaBand();
                }
                break;

            // Aide
            case '?':
                showHelp = !showHelp;
                drawUI();
                break;
        }
    }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    M5Cardputer.begin(cfg);

    M5.Lcd.setRotation(1);
    M5.Lcd.fillScreen(COL_BG);
    M5.Lcd.setTextFont(1);

    // Splash screen
    M5.Lcd.setTextColor(COL_HDR_FG, COL_BG);
    M5.Lcd.setTextSize(2);
    M5.Lcd.drawString("Jeff's", 62, 15);
    M5.Lcd.drawString("Web Radio", 40, 42);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COL_DIM, COL_BG);
    M5.Lcd.drawString("v1.7  Cardputer ADV", 40, 80);
    M5.Lcd.setTextColor(COL_SEL_FG, COL_BG);
    M5.Lcd.drawString("by Jeff  (powered by Claude)", 18, 96);
    delay(1800);

    // WiFi
    startWiFiPortal();

    // Init audio I2S — ES8311 du Cardputer ADV
    // GPIO8/9 = I2C (géré par M5Cardputer.begin)
    // I2S data pins câblés sur le Stamp-S3A
    audio.setPinout(I2S_BCLK, I2S_LRCLK, I2S_DOUT);
    audio.setVolume(volume);

    drawUI();
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
    // Boucle audio — DOIT être appelé le plus souvent possible
    audio.loop();

    // Mise à jour clavier
    handleKeyboard();

    // Ticker texte défilant
    if (isPlaying && millis() - lastTickerMove > 350) {
        tickerOffset++;
        drawMetaBand();
        lastTickerMove = millis();
    }

    // Refresh metadata périodique
    if (isPlaying && millis() - lastMetaFetch > META_INTERVAL) {
        fetchMetadata();
        drawMetaBand();
    }

    // Petite pause pour ne pas saturer le CPU
    delay(5);
}
