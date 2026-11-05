/*
 * ESP32 Akıllı Sulama + Sıcaklık İzleme + Uzaktan Kontrol
 * --------------------------------------------------------
 * ESP32  <-->  Firebase Realtime Database  <-->  Web arayüzü (mentör)
 *
 * Sensörler:
 *   - Toprak nem sensörü (analog)  -> toprak nemi  -> SULAMA LED'i
 *   - DHT11 / DHT22                -> sıcaklık     -> KLİMA LED'i
 *                                   + hava nemi   -> sadece gösterim
 *
 * Veritabanı yapısı:
 *   /sensor  -> { sicaklik, nem, hamNem, havaNem, zaman }   (ESP32 yazar, web okur)
 *   /kontrol -> { mod, klima, sulama,
 *                 sicaklikEsik, nemEsik }          (web yazar, ESP32 okur)
 *   /durum   -> { klima, sulama }                  (ESP32 yazar: LED'lerin GERÇEK durumu)
 *
 * Yerel panel:
 *   ESP32 ayrıca kendi IP adresinde (ve http://sera.local adresinde) aynı paneli yayınlar.
 *   Aynı Wi-Fi'a bağlı herkes açabilir. Oradan verilen komutlar Firebase'e de yazılır,
 *   böylece iki panel her zaman aynı durumu gösterir.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// ================== AYARLAR (BURAYI DÜZENLE) ==================
// Wi-Fi bilgileri ve Firebase adresi include/secrets.h dosyasında.
// İlk kurulumda include/secrets.example.h dosyasını secrets.h adıyla kopyalayıp doldur.
#include "secrets.h"

// Yerel panel adresi: http://sera.local  (IP adresi de her zaman çalışır)
const char* CIHAZ_ADI = "sera";

// --- Pinler ---
#define DHT_PIN       4         // Sıcaklık sensörü DATA ucu (dijital, Wi-Fi ile sorunsuz)
#define DHT_TYPE      DHT11     // DHT22 kullanıyorsan: DHT22
#define TOPRAK_PIN    34        // Toprak sensörü AO ucu. MUTLAKA ADC1 pini: 32,33,34,35,36,39
#define KLIMA_LED     26        // "Klima" yerine yanan LED
#define SULAMA_LED    27        // "Su pompası" yerine yanan LED

// --- Toprak sensörü kalibrasyonu ---
// Serial Monitor'de "ham" değeri izle:
//   sensör havadayken (tamamen kuru)  gördüğün sayıyı KURU_DEGER'e,
//   sensör bardak suyun içindeyken    gördüğün sayıyı ISLAK_DEGER'e yaz.
const int KURU_DEGER  = 3200;
const int ISLAK_DEGER = 1300;

// --- Zamanlama ---
const unsigned long SENSOR_ARALIK  = 5000;  // ms - sensör okuma + gönderme sıklığı
const unsigned long KONTROL_ARALIK = 2000;  // ms - mentörün komutlarını kontrol sıklığı

// --- Histerezis (açma ile kapama arasındaki tampon) ---
const float SICAKLIK_TAMPON = 0.5;   // °C
const float NEM_TAMPON      = 10.0;  // % : eşiğin 10 puan üstüne çıkana kadar sular
// ===============================================================

// ---------------------------------------------------------------
// Yerel panelin web sayfası (ESP32 bunu tarayıcıya gönderir)
// ---------------------------------------------------------------
const char PANEL_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="tr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Akıllı Sera Paneli (Yerel)</title>
<style>
  :root{
    --bg:#f4f6f8; --kart:#fff; --yazi:#1d2530; --soluk:#6b7684; --cizgi:#e3e7ec;
    --sicak:#e8590c; --nem:#1c7ed6; --hava:#0ca678; --iyi:#2f9e44; --uyari:#f08c00; --kotu:#e03131;
  }
  @media (prefers-color-scheme: dark){
    :root{ --bg:#11151a; --kart:#1a2027; --yazi:#e8edf2; --soluk:#8d99a6; --cizgi:#2a323b; }
  }
  *{box-sizing:border-box}
  body{margin:0;background:var(--bg);color:var(--yazi);font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
  main{max-width:860px;margin:0 auto;padding:20px 16px 40px}
  header{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:18px}
  h1{font-size:1.35rem;margin:0}
  .rozet{font-size:.85rem;padding:5px 10px;border-radius:999px;background:var(--kart);border:1px solid var(--cizgi);white-space:nowrap}
  .rozet::before{content:"";display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px;background:var(--soluk)}
  .rozet.cevrimici::before{background:var(--iyi)} .rozet.cevrimdisi::before{background:var(--kotu)}
  .izgara{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:14px}
  .kart{background:var(--kart);border:1px solid var(--cizgi);border-radius:14px;padding:18px}
  .etiket{color:var(--soluk);font-size:.85rem;text-transform:uppercase;letter-spacing:.04em}
  .deger{font-size:3rem;font-weight:700;line-height:1.1;margin:6px 0;font-variant-numeric:tabular-nums}
  .sicak .deger{color:var(--sicak)} .nem .deger{color:var(--nem)} .hava .deger{color:var(--hava)}
  .alt{color:var(--soluk);font-size:.9rem}
  .uyari{display:none;margin:14px 0;padding:14px 16px;border-radius:12px;background:color-mix(in srgb,var(--uyari) 15%,var(--kart));border:1px solid var(--uyari);align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap}
  .uyari.goster{display:flex}
  h2{font-size:1.05rem;margin:26px 0 10px}
  .cihaz{display:flex;justify-content:space-between;align-items:center;gap:10px}
  .cihaz h3{margin:0 0 4px;font-size:1.1rem}
  .led{display:inline-block;width:12px;height:12px;border-radius:50%;background:var(--cizgi);margin-right:6px;vertical-align:middle}
  .led.acik{background:var(--iyi);box-shadow:0 0 10px var(--iyi)}
  button{font:inherit;border:0;border-radius:10px;padding:10px 16px;cursor:pointer;background:var(--yazi);color:var(--bg);font-weight:600}
  button.ikincil{background:transparent;color:var(--yazi);border:1px solid var(--cizgi)}
  button:disabled{opacity:.4;cursor:not-allowed}
  .mod{display:flex;background:var(--kart);border:1px solid var(--cizgi);border-radius:12px;padding:4px;width:fit-content}
  .mod button{background:transparent;color:var(--soluk)}
  .mod button.secili{background:var(--yazi);color:var(--bg)}
  label{display:block;font-size:.9rem;color:var(--soluk);margin-bottom:6px}
  input[type=number]{font:inherit;width:100%;padding:9px 10px;border-radius:9px;border:1px solid var(--cizgi);background:var(--bg);color:var(--yazi)}
  .satir{display:flex;gap:10px;align-items:end}
  .not{color:var(--soluk);font-size:.85rem;margin-top:8px}
</style>
</head>
<body>
<main>
  <header>
    <h1>Akıllı Sera Paneli <small style="font-weight:400;font-size:.7em;opacity:.6">yerel</small></h1>
    <span id="baglanti" class="rozet">Bağlanıyor…</span>
  </header>

  <div class="izgara">
    <div class="kart sicak">
      <div class="etiket">Sıcaklık</div>
      <div class="deger" id="sicaklik">--</div>
      <div class="alt">Eşik: <span id="sEsikYazi">--</span> °C</div>
    </div>
    <div class="kart nem">
      <div class="etiket">Toprak nemi</div>
      <div class="deger" id="nem">--</div>
      <div class="alt">Sulama eşiği: %<span id="nEsikYazi">--</span> <span id="ham" style="opacity:.6"></span></div>
    </div>
    <div class="kart hava">
      <div class="etiket">Hava nemi</div>
      <div class="deger" id="havaNem">--</div>
      <div class="alt">Ortamdaki bağıl nem (DHT11)</div>
    </div>
  </div>

  <div class="uyari" id="sUyari">
    <span>🌡️ Sıcaklık eşiği aştı.</span>
    <button onclick="manuelAc('klima')">Klimayı aç</button>
  </div>
  <div class="uyari" id="nUyari">
    <span>🌱 Toprak kurudu, sulama gerekiyor.</span>
    <button onclick="manuelAc('sulama')">Sulamayı başlat</button>
  </div>

  <h2>Çalışma modu</h2>
  <div class="mod">
    <button id="modOto" onclick="kaydet({mod:'otomatik'})">Otomatik</button>
    <button id="modMan" onclick="kaydet({mod:'manuel'})">Manuel</button>
  </div>
  <p class="not" id="modNot"></p>

  <h2>Cihazlar</h2>
  <div class="izgara">
    <div class="kart cihaz">
      <div><h3>Klima</h3><span class="led" id="klimaLed"></span><span id="klimaDurum">--</span></div>
      <button id="klimaBtn" onclick="degistir('klima')">Aç</button>
    </div>
    <div class="kart cihaz">
      <div><h3>Sulama</h3><span class="led" id="sulamaLed"></span><span id="sulamaDurum">--</span></div>
      <button id="sulamaBtn" onclick="degistir('sulama')">Aç</button>
    </div>
  </div>

  <h2>Eşik değerleri</h2>
  <div class="kart">
    <div class="izgara">
      <div><label for="sEsik">Sıcaklık eşiği (°C)</label><input id="sEsik" type="number" step="0.5"></div>
      <div><label for="nEsik">Sulama eşiği – toprak nemi bunun altına düşünce (%)</label><input id="nEsik" type="number" step="1"></div>
    </div>
    <div class="satir" style="margin-top:12px">
      <button class="ikincil" onclick="esikKaydet()">Eşikleri kaydet</button>
    </div>
  </div>
</main>

<script>
// Bu sayfa doğrudan ESP32'den yükleniyor; veriyi ESP32'nin kendi API'sinden alır.
const YENILEME_MS = 2000;

let veri = { sensor:{}, kontrol:{}, durum:{} };
let esiklerDolduruldu = false;
let online = false;
const $ = id => document.getElementById(id);

// ESP32'den güncel verileri al
async function yenile(){
  try{
    const r = await fetch("/api/veri?t=" + Date.now());
    if(!r.ok) throw new Error(r.status);
    veri = await r.json();
    online = true;
  }catch(e){
    online = false;
  }
  ciz();
}

// Komutu ESP32'ye gönder (ESP32 bunu Firebase'e de iletir)
async function kaydet(alanlar){
  Object.assign(veri.kontrol, alanlar); ciz();   // arayüz hemen tepki versin
  try{
    await fetch("/api/kontrol", {
      method:"POST", headers:{"Content-Type":"application/json"}, body:JSON.stringify(alanlar)
    });
  }catch(e){}
  yenile();
}

function degistir(cihaz){
  const gercek = !!veri.durum[cihaz];
  kaydet({ [cihaz]: !gercek });
}
function manuelAc(cihaz){ kaydet({ mod:"manuel", [cihaz]: true }); }
function esikKaydet(){
  const s = parseFloat($("sEsik").value), n = parseFloat($("nEsik").value);
  if(isNaN(s) || isNaN(n)) return alert("Geçerli sayı gir.");
  kaydet({ sicaklikEsik:s, nemEsik:n });
}

function ciz(){
  const { sensor:s, kontrol:k, durum:d } = veri;
  const sEsik = k.sicaklikEsik ?? 28, nEsik = k.nemEsik ?? 30;
  const otomatik = (k.mod ?? "otomatik") === "otomatik";

  // Ölçümler
  $("sicaklik").textContent = s.sicaklik != null ? s.sicaklik.toFixed(1) + "°C" : "--";
  $("nem").textContent      = s.nem != null ? "%" + s.nem.toFixed(0) : "--";
  $("ham").textContent      = s.hamNem != null ? "(ham: " + s.hamNem + ")" : "";
  $("havaNem").textContent  = s.havaNem != null ? "%" + s.havaNem.toFixed(0) : "--";
  $("sEsikYazi").textContent = sEsik; $("nEsikYazi").textContent = nEsik;

  // ESP32'ye ulaşabiliyor muyuz?
  $("baglanti").textContent = online ? "ESP32'ye bağlı" : "ESP32'ye ulaşılamıyor";
  $("baglanti").className = "rozet " + (online ? "cevrimici" : "cevrimdisi");

  // Uyarılar (manuel modda ve cihaz kapalıyken öneri göster)
  $("sUyari").classList.toggle("goster", !otomatik && s.sicaklik != null && s.sicaklik >= sEsik && !d.klima);
  $("nUyari").classList.toggle("goster", !otomatik && s.nem != null && s.nem <= nEsik && !d.sulama);

  // Mod
  $("modOto").classList.toggle("secili", otomatik);
  $("modMan").classList.toggle("secili", !otomatik);
  $("modNot").textContent = otomatik
    ? "ESP32 eşiklere göre cihazları kendisi açıp kapatıyor. Elle kontrol için Manuel'e geç."
    : "Cihazları sen kontrol ediyorsun. Eşik aşılınca burada öneri çıkar.";

  // Cihazlar: gösterilen durum ESP32'nin bildirdiği GERÇEK durum
  for(const c of ["klima","sulama"]){
    const acik = !!d[c];
    $(c+"Led").classList.toggle("acik", acik);
    $(c+"Durum").textContent = acik ? (c==="sulama" ? "Sulanıyor" : "Açık") : "Kapalı";
    $(c+"Btn").textContent = acik ? (c==="sulama" ? "Durdur" : "Kapat") : (c==="sulama" ? "Sula" : "Aç");
    $(c+"Btn").disabled = otomatik;
  }

  // Eşik kutularını ilk açılışta doldur (kullanıcı yazarken ezme)
  if(!esiklerDolduruldu && k.sicaklikEsik != null){
    $("sEsik").value = sEsik; $("nEsik").value = nEsik; esiklerDolduruldu = true;
  }
}

yenile();
setInterval(yenile, YENILEME_MS);
</script>
</body>
</html>
)rawliteral";

DHT dht(DHT_PIN, DHT_TYPE);
WiFiClientSecure tlsClient;
WebServer server(80);   // yerel web sunucu, port 80

// ESP32'nin iki çekirdeği var. Yavaş Firebase işleri bir çekirdekte (Çekirdek 0),
// yerel web sunucu diğerinde (Çekirdek 1) çalışıyor; böylece biri diğerini bekletmiyor.
// İkisi aynı değişkenlere eriştiği için, aynı anda değiştirmesinler diye bir "kilit" kullanıyoruz:
// bir taraf değişkenlerle işini bitirene kadar diğeri bekler (bu bekleme mikrosaniyeler sürer).
SemaphoreHandle_t kilit;
#define KILITLE() xSemaphoreTake(kilit, portMAX_DELAY)
#define BIRAK()   xSemaphoreGive(kilit)

// Son ölçümler
float sicaklik = NAN;
float nem      = NAN;   // toprak nemi, %
int   hamNem   = 0;     // kalibrasyon için ham ADC değeri
float havaNem  = NAN;   // havanın bağıl nemi, % (DHT'den)

// Kontrol bilgileri (varsayılanlar)
String mod           = "otomatik";   // "otomatik" | "manuel"
bool   istenenKlima  = false;
bool   istenenSulama = false;
float  sicaklikEsik  = 28.0;         // bunun ÜSTÜNDE klima açılır
float  nemEsik       = 30.0;         // bunun ALTINDA sulama başlar

// LED'lerin gerçek durumu
bool klimaAcik  = false;
bool sulamaAcik = false;
bool durumGonderilecek = true;       // /durum Firebase'e gönderilmeli mi?

// Yerel panelden gelen ve Firebase'e iletilmeyi bekleyen komutlar
JsonDocument bekleyenKomut;
uint32_t yerelDegisiklik = 0;        // yerel panelden her komut gelişinde 1 artar

// ---------------------------------------------------------------
// Yardımcı fonksiyonlar
// ---------------------------------------------------------------
void wifiBaglan() {
  Serial.printf("WiFi'a baglaniliyor: %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long baslangic = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - baslangic < 20000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nBaglandi! IP: %s\n", WiFi.localIP().toString().c_str());
    MDNS.end();
    if (MDNS.begin(CIHAZ_ADI)) MDNS.addService("http", "tcp", 80);
    Serial.printf(">>> Yerel panel: http://%s  veya  http://%s.local\n",
                  WiFi.localIP().toString().c_str(), CIHAZ_ADI);
  } else {
    Serial.println("\nWiFi baglantisi basarisiz, tekrar denenecek.");
  }
}

// Firebase'e veri yazar. metod: "PUT" (üzerine yaz) veya "PATCH" (sadece verilen alanları güncelle)
bool firebaseYaz(const String& yol, const String& json, const char* metod = "PUT") {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  String url = String(FIREBASE_URL) + yol + ".json";
  http.begin(tlsClient, url);
  http.addHeader("Content-Type", "application/json");
  int kod = http.sendRequest(metod, json);
  http.end();
  if (kod != 200) Serial.printf("Yazma hatasi (%s): %d\n", yol.c_str(), kod);
  return kod == 200;
}

// Firebase'den veri okur. Hata olursa boş string döner.
String firebaseOku(const String& yol) {
  if (WiFi.status() != WL_CONNECTED) return "";
  HTTPClient http;
  String url = String(FIREBASE_URL) + yol + ".json";
  http.begin(tlsClient, url);
  int kod = http.GET();
  String cevap = (kod == 200) ? http.getString() : "";
  if (kod != 200) Serial.printf("Okuma hatasi (%s): %d\n", yol.c_str(), kod);
  http.end();
  return cevap;
}

// Toprak sensörünü 10 kez okuyup ortalamasını alır (analog okuma gürültülüdür)
int toprakHamOku() {
  long toplam = 0;
  for (int i = 0; i < 10; i++) {
    toplam += analogRead(TOPRAK_PIN);
    delay(5);
  }
  return toplam / 10;
}

// Ham değeri yüzdeye çevirir: KURU_DEGER -> %0, ISLAK_DEGER -> %100
float hamdanYuzdeye(int ham) {
  float yuzde = (float)(KURU_DEGER - ham) * 100.0 / (KURU_DEGER - ISLAK_DEGER);
  return constrain(yuzde, 0.0, 100.0);
}

// ---------------------------------------------------------------
// 1) Karar ver ve LED'leri sür
//    (Hızlı bir işlem, internete çıkmaz. Çağıran taraf kilidi almış olmalı.)
// ---------------------------------------------------------------
void cihazlariGuncelle() {
  bool yeniKlima  = klimaAcik;
  bool yeniSulama = sulamaAcik;

  if (mod == "otomatik") {
    // KLİMA: sıcaklık eşiğe ULAŞINCA açılır, eşiğin biraz altına inince kapanır
    if (!isnan(sicaklik)) {
      if (sicaklik >= sicaklikEsik)                         yeniKlima = true;
      else if (sicaklik < sicaklikEsik - SICAKLIK_TAMPON)   yeniKlima = false;
    }
    // SULAMA: toprak nemi eşiğin ALTINA düşünce başlar,
    //         eşiğin NEM_TAMPON kadar üstüne çıkınca durur (toprak iyice ıslansın)
    if (!isnan(nem)) {
      if (nem <= nemEsik)                                   yeniSulama = true;
      else if (nem >= nemEsik + NEM_TAMPON)                 yeniSulama = false;
    }
  } else {
    // Manuel mod: mentör ne dediyse o
    yeniKlima  = istenenKlima;
    yeniSulama = istenenSulama;
  }

  if (yeniKlima != klimaAcik || yeniSulama != sulamaAcik) {
    klimaAcik  = yeniKlima;
    sulamaAcik = yeniSulama;
    durumGonderilecek = true;        // Firebase görevi bunu fark edip gönderecek
    Serial.printf("[%s] Klima: %s | Sulama: %s\n", mod.c_str(),
                  klimaAcik ? "ACIK" : "KAPALI", sulamaAcik ? "ACIK" : "KAPALI");
  }
  digitalWrite(KLIMA_LED,  klimaAcik  ? HIGH : LOW);
  digitalWrite(SULAMA_LED, sulamaAcik ? HIGH : LOW);
}

// ---------------------------------------------------------------
// 2) FIREBASE TARAFI (Çekirdek 0'da çalışır)
// ---------------------------------------------------------------

// Sensörleri oku ve Firebase'e gönder
void sensorOkuVeGonder() {
  int   ham = toprakHamOku();
  float n   = hamdanYuzdeye(ham);
  float t   = dht.readTemperature();
  float h   = dht.readHumidity();     // DHT aynı anda havanın nemini de ölçüyor

  KILITLE();
  hamNem = ham;
  nem    = n;
  if (!isnan(t)) sicaklik = t;
  if (!isnan(h)) havaNem  = h;
  float s = sicaklik;
  float hn = havaNem;
  BIRAK();

  if (isnan(t) || isnan(h)) Serial.println("DHT okunamadi! Kablolari kontrol et.");
  Serial.printf("Sicaklik: %.1f C | Hava nemi: %.0f %% | Toprak nemi: %.0f %% (ham: %d)\n",
                s, hn, n, ham);

  JsonDocument doc;
  if (!isnan(s))  doc["sicaklik"] = round(s * 10) / 10.0;
  if (!isnan(hn)) doc["havaNem"]  = round(hn);
  doc["nem"]    = round(n);
  doc["hamNem"] = ham;
  doc["zaman"][".sv"] = "timestamp";   // Firebase sunucu saatini yazar
  String json;
  serializeJson(doc, json);
  firebaseYaz("/sensor", json);
}

// Mentörün (uzak panelden) verdiği komutları oku
void kontrolOku() {
  KILITLE();
  uint32_t baslangicSayaci = yerelDegisiklik;
  BIRAK();

  String cevap = firebaseOku("/kontrol");   // bu satır ~1 sn sürebilir
  if (cevap == "") return;                  // bağlantı hatası

  if (cevap == "null") {                    // ilk çalıştırma: varsayılanları oluştur
    JsonDocument doc;
    KILITLE();
    doc["mod"] = mod;
    doc["klima"] = false;
    doc["sulama"] = false;
    doc["sicaklikEsik"] = sicaklikEsik;
    doc["nemEsik"] = nemEsik;
    BIRAK();
    String json;
    serializeJson(doc, json);
    firebaseYaz("/kontrol", json);
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, cevap)) {
    Serial.println("Kontrol verisi cozumlenemedi.");
    return;
  }

  KILITLE();
  // Biz Firebase'den okurken yerel panelden yeni bir komut geldiyse, okuduğumuz veri
  // artık eski demektir. Onu uygularsak yerel paneldeki komutu geri almış oluruz, o yüzden atla.
  if (yerelDegisiklik == baslangicSayaci) {
    mod           = doc["mod"]          | "otomatik";
    istenenKlima  = doc["klima"]        | false;
    istenenSulama = doc["sulama"]       | false;
    sicaklikEsik  = doc["sicaklikEsik"] | 28.0;
    nemEsik       = doc["nemEsik"]      | 30.0;
  }
  BIRAK();
}

// Yerel panelden gelen komutları Firebase'e ilet (iki panel aynı durumu göstersin)
void bekleyenKomutuGonder() {
  KILITLE();
  if (bekleyenKomut.size() == 0) { BIRAK(); return; }
  String json;
  serializeJson(bekleyenKomut, json);
  bekleyenKomut.clear();
  BIRAK();
  firebaseYaz("/kontrol", json, "PATCH");
}

// LED'lerin gerçek durumu değiştiyse Firebase'e bildir
void durumGonder() {
  KILITLE();
  if (!durumGonderilecek) { BIRAK(); return; }
  String json = String("{\"klima\":") + (klimaAcik ? "true" : "false") +
                ",\"sulama\":" + (sulamaAcik ? "true" : "false") + "}";
  durumGonderilecek = false;
  BIRAK();
  if (!firebaseYaz("/durum", json)) {
    KILITLE(); durumGonderilecek = true; BIRAK();   // olmadıysa sonra tekrar dene
  }
}

// Firebase görevi: sonsuz döngü, Çekirdek 0'da kendi başına çalışır
void firebaseGorevi(void* parametre) {
  unsigned long sonSensor = 0, sonKontrol = 0;
  bool ilkTur = true;
  for (;;) {
    unsigned long simdi = millis();

    bekleyenKomutuGonder();   // önce yerel komutları ilet, sonra oku

    if (ilkTur || simdi - sonSensor >= SENSOR_ARALIK) {
      sonSensor = simdi;
      sensorOkuVeGonder();
    }
    if (ilkTur || simdi - sonKontrol >= KONTROL_ARALIK) {
      sonKontrol = simdi;
      kontrolOku();
      KILITLE(); cihazlariGuncelle(); BIRAK();
    }

    durumGonder();
    ilkTur = false;
    vTaskDelay(pdMS_TO_TICKS(50));   // diğer işlere nefes aldır
  }
}

// ---------------------------------------------------------------
// 3) YEREL WEB SUNUCU (Çekirdek 1'de, loop() içinde çalışır)
// ---------------------------------------------------------------

// "/" -> paneli gönder
void sayfaGonder() {
  server.send_P(200, "text/html; charset=utf-8", PANEL_HTML);
}

// "/api/veri" -> tüm güncel bilgileri JSON olarak gönder
// (Firebase'deki yapının aynısı, böylece panel kodu iki yerde de aynı kalıyor)
void veriGonder() {
  JsonDocument doc;
  KILITLE();
  if (!isnan(sicaklik)) doc["sensor"]["sicaklik"] = round(sicaklik * 10) / 10.0;
  if (!isnan(nem))      doc["sensor"]["nem"]      = round(nem);
  doc["sensor"]["hamNem"] = hamNem;
  if (!isnan(havaNem))  doc["sensor"]["havaNem"]  = round(havaNem);
  doc["kontrol"]["mod"]          = mod;
  doc["kontrol"]["klima"]        = istenenKlima;
  doc["kontrol"]["sulama"]       = istenenSulama;
  doc["kontrol"]["sicaklikEsik"] = sicaklikEsik;
  doc["kontrol"]["nemEsik"]      = nemEsik;
  doc["durum"]["klima"]  = klimaAcik;
  doc["durum"]["sulama"] = sulamaAcik;
  BIRAK();

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

// "/api/kontrol" (POST) -> paneldeki butonlardan gelen komutu uygula
void kontrolAl() {
  JsonDocument gelen;
  if (deserializeJson(gelen, server.arg("plain"))) {
    server.send(400, "application/json", "{\"hata\":\"gecersiz JSON\"}");
    return;
  }

  KILITLE();
  // Sadece tanıdığımız alanları kabul et; gelmeyen alanlara dokunma.
  // Her değişikliği Firebase'e iletilmek üzere "bekleyenKomut"a da ekle.
  if (gelen["mod"].is<const char*>()) {
    mod = gelen["mod"].as<const char*>();
    bekleyenKomut["mod"] = mod;
  }
  if (gelen["klima"].is<bool>()) {
    istenenKlima = gelen["klima"];
    bekleyenKomut["klima"] = istenenKlima;
  }
  if (gelen["sulama"].is<bool>()) {
    istenenSulama = gelen["sulama"];
    bekleyenKomut["sulama"] = istenenSulama;
  }
  if (gelen["sicaklikEsik"].is<float>()) {
    sicaklikEsik = gelen["sicaklikEsik"];
    bekleyenKomut["sicaklikEsik"] = sicaklikEsik;
  }
  if (gelen["nemEsik"].is<float>()) {
    nemEsik = gelen["nemEsik"];
    bekleyenKomut["nemEsik"] = nemEsik;
  }
  yerelDegisiklik++;
  cihazlariGuncelle();   // LED'ler anında değişsin
  BIRAK();

  // İnterneti beklemeden hemen cevap ver; Firebase'e iletme işini Firebase görevi yapacak
  server.send(200, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(KLIMA_LED, OUTPUT);
  pinMode(SULAMA_LED, OUTPUT);
  digitalWrite(KLIMA_LED, LOW);
  digitalWrite(SULAMA_LED, LOW);

  analogReadResolution(12);         // 0-4095
  analogSetAttenuation(ADC_11db);   // 0-3.3V aralığını ölçebilsin

  kilit = xSemaphoreCreateMutex();
  dht.begin();
  tlsClient.setInsecure();   // HTTPS sertifika doğrulamasını atla (prototip için yeterli)
  wifiBaglan();

  server.on("/", HTTP_GET, sayfaGonder);
  server.on("/api/veri", HTTP_GET, veriGonder);
  server.on("/api/kontrol", HTTP_POST, kontrolAl);
  server.onNotFound([]() { server.send(404, "text/plain", "Bulunamadi"); });
  server.begin();

  // Firebase işlerini ayrı bir görev olarak Çekirdek 0'da başlat.
  // 16384: görevin bellek (stack) boyutu; HTTPS bağlantısı bol bellek ister.
  xTaskCreatePinnedToCore(firebaseGorevi, "firebase", 16384, NULL, 1, NULL, 0);
}

// loop() Çekirdek 1'de çalışır ve artık sadece yerel paneli dinliyor.
// Firebase ne kadar yavaş olursa olsun burası beklemiyor.
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiBaglan();
    return;
  }
  server.handleClient();
  delay(2);
}
