// ============================================================
//  JEFF'S WEB RADIO — v1.6
//  Pour M5Stack Cardputer ADV (ESP32-S3)
//  Corrections v1.6 :
//    - VU-mètre SUPPRIMÉ (libère CPU + évite les glitchs)
//    - Volume : M5Unified Speaker.setVolume() SEUL pilote le codec
//              SetGain() ESP8266Audio fixé à 1.0f (neutre)
//              → plus de cumul de gain → plus de saturation
//    - Nostalgie : URL cdn.nrjaudio.fm HTTP confirmée ESP32
//    - Sud Radio  : URL infomaniak HTTP confirmée
//    - Watchdog : timeout réduit à 5s, reset complet si bloqué
//    - stopStream() : double délai DMA conservé (anti-bruit)
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
//  CONFIGURATION STATIONS
// ============================================================
struct Station {
    const char* name;
    const char* url;
    const char* metaUrl;
};

Station STATIONS[] = {
    { "Nostalgie",       "http://cdn.nrjaudio.fm/audio1/fr/30601/mp3_128.mp3",                "" },
    { "FIP",             "http://icecast.radiofrance.fr/fip-midfi.mp3",                       "https://api.radiofrance.fr/livemeta/pull/7" },
    { "Sud Radio",       "http://sudradio.ice.infomaniak.ch/sudradio-high.mp3",               "" },
    { "RP Main Mix",     "http://stream.radioparadise.com/mp3-128",                           "https://api.radioparadise.com/api/now_playing?block_id=0&format=json" },
    { "RP Mellow Mix",   "http://stream.radioparadise.com/mellow-128",                        "https://api.radioparadise.com/api/now_playing?block_id=1&format=json" },
    { "RP Rock Mix",     "http://stream.radioparadise.com/rock-128",                          "https://api.radioparadise.com/api/now_playing?block_id=2&format=json" },
    { "SomaFM Groove",   "http://ice.somafm.com/groovesalad-128-mp3",                         "" },
    { "SomaFM Space",    "http://ice.somafm.com/spacestation-128-mp3",                        "" },
    { "SomaFM 80s",      "http://ice.somafm.com/u80s-128-mp3",                                "" },
    { "Nightride FM",    "https://stream.nightride.fm/nightride.mp3",                         "" },
    { "Lofi Hip Hop",    "http://stream.zeno.fm/f3wvbbqmdg8uv",                               "" },
    { "BluesWave Athens","http://blueswave.radio:8000/blueswave",                             "" },
    { "Blues Radio",     "http://198.58.98.83/stream/1/",                                     "" },
    { "101 Smooth Jazz", "http://strm112.1.fm/smoothjazz_mobile_mp3",                         "" },
    { "Smooth Jazz DLX", "http://agnes.torontocast.com:8142/stream",                          "" },
    { "Chillout Lounge", "http://strm112.1.fm/chilloutlounge_mobile_mp3",                     "" },
    { "Sensual Lounge",  "http://agnes.torontocast.com:8146/stream",                          "" },
};

const int NUM_STATIONS = sizeof(STATIONS) / sizeof(STATIONS[0]);

// ============================================================
//  AUDIO ENGINE
// ============================================================
AudioFileSourceHTTPStream* audioSource  = nullptr;
AudioFileSourceBuffer*     audioBuffer  = nullptr;
AudioGeneratorMP3*         audioMP3     = nullptr;
AudioOutputI2S*            audioOutput  = nullptr;

// Pins I2S Cardputer ADV (codec ES8311 / Stamp-S3A)
#define I2S_BCLK   41
#define I2S_LRCLK  43
#define I2S_DOUT   42

// ============================================================
//  ÉTAT APPLICATION
// ============================================================
int   currentStation = 0;
int   listScrollTop  = 0;
bool  isPlaying      = false;
int   volume         = 75;   // 0-100, contrôle M5Unified uniquement
bool  isMuted        = false;

String nowPlaying    = "";
String nowArtist     = "";
String statusMsg     = "Pret";

unsigned long lastMetaFetch  = 0;
const long    META_INTERVAL  = 15000;
int   tickerOffset   = 0;
unsigned long lastTickerMove = 0;

