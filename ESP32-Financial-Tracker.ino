#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h> 
#include <DNSServer.h>
#include <WebServer.h> 
#include <WiFiManager.h> 
#include <Preferences.h> 
#include <esp_task_wdt.h>

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ==========================================
// 🔑 API KEY
// ==========================================
const char* apiKey = "BURAYA_KENDI_API_KEYINIZI_YAZIN";
// --- WDT AYARI ---
#define WDT_TIMEOUT 60 

// --- DONANIM AYARLARI ---
#define BUZZER_PIN  13 
#define BUZZER_CH   0  

// --- LED PİNLERİ ---
#define LED_RED     25
#define LED_YELLOW  26 
#define LED_GREEN   27 

// --- EKRAN AYARLARI ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- TUŞLAR ---
#define BTN_UP      18
#define BTN_DOWN    19
#define BTN_MODE    5  
#define BTN_ACTION  4  

// --- WEB SUNUCU ---
WebServer server(80); 

// --- KATEGORİLER ---
enum Category { CAT_WALLET, CAT_EMTIA, CAT_DOVIZ, CAT_BORSA, CAT_KRIPTO };
Category currentCategory = CAT_WALLET;

struct MarketItem {
  String name; String price; String rate; float myQuantity; 
};
struct WalletItem {
  String name; String valueTL; String quantityStr; 
};

// --- LİSTELER ---
const char* hedefHisseler[] = {"BIST 100", "XU100", "THYAO", "ASELS", "TUPRS", "ALTNY", "BIMAS", "EUPWR", "SASA", "HEKTS", "KCHOL", "GARAN", "AKBNK", "EREGL", "SISE", "ASTOR"};
const int hisseSayisi = 16;

const char* hedefKripto[] = {"BTC", "ETH", "BNB", "XRP", "SOL", "AVAX", "DOGE", "SHIB", "HBAR", "ADA", "DOT", "MATIC", "TRX", "LTC", "LINK"};
const int kriptoSayisi = 15;

const char* hedefDoviz[] = {"USD", "EUR", "GBP", "CHF", "JPY", "CAD", "RUB"}; 
const int dovizSayisi = 7;
const char* hedefEmtia[] = {"Gram Altın", "Çeyrek Altın", "Ons Altın", "Gümüş", "Platin", "Paladyum", "Brent Petrol", "Bakır"}; 
const int emtiaSayisi = 8;

// --- LINKLER ---
const char* linkGold = "https://api.collectapi.com/economy/goldPrice";
const char* linkCurrency = "https://api.collectapi.com/economy/allCurrency";
const char* linkCrypto = "https://api.collectapi.com/economy/cripto"; 
const char* linkBorsa = "https://api.collectapi.com/economy/liveBorsa";

// --- VERİ SAKLAMA ---
MarketItem dataEmtia[15];
MarketItem dataDoviz[10];
MarketItem dataBorsa[20];
MarketItem dataKripto[20]; 
WalletItem dataWallet[20];

int countEmtia = 0; int countDoviz = 0; int countBorsa = 0; int countKripto = 0; int countWallet = 0;
int itemIndex = 0; int scrollIndex = 0;
double globalTotalWealth = 0.0;
double lastSavedWealth = 0.0;
String myIP = ""; 

Preferences preferences;

// --- PROTOTİPLER ---
void playTone(int freq, int duration);
void playClick();
void playSuccess();
void playFail();
void checkWealthChange(double newWealth);
String trDuzelt(String text);
String formatMoney(long val);
void kategoriGuncelle(Category cat);
void tumVerileriCek();
void ekraniCiz();
void configModeCallback(WiFiManager *myWiFiManager);
void drawBigTrendArrow(int x, int y, bool isUp);
float parseSmartPrice(String fiyatStr); 
void calculateWallet();
void openEditor(MarketItem &item, String keyName);
void resetWalletMenu();
void drawLoadingScreen(int percent, String stepName);
String extractValue(String data, String key); 
void setLeds(bool red, bool green, bool yellow);
void updateLedForRate(String rateStr);
void handleRoot(); 
void handleSave(); 
String generateHtml(); 

