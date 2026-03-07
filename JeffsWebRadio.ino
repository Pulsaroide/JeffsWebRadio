// ============================================================
//  JEFF'S WEB RADIO — v1.8
//  Pour M5Stack Cardputer ADV (ESP32-S3 / Stamp-S3A)
//
//  LIBRAIRIES REQUISES (toutes disponibles via Library Manager) :
//    - M5Cardputer       (M5Stack)
//    - WiFiManager       (tzapu)  v2.0.17+
//    - ESP8266Audio      (earlephilhower)
//    - ArduinoJson       v7+
//
//  PARTITION : Huge APP (3MB No OTA / 1MB SPIFFS)
//
//  CORRECTIONS v1.8 :
//    - Volume : M5Cardputer.Speaker.setVolume() seul pilote l'ES8311
//               AudioOutputI2S::SetGain() restera à 1.0 (neutre)
//               Le Speaker est réinitialisé à chaque startStream()
//               avec le bon volume AVANT de démarrer l'I2S
//    - UI     : barre de volume graphique + écran aide complet [?]
//    - Nostalgie URL corrigée, Sud Radio supprimée
// ============================================================

#include <M5Cardputer.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "AudioFileSourceHTTPStream.h"
#include "AudioFileSourceBuffer.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

// ============================================================
//  PINS I2S — Cardputer ADV (Stamp-S3A, codec ES8311)
// ============================================================
#define I2S_BCLK   41
#define I2S_LRCLK  43
#define I2S_DOUT   42

// ============================================================
//  STATIONS
// ============================================================
struct Station {
    const char* name;
    const char* url;
    const char* metaUrl;
};

Station STATIONS[] = {
    { "Nostalgie",       "http://cdn.nrjaudio.fm/adwz1/fr/30601/mp3_128.mp3",                 "" },
    { "FIP",             "http://icecast.radiofrance.fr/fip-midfi.mp3",                        "https://api.radiofrance.fr/livemeta/pull/7" },
    { "RP Main Mix",     "http://stream.radioparadise.com/mp3-128",                            "https://api.radioparadise.com/api/now_playing?block_id=0&format=json" },
    { "RP Mellow Mix",   "http://stream.radioparadise.com/mellow-128",                         "https://api.radioparadise.com/api/now_playing?block_id=1&format=json" },
    { "RP Rock Mix",     "http://stream.radioparadise.com/rock-128",                           "https://api.radioparadise.com/api/now_playing?block_id=2&format=json" },
    { "SomaFM Groove",   "http://ice.somafm.com/groovesalad-128-mp3",                          "" },
    { "SomaFM Space",    "http://ice.somafm.com/spacestation-128-mp3",                         "" },
    { "SomaFM 80s",      "http://ice.somafm.com/u80s-128-mp3",                                 "" },
    { "Nightride FM",    "https://stream.nightride.fm/nightride.mp3",                          "" },
    { "Lofi Hip Hop",    "http://stream.zeno.fm/f3wvbbqmdg8uv",                                "" },
    { "BluesWave",       "http://blueswave.radio:8000/blueswave",                              "" },
    { "Blues Radio",     "http://198.58.98.83/stream/1/",                                      "" },
    { "101 Smooth Jazz", "http://strm112.1.fm/smoothjazz_mobile_mp3",                          "" },
    { "Smooth Jazz DLX", "http://agnes.torontocast.com:8142/stream",                           "" },
    { "Chillout Lounge", "http://strm112.1.fm/chilloutlounge_mobile_mp3",                      "" },
    { "Sensual Lounge",  "http://agnes.torontocast.com:8146/stream",                           "" },
};
const int NUM_STATIONS = sizeof(STATIONS) / sizeof(STATIONS[0]);

// ============================================================
//  AUDIO ENGINE (ESP8266Audio)
// ============================================================
AudioFileSourceHTTPStream* audioSource  = nullptr;
AudioFileSourceBuffer*     audioBuffer  = nullptr;
AudioGeneratorMP3*         audioMP3     = nullptr;
AudioOutputI2S*            audioOutput  = nullptr;

// ============================================================
//  ÉTAT
// ============================================================
int   currentStation  = 0;
int   listScrollTop   = 0;
bool  isPlaying       = false;
int   volume          = 128;   // 0-255, échelle M5Unified Speaker
bool  isMuted         = false;

String nowPlaying = "";
String nowArtist  = "";
String statusMsg  = "Appuie ENTER pour jouer";