// Watchdog logiciel : 5s sans loop OK → reconnexion
unsigned long lastLoopOK    = 0;
const long    LOOP_TIMEOUT  = 5000;

// ============================================================
//  COULEURS
// ============================================================
#define COL_BG      M5.Lcd.color565(8,   8,  18)
#define COL_HDR_BG  M5.Lcd.color565(0,   40,  80)
#define COL_HDR_FG  M5.Lcd.color565(0,  180, 255)
#define COL_SEL_BG  M5.Lcd.color565(20,  50,  90)
#define COL_SEL_FG  M5.Lcd.color565(255, 200,   0)
#define COL_TEXT    M5.Lcd.color565(220, 220, 220)
#define COL_MUTED   M5.Lcd.color565(90,  90,  90)
#define COL_PLAY    M5.Lcd.color565(0,  255, 120)
#define COL_STOP    M5.Lcd.color565(255,  60,  60)
#define COL_META_BG M5.Lcd.color565(5,   25,  45)
#define COL_META_FG M5.Lcd.color565(180, 230, 255)
#define COL_SCROLL  M5.Lcd.color565(60,  60, 100)
#define COL_BAR_BG  M5.Lcd.color565(15,  15,  35)

// ============================================================
//  LAYOUT — sans VU-mètre
// ============================================================
#define W             240
#define H             135
#define HEADER_H       18
#define META_H         22
#define LIST_Y        (HEADER_H + META_H)
#define LIST_ITEM_H    15
#define STATUSBAR_H    14
#define LIST_H        (H - LIST_Y - STATUSBAR_H)
#define VISIBLE_ITEMS (LIST_H / LIST_ITEM_H)
#define STATUS_Y      (H - STATUSBAR_H)

// ============================================================
//  WIFI
// ============================================================
void startWiFiPortal() {
    M5.Lcd.fillScreen(COL_BG);
    M5.Lcd.setTextColor(COL_HDR_FG, COL_BG); M5.Lcd.setTextSize(1);
    M5.Lcd.drawString("JEFF'S WEB RADIO", 10, 10);
    M5.Lcd.setTextColor(COL_SEL_FG, COL_BG);
    M5.Lcd.drawString("WiFi: JeffsRadio-Setup", 10, 50);
    M5.Lcd.setTextColor(COL_TEXT, COL_BG);
    M5.Lcd.drawString("Puis 192.168.4.1", 10, 70);

    WiFiManager wm;
    wm.setAPStaticIPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
    wm.setConfigPortalTimeout(180);
    bool ok = wm.autoConnect("JeffsRadio-Setup");

    M5.Lcd.fillScreen(COL_BG);
    if (ok) {
        M5.Lcd.setTextColor(COL_PLAY, COL_BG);
        M5.Lcd.drawString("WiFi connecte!", 10, 50);
        M5.Lcd.setTextColor(COL_TEXT, COL_BG);
        M5.Lcd.drawString(WiFi.localIP().toString(), 10, 70);
        delay(1500);
    } else {
        M5.Lcd.setTextColor(COL_STOP, COL_BG);
        M5.Lcd.drawString("WiFi non configure", 10, 50);
        delay(3000);
    }
}

// ============================================================
//  VOLUME — v1.6 : M5Unified SEUL, SetGain fixé à 1.0f
//  L'ES8311 est contrôlé via Speaker.setVolume(0-255).
//  SetGain(1.0f) = neutre, pas d'amplification logicielle.
// ============================================================
void applyVolume() {
    uint8_t spkVol = isMuted ? 0 : (uint8_t)(volume * 255 / 100);
    M5Cardputer.Speaker.setVolume(spkVol);
    // SetGain reste toujours à 1.0f — neutre, jamais modifié
}

// ============================================================
//  AUDIO — stopStream propre (anti-bruit v1.5 conservé)
// ============================================================
void stopStream() {
    isPlaying = false;

    if (audioMP3)    { audioMP3->stop(); delete audioMP3;    audioMP3    = nullptr; }
    if (audioBuffer) {                   delete audioBuffer; audioBuffer = nullptr; }
    if (audioSource) {                   delete audioSource; audioSource = nullptr; }

    delay(150);  // laisse le DMA I2S se vider

    if (audioOutput) { delete audioOutput; audioOutput = nullptr; }

    M5Cardputer.Speaker.end();
    delay(150);  // attend que l'ES8311 soit stable

    statusMsg  = "Arrete";
    nowPlaying = "";
    nowArtist  = "";
    tickerOffset = 0;
}