// ==========================================
// SETUP
// ==========================================
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  Serial.begin(115200);

  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);

  ledcSetup(BUZZER_CH, 2000, 8);
  ledcAttachPin(BUZZER_PIN, BUZZER_CH);

  // LED SETUP
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  
  setLeds(1,0,0); delay(200); setLeds(0,0,1); delay(200); setLeds(0,1,0); delay(200); setLeds(0,0,0); 

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_ACTION, INPUT_PULLUP);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { for(;;); }
  
  playTone(1000, 100); delay(50); playTone(2000, 100);
  esp_task_wdt_reset();

  WiFiManager wm;
  wm.setAPCallback(configModeCallback); 
  wm.setConfigPortalTimeout(180); 
  if (!wm.autoConnect("ESP32_Borsa_Kurulum")) { 
    setLeds(1,0,0); delay(1000); ESP.restart(); 
  }

  // --- WEB SERVER BAŞLAT ---
  myIP = WiFi.localIP().toString();
  server.on("/", handleRoot); 
  server.on("/save", HTTP_POST, handleSave); 
  server.begin();

  // --- IP ADRESİ GÖSTERİMİ (DÜZELTİLDİ) ---
  display.clearDisplay(); 
  
  // Başlık
  display.setTextSize(1); 
  display.setTextColor(WHITE);
  display.setCursor(25, 0); 
  display.println("WEB ARAYUZU");
  display.drawLine(0, 10, 128, 10, WHITE); // Altını çiz

  // Açıklama
  display.setCursor(0, 20); 
  display.println("Tarayicidan Girin:");

  // IP Adresi (Size 1 ile ekrana sığar)
  display.setCursor(10, 35); 
  display.print("http://");
  display.println(myIP);

  // Süre Bilgisi
  display.setCursor(20, 55); 
  display.println("(15 sn Bekle)");
  
  display.display(); 
  
  // 15 Saniye bekle (Kullanıcı IP'yi yazabilsin)
  delay(15000); 

  tumVerileriCek();
}

// ==========================================
// LOOP
// ==========================================
void loop() {
  esp_task_wdt_reset(); 
  server.handleClient(); 

  if (WiFi.status() == WL_CONNECTED) {
      if (digitalRead(BTN_MODE) == LOW) {
        unsigned long pressTime = millis();
        while(digitalRead(BTN_MODE) == LOW) { esp_task_wdt_reset(); } 
        if(millis() - pressTime > 2000) { if(currentCategory == CAT_WALLET) resetWalletMenu(); } 
        else {
            playClick(); itemIndex = 0; scrollIndex = 0;
            if (currentCategory == CAT_WALLET) currentCategory = CAT_EMTIA;
            else if (currentCategory == CAT_EMTIA) currentCategory = CAT_DOVIZ;
            else if (currentCategory == CAT_DOVIZ) currentCategory = CAT_BORSA;
            else if (currentCategory == CAT_BORSA) currentCategory = CAT_KRIPTO;
            else currentCategory = CAT_WALLET;
            ekraniCiz(); delay(200);
        }
      }
      
      if (digitalRead(BTN_DOWN) == LOW) {
        playClick(); int maxLimit = 0;
        if(currentCategory == CAT_EMTIA) maxLimit = countEmtia;
        else if(currentCategory == CAT_DOVIZ) maxLimit = countDoviz;
        else if(currentCategory == CAT_BORSA) maxLimit = countBorsa;
        else if(currentCategory == CAT_KRIPTO) maxLimit = countKripto;
        else if(currentCategory == CAT_WALLET) maxLimit = countWallet; 
        if (currentCategory == CAT_WALLET) { if (scrollIndex < countWallet - 1) { scrollIndex++; ekraniCiz(); delay(150); } } 
        else { if (itemIndex < maxLimit - 1) { itemIndex++; ekraniCiz(); delay(200); } }
      }
      
      if (digitalRead(BTN_UP) == LOW) {
         playClick();
         if (currentCategory == CAT_WALLET) { if (scrollIndex > 0) { scrollIndex--; ekraniCiz(); delay(150); } } 
         else { if (itemIndex > 0) { itemIndex--; ekraniCiz(); delay(200); } }
      }
      
      if (digitalRead(BTN_ACTION) == LOW) {
        unsigned long pressTime = millis();
        while(digitalRead(BTN_ACTION) == LOW) { esp_task_wdt_reset(); } 
        if(millis() - pressTime > 1000) { playTone(1500, 50); delay(50); playTone(1500, 50); tumVerileriCek(); } 
        else {
             playClick(); 
             if(currentCategory != CAT_WALLET) {
                 MarketItem *selectedItem;
                 if(currentCategory == CAT_EMTIA) selectedItem = &dataEmtia[itemIndex];
                 else if(currentCategory == CAT_DOVIZ) selectedItem = &dataDoviz[itemIndex];
                 else if(currentCategory == CAT_BORSA) selectedItem = &dataBorsa[itemIndex];
                 else if(currentCategory == CAT_KRIPTO) selectedItem = &dataKripto[itemIndex];
                 
                 String key = selectedItem->name; key.replace(" ", ""); key.replace(".", "");
                 if(key.length() > 15) key = key.substring(0, 15);
                 openEditor(*selectedItem, key);
             }
        }
      }
  }
}