unsigned long lastMetaFetch  = 0;
const long    META_INTERVAL  = 15000;
int   tickerOffset   = 0;
unsigned long lastTickerMove = 0;

bool  showHelp = false;

unsigned long lastLoopOK   = 0;
const long    LOOP_TIMEOUT = 8000;

// ============================================================
//  COULEURS
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
#define COL_KEY_BG  M5.Lcd.color565(0,   50, 100)

// ============================================================
//  LAYOUT (240 x 135)
// ============================================================
#define W             240
#define H             135
#define HEADER_H       18
#define META_H         20
#define VOLBAR_H       11
#define LIST_Y        (HEADER_H + META_H + VOLBAR_H)
#define LIST_ITEM_H    15
#define HINT_H         14
#define LIST_H        (H - LIST_Y - HINT_H)
#define VISIBLE_ITEMS (LIST_H / LIST_ITEM_H)
#define HINT_Y        (H - HINT_H)

// ============================================================
//  VOLUME — ES8311 via M5Unified Speaker
//  L'astuce : configurer le Speaker AVANT de lancer l'I2S
//  et ne JAMAIS appeler SetGain() (reste à 1.0f neutre)
// ============================================================
void applyVolume() {
    uint8_t v = isMuted ? 0 : (uint8_t)volume;
    M5Cardputer.Speaker.setVolume(v);
}

// ============================================================
//  AUDIO — stop propre
// ============================================================
void stopStream() {
    isPlaying = false;
    if (audioMP3)    { audioMP3->stop();   delete audioMP3;    audioMP3    = nullptr; }
    if (audioBuffer) {                     delete audioBuffer; audioBuffer = nullptr; }
    if (audioSource) {                     delete audioSource; audioSource = nullptr; }
    delay(120);
    if (audioOutput) { audioOutput->stop(); delete audioOutput; audioOutput = nullptr; }
    M5Cardputer.Speaker.end();
    delay(120);
    statusMsg  = "Arrete";
    nowPlaying = "";
    nowArtist  = "";
    tickerOffset = 0;
}

// ============================================================
//  AUDIO — démarrage
//  Ordre critique :
//    1) Speaker.config() + Speaker.begin() → réveille ES8311
//    2) Speaker.setVolume()                → niveau audio
//    3) AudioOutputI2S + SetGain(1.0f)     → I2S neutre
//    4) connecttohost                      → lance le stream
// ============================================================
void startStream() {
    stopStream();
    statusMsg = "Connexion...";
    drawUI();

    // Étape 1 : configurer et démarrer le codec ES8311
    auto spkcfg = M5Cardputer.Speaker.config();
    spkcfg.sample_rate   = 44100;
    spkcfg.task_priority = 2;
    M5Cardputer.Speaker.config(spkcfg);
    M5Cardputer.Speaker.begin();

    // Étape 2 : appliquer le volume AU CODEC avant l'I2S
    applyVolume();
    delay(80);

    // Étape 3 : init I2S en mode externe, gain NEUTRE
    audioOutput = new AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S);
    audioOutput->SetPinout(I2S_BCLK, I2S_LRCLK, I2S_DOUT);
    audioOutput->SetGain(1.0f);   // NE PAS CHANGER — volume géré par ES8311

    // Étape 4 : stream HTTP
    audioSource = new AudioFileSourceHTTPStream(STATIONS[currentStation].url);
    audioBuffer = new AudioFileSourceBuffer(audioSource, 16384);
    audioMP3    = new AudioGeneratorMP3();

    if (audioMP3->begin(audioBuffer, audioOutput)) {
        isPlaying    = true;
        statusMsg    = "En lecture";
        nowPlaying   = STATIONS[currentStation].name;
        tickerOffset = 0;
        lastLoopOK   = millis();
        fetchMetadata();
    } else {
        statusMsg = "Erreur stream!";
        stopStream();
    }
    drawUI();
}

// ============================================================
//  METADATA
// ============================================================
void fetchMetadata() {
    if (strlen(STATIONS[currentStation].metaUrl) == 0) return;
    if (WiFi.status() != WL_CONNECTED) return;
    HTTPClient http;
    http.begin(STATIONS[currentStation].metaUrl);
    http.addHeader("User-Agent", "JeffsWebRadio/1.8");
    http.setTimeout(5000);
    if (http.GET() == 200) {
        JsonDocument doc;
        if (!deserializeJson(doc, http.getString())) {
            String sname = String(STATIONS[currentStation].name);
            if (currentStation >= 2 && currentStation <= 4) {  // Radio Paradise
                nowPlaying = doc["title"]  | sname;
                nowArtist  = doc["artist"] | String("");
            } else if (currentStation == 1) {  // FIP
                JsonObject now = doc["now"];
                nowPlaying = now["firstLine"]["title"]  | sname;
                nowArtist  = now["secondLine"]["title"] | String("");
            } else {
                nowPlaying = doc["title"]  | sname;
                nowArtist  = doc["artist"] | String("");
            }
            tickerOffset = 0;
        }
    }
    http.end();
    lastMetaFetch = millis();
}