void startStream() {
    stopStream();
    statusMsg = "Connexion...";
    drawUI();

    // 1) Réveille le codec ES8311
    auto spkcfg = M5Cardputer.Speaker.config();
    spkcfg.sample_rate   = 44100;
    spkcfg.task_priority = 2;
    M5Cardputer.Speaker.config(spkcfg);
    M5Cardputer.Speaker.begin();
    applyVolume();
    delay(100);

    // 2) Init I2S — SetGain FIXÉ à 1.0f (neutre)
    audioOutput = new AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S);
    audioOutput->SetPinout(I2S_BCLK, I2S_LRCLK, I2S_DOUT);
    audioOutput->SetGain(1.0f);  // TOUJOURS 1.0, volume géré par ES8311

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
    http.addHeader("User-Agent", "JeffsWebRadio/1.6");
    if (http.GET() == 200) {
        JsonDocument doc;
        if (!deserializeJson(doc, http.getString())) {
            if (currentStation >= 3 && currentStation <= 5) {
                nowPlaying = doc["title"]  | String(STATIONS[currentStation].name);
                nowArtist  = doc["artist"] | String("");
            } else if (currentStation == 1) {
                JsonObject now = doc["now"];
                nowPlaying = now["firstLine"]["title"]  | String(STATIONS[currentStation].name);
                nowArtist  = now["secondLine"]["title"] | String("");
            } else {
                nowPlaying = doc["title"]  | String(STATIONS[currentStation].name);
                nowArtist  = doc["artist"] | String("");
            }
            tickerOffset = 0;
        }
    }
    http.end();
    lastMetaFetch = millis();
}

// ============================================================
//  DESSIN UI
// ============================================================
void drawHeader() {
    M5.Lcd.fillRect(0, 0, W, HEADER_H, COL_HDR_BG);
    M5.Lcd.setTextColor(COL_HDR_FG, COL_HDR_BG); M5.Lcd.setTextSize(1);
    M5.Lcd.drawString(">> JEFF'S WEB RADIO", 4, 5);
    M5.Lcd.setTextColor(WiFi.status() == WL_CONNECTED ? COL_PLAY : COL_STOP, COL_HDR_BG);
    M5.Lcd.drawString(WiFi.status() == WL_CONNECTED ? "WiFi" : "NoWi", W - 30, 5);
}

void drawMetaBand() {
    M5.Lcd.fillRect(0, HEADER_H, W, META_H, COL_META_BG);
    String full = (nowArtist.length() > 0) ? nowArtist + " - " + nowPlaying
                : (nowPlaying.length() > 0) ? nowPlaying : statusMsg;
    const int MAX_VIS = 28;
    String display = full;
    if ((int)full.length() > MAX_VIS) {
        String loop = full + "   " + full;
        if (tickerOffset >= (int)(full.length() + 3)) tickerOffset = 0;
        display = loop.substring(tickerOffset, tickerOffset + MAX_VIS);
    }
    M5.Lcd.setTextSize(1);
    if (isPlaying) {
        M5.Lcd.setTextColor(COL_PLAY, COL_META_BG);
        M5.Lcd.drawString("*", 4, HEADER_H + 7);
        M5.Lcd.setTextColor(COL_META_FG, COL_META_BG);
    } else {
        M5.Lcd.setTextColor(COL_MUTED, COL_META_BG);
    }
    M5.Lcd.drawString(display, 16, HEADER_H + 7);
}