// --- WEB SERVER FONKSİYONLARI ---

void handleRoot() {
  server.send(200, "text/html", generateHtml());
}

void handleSave() {
  preferences.begin("wallet", false); 
  
  for (int i = 0; i < server.args(); i++) {
    String argName = server.argName(i); 
    String argValue = server.arg(i);    
    
    String key = argName;
    key.replace(" ", ""); 
    key.replace(".", ""); 
    if(key.length() > 15) key = key.substring(0, 15);
    
    if (argValue.length() > 0) {
      float val = argValue.toFloat();
      preferences.putFloat(key.c_str(), val);
    }
  }
  preferences.end();
  
  calculateWallet();
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{background:#121212;color:#0f0;font-family:sans-serif;text-align:center;padding:50px;}a{color:white;text-decoration:none;border:1px solid white;padding:10px 20px;border-radius:5px;}</style></head><body><h1>KAYDEDILDI!</h1><p>Cihaz guncellendi.</p><br><a href='/'>Geri Don</a></body></html>";
  server.send(200, "text/html", html);
  playSuccess();
}

String generateHtml() {
  String html = "<html><head><title>Portfoy Yonetimi</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { background-color: #1e1e1e; color: #ffffff; font-family: Arial, sans-serif; margin: 0; padding: 20px; }";
  html += "h1 { text-align: center; color: #4CAF50; }";
  html += "h2 { border-bottom: 2px solid #333; padding-bottom: 10px; margin-top: 30px; }";
  html += ".group { background: #2d2d2d; padding: 15px; border-radius: 10px; margin-bottom: 20px; }";
  html += "label { display: block; margin: 10px 0 5px; font-size: 14px; color: #aaa; }";
  html += "input[type='number'] { width: 100%; padding: 10px; border-radius: 5px; border: 1px solid #444; background: #121212; color: white; font-size: 16px; box-sizing: border-box;}";
  html += "input[type='submit'] { width: 100%; background-color: #4CAF50; color: white; padding: 15px; border: none; border-radius: 5px; font-size: 18px; cursor: pointer; margin-top: 20px; }";
  html += "input[type='submit']:hover { background-color: #45a049; }";
  html += "</style></head><body>";
  
  html += "<h1>PORTFOY YONETIMI</h1>";
  html += "<form action='/save' method='POST'>";
  
  // EMTIA
  html += "<h2>EMTIA / ALTIN</h2><div class='group'>";
  preferences.begin("wallet", true);
  for(int i=0; i<emtiaSayisi; i++) {
     String n = String(hedefEmtia[i]);
     String key = n; key.replace(" ", ""); key.replace(".", ""); if(key.length()>15) key=key.substring(0,15);
     float val = preferences.getFloat(key.c_str(), 0);
     html += "<label>" + n + "</label><input type='number' step='0.01' name='" + n + "' value='" + String(val) + "'>";
  }
  html += "</div>";

  // DOVIZ
  html += "<h2>DOVIZ</h2><div class='group'>";
  for(int i=0; i<dovizSayisi; i++) {
     String n = String(hedefDoviz[i]);
     String label = n; 
     if(String(hedefDoviz[i])=="USD") label="ABD Dolari"; 
     else if(String(hedefDoviz[i])=="EUR") label="Euro";
     else if(String(hedefDoviz[i])=="GBP") label="Sterlin";
     
     String key = trDuzelt(label); key.replace(" ", ""); key.replace(".", ""); if(key.length()>15) key=key.substring(0,15);
     float val = preferences.getFloat(key.c_str(), 0);
     html += "<label>" + label + "</label><input type='number' step='0.01' name='" + label + "' value='" + String(val) + "'>";
  }
  html += "</div>";

  // BORSA
  html += "<h2>BORSA ISTANBUL</h2><div class='group'>";
  for(int i=0; i<hisseSayisi; i++) {
     String n = String(hedefHisseler[i]);
     String key = n; key.replace(" ", ""); key.replace(".", ""); if(key.length()>15) key=key.substring(0,15);
     float val = preferences.getFloat(key.c_str(), 0);
     html += "<label>" + n + "</label><input type='number' name='" + n + "' value='" + String(val) + "'>";
  }
  html += "</div>";

  // KRIPTO
  html += "<h2>KRIPTO PARA</h2><div class='group'>";
  for(int i=0; i<kriptoSayisi; i++) {
     String n = String(hedefKripto[i]);
     String key = n; key.replace(" ", ""); key.replace(".", ""); if(key.length()>15) key=key.substring(0,15);
     float val = preferences.getFloat(key.c_str(), 0);
     html += "<label>" + n + "</label><input type='number' step='0.0001' name='" + n + "' value='" + String(val) + "'>";
  }
  html += "</div>";
  
  preferences.end();

  html += "<input type='submit' value='KAYDET & GUNCELLE'>";
  html += "</form></body></html>";
  return html;
}

void configModeCallback(WiFiManager *myWiFiManager) {
  display.clearDisplay(); display.setTextSize(1); display.setTextColor(WHITE);
  display.setCursor(0, 0); display.println("WI-FI YOK!");
  display.setCursor(0, 20); display.println("Telefondan Baglan:");
  display.setCursor(0, 35); display.println("Ag: ESP32_Borsa_Kurulum");
  setLeds(0,0,1); display.display();
}

String extractValue(String data, String key) {
  String searchKey = "\"" + key + "\":";
  int start = data.indexOf(searchKey);
  if (start == -1) return "";
  start += searchKey.length();
  if(data.charAt(start) == '"') {
    start++; int end = data.indexOf("\"", start);
    if(end == -1) return "";
    return data.substring(start, end);
  } else {
    int end = start;
    while(end < data.length()) {
        char c = data.charAt(end);
        if (isdigit(c) || c == '.' || c == '-') end++; else break; 
    }
    return data.substring(start, end);
  }
}

void setLeds(bool red, bool green, bool yellow) {
    digitalWrite(LED_RED, red ? HIGH : LOW);
    digitalWrite(LED_GREEN, green ? HIGH : LOW);
    digitalWrite(LED_YELLOW, yellow ? HIGH : LOW);
}

void updateLedForRate(String rateStr) {
    if (rateStr.indexOf('-') != -1) setLeds(1, 0, 0); else setLeds(0, 1, 0);
}

void kategoriGuncelle(Category cat) {
  if(WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http; http.setTimeout(40000); 
    if(cat == CAT_EMTIA) http.begin(client, linkGold);
    else if(cat == CAT_DOVIZ) http.begin(client, linkCurrency);
    else if(cat == CAT_KRIPTO) http.begin(client, linkCrypto);
    else if(cat == CAT_BORSA) http.begin(client, linkBorsa);
    http.addHeader("content-type", "application/json"); 
    http.addHeader("authorization", apiKey);
    int code = http.GET();
    if (code == 200) {
      if(cat != CAT_BORSA) {
          String payload = http.getString();
          DynamicJsonDocument doc(16000); deserializeJson(doc, payload); JsonArray result = doc["result"];
          if(cat == CAT_DOVIZ) {
             countDoviz = 0;
             for(JsonVariant v : result) {
                String code = v["code"].as<String>();
                for(int k=0; k<dovizSayisi; k++) {
                   if(code == String(hedefDoviz[k])) {
                      String ad = v["name"].as<String>();
                      if(code=="USD") ad="ABD Dolari"; else if(code=="EUR") ad="Euro"; else if(code=="GBP") ad="Sterlin";
                      dataDoviz[countDoviz].name = trDuzelt(ad);
                      dataDoviz[countDoviz].price = v["sellingstr"].as<String>(); 
                      dataDoviz[countDoviz].rate = v["rate"].as<String>();
                      String key = dataDoviz[countDoviz].name; key.replace(" ", ""); key.replace(".", ""); if(key.length()>15) key=key.substring(0,15);
                      preferences.begin("wallet", true); dataDoviz[countDoviz].myQuantity = preferences.getFloat(key.c_str(), 0); preferences.end();
                      countDoviz++; break;
                   }
                }
             }
          }
          else if(cat == CAT_EMTIA) {
             countEmtia = 0;
             for(JsonVariant v : result) {
                String rawName = v["name"].as<String>();
                for(int k=0; k<emtiaSayisi; k++) {
                   if(rawName == String(hedefEmtia[k]) || (String(hedefEmtia[k]) == "Brent Petrol" && rawName.indexOf("Petrol") >= 0)) {
                      dataEmtia[countEmtia].name = trDuzelt(rawName);
                      dataEmtia[countEmtia].price = v["sellingstr"].as<String>();
                      dataEmtia[countEmtia].rate = v["rate"].as<String>();
                      String key = dataEmtia[countEmtia].name; key.replace(" ", ""); key.replace(".", ""); if(key.length()>15) key=key.substring(0,15);
                      preferences.begin("wallet", true); dataEmtia[countEmtia].myQuantity = preferences.getFloat(key.c_str(), 0); preferences.end();
                      countEmtia++; break;
                   }
                }
             }
          }
          else if(cat == CAT_KRIPTO) {
             countKripto = 0;
             for(JsonVariant v : result) {
                String code = v["code"].as<String>();
                String name = v["name"].as<String>();
                for(int k=0; k<kriptoSayisi; k++) {
                   String hedef = String(hedefKripto[k]);
                   if(code == hedef || name.indexOf(hedef) >= 0) {
                      dataKripto[countKripto].name = v["name"].as<String>();
                      dataKripto[countKripto].price = v["price"].as<String>();
                      dataKripto[countKripto].rate = v["changeHour"].as<String>();
                      String key = dataKripto[countKripto].name; key.replace(" ", ""); key.replace(".", ""); if(key.length()>15) key=key.substring(0,15);
                      preferences.begin("wallet", true); dataKripto[countKripto].myQuantity = preferences.getFloat(key.c_str(), 0); preferences.end();
                      countKripto++; break;
                   }
                }
             }
          }
      } 
      else {
          WiFiClient *stream = http.getStreamPtr(); countBorsa = 0; int foundTotal = 0; 
          while(http.connected() && (stream->available() > 0 || stream->connected())) {
             String chunk = stream->readStringUntil('}'); String chunkUpper = chunk; chunkUpper.toUpperCase();
             for(int k=0; k<hisseSayisi; k++) {
                String hedef = String(hedefHisseler[k]); String hedefUpper = hedef; hedefUpper.toUpperCase();
                if(chunkUpper.indexOf("\"NAME\":\"" + hedefUpper + "\"") != -1 || chunkUpper.indexOf("\"TEXT\":\"" + hedefUpper + "\"") != -1) {
                    bool exists = false; for(int z=0; z<countBorsa; z++) if(dataBorsa[z].name == hedef) exists=true; if(exists) break;
                    dataBorsa[countBorsa].name = hedef;
                    dataBorsa[countBorsa].price = extractValue(chunk, "price");
                    if(dataBorsa[countBorsa].price == "") dataBorsa[countBorsa].price = extractValue(chunk, "pricestr");
                    dataBorsa[countBorsa].rate = extractValue(chunk, "rate");
                    String key = hedef; key.replace(" ", ""); key.replace(".", ""); if(key.length() > 15) key = key.substring(0, 15);
                    preferences.begin("wallet", true); dataBorsa[countBorsa].myQuantity = preferences.getFloat(key.c_str(), 0); preferences.end();
                    countBorsa++; foundTotal++; break;
                }
             }
             if(foundTotal >= hisseSayisi - 1) break; if(countBorsa >= 20) break; esp_task_wdt_reset();
          }
      }
    }
    http.end();
  }
}

float parseSmartPrice(String fiyatStr) {
  String clean = "";
  for (int i = 0; i < fiyatStr.length(); i++) {
    char c = fiyatStr.charAt(i); if (isdigit(c) || c == '.' || c == ',') clean += c;
  }
  if (clean.indexOf(',') != -1) { clean.replace(".", ""); clean.replace(",", "."); } 
  else if (clean.indexOf('.') != -1) {
     int dots = 0; for(int k=0; k<clean.length(); k++) if(clean.charAt(k)=='.') dots++;
     if(dots > 1) clean.replace(".", ""); 
  }
  return clean.toFloat();
}

void playTone(int freq, int duration) {
  ledcWriteTone(BUZZER_CH, freq); delay(duration); ledcWriteTone(BUZZER_CH, 0);
}
void playClick() { playTone(2000, 20); }
void playSuccess() { playTone(1047, 100); delay(20); playTone(1319, 100); delay(20); playTone(1568, 150); delay(20); playTone(2093, 300); }
void playFail() { playTone(880, 150); delay(20); playTone(830, 150); delay(20); playTone(784, 150); delay(20); playTone(740, 400); }

void checkWealthChange(double newWealth) {
  preferences.begin("wallet", true); double last = preferences.getDouble("lastWealth", 0.0); preferences.end();
  lastSavedWealth = last; 
  if(last == 0.0) { preferences.begin("wallet", false); preferences.putDouble("lastWealth", newWealth); preferences.end(); return; }
  if(newWealth > last) { playSuccess(); setLeds(0,1,0); } 
  else if(newWealth < last) { playFail(); setLeds(1,0,0); } 
  else playTone(500, 100);
  preferences.begin("wallet", false); preferences.putDouble("lastWealth", newWealth); preferences.end();
}

void tumVerileriCek() {
  setLeds(0,0,1); 
  drawLoadingScreen(5, "Baglanti Kontrol");
  drawLoadingScreen(20, "Emtia Verileri..."); kategoriGuncelle(CAT_EMTIA); 
  drawLoadingScreen(40, "Doviz Kurlari..."); kategoriGuncelle(CAT_DOVIZ); 
  drawLoadingScreen(60, "Borsa Hisseleri..."); kategoriGuncelle(CAT_BORSA); 
  drawLoadingScreen(80, "Kripto Piyasasi..."); kategoriGuncelle(CAT_KRIPTO);
  drawLoadingScreen(95, "Hesaplaniyor..."); calculateWallet();
  drawLoadingScreen(100, "TAMAMLANDI!");
  checkWealthChange(globalTotalWealth); 
  delay(500); ekraniCiz();
}

void drawLoadingScreen(int percent, String stepName) {
    esp_task_wdt_reset();
    display.clearDisplay(); display.drawRect(0, 0, 128, 64, WHITE); display.fillRect(0, 0, 128, 16, WHITE);
    display.setTextColor(BLACK); display.setTextSize(1); display.setCursor(10, 4); display.print("SISTEM GUNCELLEME");
    display.setTextColor(WHITE); display.setCursor(10, 25); display.println(stepName);
    display.drawRect(10, 45, 108, 10, WHITE); int fillWidth = map(percent, 0, 100, 0, 104);
    display.fillRect(12, 47, fillWidth, 6, WHITE); display.display();
    static bool yellowState = false; yellowState = !yellowState; setLeds(0, 0, yellowState);
    playTone(4000 + (percent*10), 10); 
}

void resetWalletMenu() {
    display.clearDisplay(); display.fillRoundRect(0, 0, 128, 64, 4, WHITE); 
    display.setTextColor(BLACK); display.setTextSize(2); display.setCursor(30, 10); display.println("DIKKAT");
    display.setTextSize(1); display.setCursor(15, 30); display.println("CUZDAN SILINSIN?");
    display.fillRect(0, 50, 128, 14, BLACK); display.setTextColor(WHITE); display.setCursor(5, 53); display.print("K3:IPTAL"); display.setCursor(75, 53); display.print("K4:SIL");
    display.display(); delay(1000); 
    while(true) {
        esp_task_wdt_reset();
        if(digitalRead(BTN_MODE) == LOW) { playClick(); ekraniCiz(); return; }
        if(digitalRead(BTN_ACTION) == LOW) {
            playTone(500, 300); display.clearDisplay(); display.setTextColor(WHITE); display.setCursor(30, 30); display.println("TEMIZLENIYOR..."); display.display();
            preferences.begin("wallet", false); preferences.clear(); preferences.end();
            for(int i=0; i<15; i++) dataEmtia[i].myQuantity=0; for(int i=0; i<10; i++) dataDoviz[i].myQuantity=0; for(int i=0; i<20; i++) dataBorsa[i].myQuantity=0; for(int i=0; i<20; i++) dataKripto[i].myQuantity=0;
            calculateWallet(); delay(1000); ekraniCiz(); return;
        }
        delay(50);
    }
}

void openEditor(MarketItem &item, String keyName) {
    float quantity = item.myQuantity; float step = 1.0; bool editing = true;
    while(editing) {
        esp_task_wdt_reset();
        display.clearDisplay(); display.fillRect(0, 0, 128, 14, WHITE);
        display.setTextColor(BLACK); display.setTextSize(1); display.setCursor(2, 3); display.print("ADET GIRINIZ");
        display.setTextColor(WHITE); display.setCursor(0, 20); display.println(item.name);
        display.setTextSize(2); display.setCursor(20, 35); display.print(quantity, (step < 1.0 ? 2 : 0)); 
        display.setTextSize(1); display.setCursor(0, 56);
        if(step == 1.0) display.print("[x1]"); else if(step == 10.0) display.print("[x10]"); else if(step == 100.0) display.print("[x100]"); else display.print("[x0.1]");
        display.setCursor(55, 56); display.print("K4:OK"); display.display();

        if(digitalRead(BTN_UP) == LOW) { playClick(); quantity += step; delay(150); }
        if(digitalRead(BTN_DOWN) == LOW) { playClick(); quantity -= step; if(quantity < 0) quantity = 0; delay(150); }
        if(digitalRead(BTN_MODE) == LOW) {
            playClick(); if(step == 1.0) step = 10.0; else if (step == 10.0) step = 100.0; else if (step == 100.0) step = 0.1; else step = 1.0;
            while(digitalRead(BTN_MODE) == LOW); delay(200);
        }
        if(digitalRead(BTN_ACTION) == LOW) {
            playTone(1500, 100); display.clearDisplay(); display.setCursor(30, 30); display.println("KAYDEDILDI!"); display.display();
            preferences.begin("wallet", false); preferences.putFloat(keyName.c_str(), quantity); preferences.end();
            item.myQuantity = quantity; calculateWallet(); delay(1000); editing = false;
        }
        delay(10);
    }
    ekraniCiz();
}

String trDuzelt(String text) {
  text.replace("Ã§", "c"); text.replace("ç", "c"); text.replace("Ä±", "i"); text.replace("ı", "i");
  text.replace("Ä°", "I"); text.replace("İ", "I"); text.replace("ÄŸ", "g"); text.replace("ğ", "g");
  text.replace("Ã¼", "u"); text.replace("ü", "u"); text.replace("ÅŸ", "s"); text.replace("ş", "s");
  text.replace("Ã¶", "o"); text.replace("ö", "o"); text.replace("Ç", "C"); text.replace("Ğ", "G");
  text.replace("Ü", "U"); text.replace("Ş", "S"); text.replace("Ö", "O"); text.replace("İ", "I");
  return text;
}

String formatMoney(long val) {
  String s = String(val); int len = s.length();
  if(len <= 3) return s; String res = ""; int count = 0;
  for(int i=len-1; i>=0; i--) { res = s[i] + res; count++; if(count % 3 == 0 && i != 0) res = "." + res; }
  return res;
}

void calculateWallet() {
  countWallet = 0; globalTotalWealth = 0.0; preferences.begin("wallet", true); 
  float dolarKuru = 36.0; if(countDoviz > 0) dolarKuru = parseSmartPrice(dataDoviz[0].price);

  for(int i=0; i<countEmtia; i++) {
     String key = dataEmtia[i].name; key.replace(" ", ""); key.replace(".", ""); if(key.length()>15) key=key.substring(0,15);
     float qty = preferences.getFloat(key.c_str(), 0); dataEmtia[i].myQuantity = qty; 
     if(qty > 0) {
         float rawPrice = parseSmartPrice(dataEmtia[i].price); double val = rawPrice * qty; globalTotalWealth += val; 
         dataWallet[countWallet].name = dataEmtia[i].name; dataWallet[countWallet].valueTL = formatMoney((long)val); dataWallet[countWallet].quantityStr = String(qty, 1); countWallet++;
     }
  }
  for(int i=0; i<countDoviz; i++) {
     String key = dataDoviz[i].name; key.replace(" ", ""); key.replace(".", ""); if(key.length()>15) key=key.substring(0,15);
     float qty = preferences.getFloat(key.c_str(), 0); dataDoviz[i].myQuantity = qty;
     if(qty > 0) {
         float rawPrice = parseSmartPrice(dataDoviz[i].price); double val = rawPrice * qty; globalTotalWealth += val;
         dataWallet[countWallet].name = dataDoviz[i].name; dataWallet[countWallet].valueTL = formatMoney((long)val); dataWallet[countWallet].quantityStr = String((int)qty); countWallet++;
     }
  }
  for(int i=0; i<countKripto; i++) {
     String key = dataKripto[i].name; key.replace(" ", ""); key.replace(".", ""); if(key.length()>15) key=key.substring(0,15);
     float qty = preferences.getFloat(key.c_str(), 0); dataKripto[i].myQuantity = qty;
     if(qty > 0) {
         float rawPrice = parseSmartPrice(dataKripto[i].price); double val = (rawPrice * qty) * dolarKuru; globalTotalWealth += val;
         dataWallet[countWallet].name = dataKripto[i].name; dataWallet[countWallet].valueTL = formatMoney((long)val); dataWallet[countWallet].quantityStr = String(qty, 4); countWallet++;
     }
  }
  for(int i=0; i<countBorsa; i++) {
     String key = dataBorsa[i].name; key.replace(" ", ""); key.replace(".", ""); if(key.length()>15) key=key.substring(0,15);
     float qty = preferences.getFloat(key.c_str(), 0); dataBorsa[i].myQuantity = qty;
     if(qty > 0) {
         float rawPrice = parseSmartPrice(dataBorsa[i].price); double val = rawPrice * qty; globalTotalWealth += val;
         dataWallet[countWallet].name = dataBorsa[i].name; dataWallet[countWallet].valueTL = formatMoney((long)val); dataWallet[countWallet].quantityStr = String((int)qty); countWallet++;
     }
  }
  preferences.end();
}

void drawBigTrendArrow(int x, int y, bool isUp) {
  if(isUp) {
    display.drawLine(x, y+14, x+7, y+7, WHITE); display.drawLine(x+7, y+7, x+12, y+11, WHITE); display.drawLine(x+12, y+11, x+20, y+2, WHITE); 
    display.fillTriangle(x+20, y, x+16, y+6, x+24, y+6, WHITE);
  } else {
    display.drawLine(x, y, x+7, y+7, WHITE); display.drawLine(x+7, y+7, x+12, y+3, WHITE); display.drawLine(x+12, y+3, x+20, y+12, WHITE);
    display.fillTriangle(x+20, y+14, x+16, y+8, x+24, y+8, WHITE);
  }
}

void ekraniCiz() {
  display.clearDisplay();
  
  if(currentCategory == CAT_WALLET) {
     display.fillRect(0, 0, 128, 16, WHITE); display.setTextColor(BLACK); display.setTextSize(1);
     display.setCursor(2, 4); display.print("TOPLAM: "); display.print(formatMoney((long)globalTotalWealth)); display.print(" TL");
     display.setTextColor(WHITE);
     int startY = 20; int itemHeight = 22; 
     for(int i=0; i<2; i++) { 
         int dataIndex = scrollIndex + i; if(dataIndex >= countWallet) break;
         WalletItem w = dataWallet[dataIndex];
         int y = startY + (i * itemHeight);
         display.setTextSize(1); display.setCursor(0, y); display.println(w.name);
         display.setCursor(0, y + 10); display.print(w.quantityStr);
         String valStr = w.valueTL + " TL"; int strLen = valStr.length() * 6; 
         display.setCursor(128 - strLen, y + 10); display.print(valStr);
     }
     if(countWallet > 2) {
         int barHeight = 40 / countWallet * 2; int barY = 18 + (scrollIndex * (40 / countWallet));
         display.drawLine(127, 18, 127, 63, BLACK); display.drawLine(127, barY, 127, barY + barHeight, WHITE);
     }
     display.drawLine(0, 41, 128, 41, WHITE); 
     
     if(globalTotalWealth > lastSavedWealth) setLeds(0,1,0); 
     else if(globalTotalWealth < lastSavedWealth) setLeds(1,0,0);
     
     display.display(); return;
  }

  display.fillRect(0, 0, 128, 14, WHITE); display.setTextColor(BLACK); display.setTextSize(1); display.setCursor(35, 3);
  MarketItem *currentItem; int currentCount = 0;
  if(currentCategory == CAT_EMTIA) { display.print("EMTIA PIYASASI"); currentItem = &dataEmtia[itemIndex]; currentCount = countEmtia; }
  else if(currentCategory == CAT_DOVIZ) { display.print("DOVIZ KURLARI"); currentItem = &dataDoviz[itemIndex]; currentCount = countDoviz; }
  else if(currentCategory == CAT_BORSA) { display.print("BORSA ISTANBUL"); currentItem = &dataBorsa[itemIndex]; currentCount = countBorsa; }
  else if(currentCategory == CAT_KRIPTO) { display.print("KRIPTO PARA"); currentItem = &dataKripto[itemIndex]; currentCount = countKripto; }
  
  display.setCursor(2, 3); if(currentCount > 0) { display.print(itemIndex + 1); display.print("/"); display.print(currentCount); } else display.print("-");

  display.setTextColor(WHITE);
  if(currentCount > 0) {
    display.setTextSize(1); display.setCursor(0, 20); display.println(currentItem->name);
    display.setTextSize(2); display.setCursor(0, 32); 
    String fiyat = currentItem->price; if(fiyat.length() > 8) display.setTextSize(1); display.print(fiyat);
    display.setTextSize(1); if(currentCategory == CAT_KRIPTO) display.println(" $"); else display.println(" TL");
    bool isNegative = (currentItem->rate.indexOf("-") >= 0);
    drawBigTrendArrow(100, 38, !isNegative); 
    display.setCursor(95, 55); if(!isNegative) display.print("+%"); else display.print("%"); display.println(currentItem->rate);
    display.drawLine(0, 63, 128, 63, WHITE);
    
    updateLedForRate(currentItem->rate);

  } else {
    display.setCursor(10, 30); display.setTextSize(1);
    if(currentCategory == CAT_BORSA) display.println("BORSA KAPALI"); else display.println("Veri Yok");
    setLeds(0,0,0); 
  }
  display.display();
}