// ============================================================
//  UI — HEADER
// ============================================================
void drawHeader() {
    M5.Lcd.fillRect(0, 0, W, HEADER_H, COL_HDR_BG);
    M5.Lcd.setTextFont(1); M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COL_HDR_FG, COL_HDR_BG);
    M5.Lcd.drawString(">> JEFF'S WEB RADIO v1.8", 4, 5);
    bool wok = (WiFi.status() == WL_CONNECTED);
    M5.Lcd.setTextColor(wok ? COL_PLAY : COL_STOP, COL_HDR_BG);
    M5.Lcd.drawString(wok ? "WiFi" : "NoWi", W - 30, 5);
}

// ============================================================
//  UI — BANDE META (titre défilant)
// ============================================================
void drawMetaBand() {
    M5.Lcd.fillRect(0, HEADER_H, W, META_H, COL_META_BG);
    M5.Lcd.setTextFont(1); M5.Lcd.setTextSize(1);

    String full = (nowArtist.length() > 0)
                  ? nowArtist + " - " + nowPlaying
                  : (nowPlaying.length() > 0 ? nowPlaying : statusMsg);

    const int MAX_VIS = 28;
    String display = full;
    if ((int)full.length() > MAX_VIS) {
        String loop = full + "   " + full;
        if (tickerOffset >= (int)(full.length() + 3)) tickerOffset = 0;
        display = loop.substring(tickerOffset, tickerOffset + MAX_VIS);
    }

    if (isPlaying) {
        M5.Lcd.setTextColor(COL_PLAY, COL_META_BG);
        M5.Lcd.drawString((millis() / 600) % 2 == 0 ? ">" : " ", 3, HEADER_H + 5);
        M5.Lcd.setTextColor(COL_META_FG, COL_META_BG);
    } else {
        M5.Lcd.setTextColor(COL_DIM, COL_META_BG);
    }
    M5.Lcd.drawString(display, 14, HEADER_H + 5);
}

// ============================================================
//  UI — BARRE DE VOLUME avec jauge graphique
// ============================================================
void drawVolBar() {
    int y = HEADER_H + META_H;
    M5.Lcd.fillRect(0, y, W, VOLBAR_H, COL_VOL_BG);
    M5.Lcd.setTextFont(1); M5.Lcd.setTextSize(1);

    if (isMuted) {
        M5.Lcd.setTextColor(COL_STOP, COL_VOL_BG);
        M5.Lcd.drawString("MUTE", 4, y + 2);
        M5.Lcd.setTextColor(COL_DIM, COL_VOL_BG);
        M5.Lcd.drawString("[M] pour reactiver", 42, y + 2);
    } else {
        // Label "Vol"
        M5.Lcd.setTextColor(COL_VOL_FG, COL_VOL_BG);
        M5.Lcd.drawString("Vol", 3, y + 2);

        // Jauge graphique
        int bx = 23, bw = 150, bh = 7, bby = y + 2;
        int filled = (volume * bw) / 255;
        M5.Lcd.fillRect(bx, bby, bw, bh, COL_BAR_BG);
        if (filled > 0) M5.Lcd.fillRect(bx, bby, filled, bh, COL_VOL_FG);

        // Valeur en %
        int pct = (volume * 100) / 255;
        M5.Lcd.setTextColor(COL_TEXT, COL_VOL_BG);
        String vs = String(pct) + "%";
        M5.Lcd.drawString(vs, 178, y + 2);

        // Touches rapides
        M5.Lcd.setTextColor(COL_DIM, COL_VOL_BG);
        M5.Lcd.drawString("[+][-]", W - 40, y + 2);
    }
}