void drawStationList() {
    if (currentStation < listScrollTop) listScrollTop = currentStation;
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
            M5.Lcd.setTextColor(COL_MUTED, COL_BG);
        }
        M5.Lcd.setTextSize(1);
        M5.Lcd.drawString(String(idx + 1) + ". " + STATIONS[idx].name, 8, y + 3);

        if (sel && isPlaying) {
            M5.Lcd.fillRect(W - 52, y + 1, 48, LIST_ITEM_H - 2, COL_PLAY);
            M5.Lcd.setTextColor(COL_BG, COL_PLAY);
            M5.Lcd.drawString("ON AIR", W - 48, y + 3);
        }
    }

    // Scrollbar
    if (NUM_STATIONS > VISIBLE_ITEMS) {
        int bH = max(3, LIST_H * VISIBLE_ITEMS / NUM_STATIONS);
        int bY = LIST_Y + LIST_H * listScrollTop / NUM_STATIONS;
        M5.Lcd.fillRect(W - 3, LIST_Y, 3, LIST_H, COL_BAR_BG);
        M5.Lcd.fillRect(W - 3, bY, 3, bH, COL_SCROLL);
    }
}

void drawStatusBar() {
    M5.Lcd.fillRect(0, STATUS_Y, W, STATUSBAR_H, COL_BAR_BG);
    M5.Lcd.setTextSize(1);
    // Volume à gauche
    String volStr = isMuted ? "MUTE" : "Vol:" + String(volume) + "%";
    M5.Lcd.setTextColor(isMuted ? COL_STOP : COL_TEXT, COL_BAR_BG);
    M5.Lcd.drawString(volStr, 4, STATUS_Y + 3);
    // Aide à droite
    M5.Lcd.setTextColor(COL_SCROLL, COL_BAR_BG);
    M5.Lcd.drawString("N/P ENT +/- M", W - 82, STATUS_Y + 3);
}

void drawUI() {
    drawHeader();
    drawMetaBand();
    drawStationList();
    drawStatusBar();
}

// ============================================================
//  CLAVIER
// ============================================================
void handleKeyboard() {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

    if (status.enter) {
        if (isPlaying) stopStream(); else startStream();
        drawUI(); return;
    }

    for (auto key : status.word) {
        switch (key) {
            case 'n': case 'N':
                if (isPlaying) stopStream();
                currentStation = (currentStation + 1) % NUM_STATIONS;
                drawStationList(); break;
            case 'p': case 'P':
                if (isPlaying) stopStream();
                currentStation = (currentStation - 1 + NUM_STATIONS) % NUM_STATIONS;
                drawStationList(); break;
            case '+': case '=':
                volume = min(100, volume + 5);
                applyVolume(); drawStatusBar(); break;
            case '-':
                volume = max(0, volume - 5);
                applyVolume(); drawStatusBar(); break;
            case 'm': case 'M':
                isMuted = !isMuted;
                applyVolume(); drawStatusBar(); break;
            case 's': case 'S':
                if (isPlaying) { stopStream(); drawUI(); } break;
            case 'r': case 'R':
                if (isPlaying) { fetchMetadata(); drawMetaBand(); } break;
        }
    }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg);
    M5.Lcd.setRotation(1);
    M5.Lcd.fillScreen(COL_BG);
    M5.Lcd.setTextFont(1);

    M5.Lcd.setTextColor(COL_HDR_FG, COL_BG); M5.Lcd.setTextSize(2);
    M5.Lcd.drawString("Jeff's", 60, 20);
    M5.Lcd.drawString("Web Radio", 40, 45);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COL_MUTED, COL_BG);
    M5.Lcd.drawString("v1.6 - Cardputer ADV", 35, 80);
    M5.Lcd.setTextColor(COL_SEL_FG, COL_BG);
    M5.Lcd.drawString("by Jeff  (powered by Claude)", 20, 100);
    delay(2000);

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
            statusMsg = "Reconnexion...";
            drawMetaBand();
            delay(2000);
            startStream();
        } else {
            lastLoopOK = millis();
        }

        // Watchdog 5s
        if (millis() - lastLoopOK > LOOP_TIMEOUT) {
            statusMsg = "Timeout - reset";
            drawMetaBand();
            stopStream();
            delay(500);
            startStream();
        }
    }

    if (isPlaying && millis() - lastTickerMove > 350) {
        tickerOffset++; drawMetaBand(); lastTickerMove = millis();
    }

    if (isPlaying && millis() - lastMetaFetch > META_INTERVAL) {
        fetchMetadata(); drawMetaBand();
    }

    handleKeyboard();
    delay(10);
}