// ============================================================
//  UI — LISTE STATIONS
// ============================================================
void drawStationList() {
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
            M5.Lcd.fillRect(0, y, 3, LIST_ITEM_H, COL_SEL_FG);
            M5.Lcd.setTextColor(COL_SEL_FG, COL_SEL_BG);
        } else {
            M5.Lcd.setTextColor(COL_DIM, COL_BG);
        }
        M5.Lcd.setTextFont(1); M5.Lcd.setTextSize(1);
        M5.Lcd.drawString(String(idx + 1) + ". " + STATIONS[idx].name, 7, y + 3);

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
//  UI — BARRE D'AIDE CONTEXTUELLE (bas d'écran)
// ============================================================
void drawHintBar() {
    M5.Lcd.fillRect(0, HINT_Y, W, HINT_H, COL_BAR_BG);
    M5.Lcd.setTextFont(1); M5.Lcd.setTextSize(1);

    auto key = [](int x, const char* k) {
        M5.Lcd.fillRect(x, HINT_Y + 1, 6 * strlen(k) + 2, HINT_H - 2, COL_KEY_BG);
        M5.Lcd.setTextColor(COL_KEY, COL_KEY_BG);
        M5.Lcd.drawString(k, x + 1, HINT_Y + 3);
        return x + 6 * strlen(k) + 5;
    };
    auto lbl = [](int x, const char* t) {
        M5.Lcd.setTextColor(COL_DIM, COL_BAR_BG);
        M5.Lcd.drawString(t, x, HINT_Y + 3);
        return x + 6 * strlen(t);
    };

    if (!isPlaying) {
        int x = 3;
        x = key(x, "ENTER"); x = lbl(x, ":play  ");
        x = key(x, "N");     x = lbl(x, "/");
        x = key(x, "P");     x = lbl(x, ":station  ");
        x = key(x, "?");     lbl(x, ":aide");
    } else {
        int x = 3;
        x = key(x, "ENTER"); x = lbl(x, ":stop  ");
        x = key(x, "+");     x = lbl(x, "/");
        x = key(x, "-");     x = lbl(x, ":vol  ");
        x = key(x, "M");     x = lbl(x, ":mute  ");
        x = key(x, "?");     lbl(x, ":aide");
    }
}

// ============================================================
//  UI — ÉCRAN D'AIDE COMPLET
// ============================================================
void drawHelpScreen() {
    M5.Lcd.fillScreen(COL_HELP_BG);
    M5.Lcd.setTextFont(1); M5.Lcd.setTextSize(1);

    // Titre
    M5.Lcd.fillRect(0, 0, W, 14, COL_HDR_BG);
    M5.Lcd.setTextColor(COL_HDR_FG, COL_HDR_BG);
    M5.Lcd.drawString("  AIDE — JEFF'S WEB RADIO", 4, 3);

    struct { const char* k; const char* d; } lines[] = {
        { "ENTER",  "Lancer ou arreter la radio" },
        { "N",      "Station suivante" },
        { "P",      "Station precedente" },
        { "+  /  =","Volume +  (par paliers de 13)" },
        { "-",      "Volume -  (par paliers de 13)" },
        { "M",      "Mute / Unmute" },
        { "R",      "Recharger les infos du morceau" },
        { "S",      "Stop force" },
        { "?",      "Afficher / fermer cette aide" },
    };
    int n = sizeof(lines) / sizeof(lines[0]);

    for (int i = 0; i < n; i++) {
        int y = 17 + i * 13;
        uint16_t bg = (i % 2 == 0) ? COL_HELP_BG : M5.Lcd.color565(5, 30, 55);
        M5.Lcd.fillRect(0, y, W, 13, bg);
        // Touche
        M5.Lcd.fillRect(2, y + 1, 54, 11, COL_KEY_BG);
        M5.Lcd.setTextColor(COL_KEY, COL_KEY_BG);
        M5.Lcd.drawString(lines[i].k, 4, y + 2);
        // Description
        M5.Lcd.setTextColor(COL_TEXT, bg);
        M5.Lcd.drawString(lines[i].d, 60, y + 2);
    }

    // Footer
    M5.Lcd.fillRect(0, H - 12, W, 12, COL_BAR_BG);
    M5.Lcd.setTextColor(COL_DIM, COL_BAR_BG);
    M5.Lcd.drawString("Volume 0-255  |  Appuie ? pour fermer", 4, H - 10);
}

// ============================================================
//  UI — COMPLET
// ============================================================
void drawUI() {
    if (showHelp) { drawHelpScreen(); return; }
    drawHeader();
    drawMetaBand();
    drawVolBar();
    drawStationList();
    drawHintBar();
}

// ============================================================
//  WIFI
// ============================================================
void startWiFiPortal() {
    M5.Lcd.fillScreen(COL_BG);
    M5.Lcd.setTextFont(1); M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COL_HDR_FG, COL_BG); M5.Lcd.setTextSize(2);
    M5.Lcd.drawString("Jeff's", 62, 12);
    M5.Lcd.drawString("Web Radio", 38, 38);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COL_SEL_FG, COL_BG);
    M5.Lcd.drawString("v1.8  Cardputer ADV", 40, 68);
    M5.Lcd.setTextColor(COL_DIM, COL_BG);
    M5.Lcd.drawString("WiFi: JeffsRadio-Setup", 28, 84);
    M5.Lcd.drawString("puis 192.168.4.1", 52, 98);

    WiFiManager wm;
    wm.setAPStaticIPConfig(
        IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
    wm.setConfigPortalTimeout(180);
    bool ok = wm.autoConnect("JeffsRadio-Setup");

    M5.Lcd.fillScreen(COL_BG);
    M5.Lcd.setTextSize(1);
    if (ok) {
        M5.Lcd.setTextColor(COL_PLAY, COL_BG);
        M5.Lcd.drawString("WiFi connecte!", 32, 55);
        M5.Lcd.setTextColor(COL_TEXT, COL_BG);
        M5.Lcd.drawString(WiFi.localIP().toString(), 60, 75);
        delay(1200);
    } else {
        M5.Lcd.setTextColor(COL_STOP, COL_BG);
        M5.Lcd.drawString("WiFi non configure", 25, 55);
        delay(2500);
    }
}

// ============================================================
//  CLAVIER
// ============================================================
void handleKeyboard() {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
    Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();

    if (st.enter) {
        showHelp = false;
        if (isPlaying) stopStream(); else startStream();
        drawUI(); return;
    }

    for (auto key : st.word) {
        switch (key) {
            case 'n': case 'N':
                if (isPlaying) stopStream();
                currentStation = (currentStation + 1) % NUM_STATIONS;
                drawStationList(); drawHintBar(); break;

            case 'p': case 'P':
                if (isPlaying) stopStream();
                currentStation = (currentStation - 1 + NUM_STATIONS) % NUM_STATIONS;
                drawStationList(); drawHintBar(); break;

            case '+': case '=':
                isMuted = false;
                volume  = min(255, volume + 13);
                applyVolume(); drawVolBar(); drawHintBar(); break;

            case '-':
                isMuted = false;
                volume  = max(0, volume - 13);
                applyVolume(); drawVolBar(); drawHintBar(); break;

            case 'm': case 'M':
                isMuted = !isMuted;
                applyVolume(); drawVolBar(); drawHintBar(); break;

            case 's': case 'S':
                if (isPlaying) { stopStream(); drawUI(); } break;

            case 'r': case 'R':
                if (isPlaying) { fetchMetadata(); drawMetaBand(); } break;

            case '?':
                showHelp = !showHelp;
                drawUI(); break;
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

    // Splash
    M5.Lcd.setTextColor(COL_HDR_FG, COL_BG); M5.Lcd.setTextSize(2);
    M5.Lcd.drawString("Jeff's", 62, 15);
    M5.Lcd.drawString("Web Radio", 38, 42);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COL_DIM, COL_BG);
    M5.Lcd.drawString("v1.8  Cardputer ADV", 40, 80);
    M5.Lcd.setTextColor(COL_SEL_FG, COL_BG);
    M5.Lcd.drawString("by Jeff  (powered by Claude)", 18, 98);
    delay(1800);

    startWiFiPortal();
    drawUI();
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
    M5Cardputer.update();

    if (isPlaying && audioMP3 && audioMP3->isRunning()) {
        if (!audioMP3->loop()) {
            // Stream terminé → reconnexion
            statusMsg = "Reconnexion...";
            drawMetaBand();
            delay(2000);
            startStream();
        } else {
            lastLoopOK = millis();
        }

        // Watchdog 8s
        if (millis() - lastLoopOK > LOOP_TIMEOUT) {
            statusMsg = "Timeout - reset";
            drawMetaBand();
            stopStream();
            delay(500);
            startStream();
        }
    }

    // Ticker défilant
    if (isPlaying && millis() - lastTickerMove > 350) {
        tickerOffset++;
        drawMetaBand();
        lastTickerMove = millis();
    }

    // Metadata périodique
    if (isPlaying && millis() - lastMetaFetch > META_INTERVAL) {
        fetchMetadata();
        drawMetaBand();
    }

    handleKeyboard();
    delay(8);
}
