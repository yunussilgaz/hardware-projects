/*
 * SICAKLIK & NEM KAYIT SİSTEMİ (SON HALİ)
 * --------------------------------------
 *   - Her 10 dakikada bir ölçüm -> /olcumler.csv dosyasına kayıt + Telegram raporu
 *   - Üst eşik aşılınca buzzer + Telegram ALARM, alt eşik altında Telegram UYARI
 *   - Telegram'dan eşik değiştirme
 *   - ESP32 kendi IP adresinde bir web sayfası yayınlıyor:
 *       * Kayıtlardan sıcaklık ve nem grafikleri (noktanın üstüne gelince ölçüm zamanı)
 *       * Grafiklerde eşik çizgileri
 *       * Eşikleri sayfadan değiştirme, alarmı susturma
 *       * CSV dosyasını indirme
 *
 * Telegram komutları: /durum /esikler /sicaklik 18 28 /nem 30 70 /sustur /panel /yardim
 * Serial komutları:   durum, sicaklik 18 28, nem 30 70, sustur, liste, olc, bilgi,
 *                     sil (ESP32'deki TÜM kayıtları siler)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <time.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <DHT.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ================== AYARLAR (BURAYI DÜZENLE) ==================
// Wi-Fi bilgileri, Telegram bot token'ı ve chat ID include/secrets.h dosyasında.
// İlk kurulumda include/secrets.example.h dosyasını secrets.h adıyla kopyalayıp doldur.
#include "secrets.h"

// Yerel sayfa adresi: http://kayit.local (IP adresi de her zaman çalışır)
const char* CIHAZ_ADI = "kayit";

// --- Pinler ---
#define DHT_PIN      4
#define DHT_TYPE     DHT11
#define BUZZER_PIN   25

#define BUZZER_PASIF false   // pasif buzzer kullanıyorsan true
#define SESSIZ_MOD   true    // true: buzzer hiç ötmez

// --- Zamanlama ---
#define TEST_MODU    false                         // true: her dakika kayıt + mesaj
const int KAYIT_ARALIGI_DK = TEST_MODU ? 1 : 10;
const unsigned long KONTROL_ARALIGI_MS  = 30000;   // alarm kontrolü (30 sn)
const unsigned long TELEGRAM_ARALIGI_MS = 3000;    // yeni komut var mı? (3 sn)

// --- Varsayılan eşikler ---
const float VARSAYILAN_SIC_ALT = 18.0, VARSAYILAN_SIC_UST = 28.0;   // °C
const float VARSAYILAN_NEM_ALT = 30.0, VARSAYILAN_NEM_UST = 70.0;   // %

// --- Histerezis ---
const float SIC_TAMPON = 1.0;   // °C
const float NEM_TAMPON = 3.0;   // %

#define DOSYA_SISTEMI LittleFS
const char* KAYIT_DOSYASI = "/olcumler.csv";
const char* SAAT_DILIMI = "<+03>-3";   // Türkiye saati (UTC+3)
// ===============================================================


// ===============================================================
// WEB SAYFASI (ESP32 bunu tarayıcıya gönderir)
// ===============================================================
const char SAYFA_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="tr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Sıcaklık & Nem Kayıtları</title>
<style>
  :root{
    --bg:#f4f6f8; --kart:#fff; --yazi:#1d2530; --soluk:#6b7684; --cizgi:#e3e7ec;
    --sicak:#e8590c; --nem:#1c7ed6; --iyi:#2f9e44; --uyari:#f08c00; --kotu:#e03131;
  }
  @media (prefers-color-scheme: dark){
    :root{ --bg:#11151a; --kart:#1a2027; --yazi:#e8edf2; --soluk:#8d99a6; --cizgi:#2a323b; }
  }
  *{box-sizing:border-box}
  body{margin:0;background:var(--bg);color:var(--yazi);font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
  main{max-width:960px;margin:0 auto;padding:20px 16px 40px}
  header{display:flex;justify-content:space-between;align-items:center;gap:12px;flex-wrap:wrap;margin-bottom:16px}
  h1{font-size:1.3rem;margin:0}
  h2{font-size:1.05rem;margin:0 0 10px}
  .rozet{font-size:.85rem;color:var(--soluk)}
  .izgara{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:14px}
  .kart{background:var(--kart);border:1px solid var(--cizgi);border-radius:14px;padding:16px}
  .etiket{color:var(--soluk);font-size:.8rem;text-transform:uppercase;letter-spacing:.04em}
  .deger{font-size:2.6rem;font-weight:700;line-height:1.1;margin:4px 0;font-variant-numeric:tabular-nums}
  .sicak .deger{color:var(--sicak)} .nem .deger{color:var(--nem)}
  .alt{color:var(--soluk);font-size:.88rem}
  .durum{display:inline-block;font-size:.8rem;font-weight:600;padding:3px 9px;border-radius:999px;margin-top:6px}
  .durum.normal{background:color-mix(in srgb,var(--iyi) 15%,transparent);color:var(--iyi)}
  .durum.dusuk{background:color-mix(in srgb,var(--uyari) 18%,transparent);color:var(--uyari)}
  .durum.yuksek{background:color-mix(in srgb,var(--kotu) 15%,transparent);color:var(--kotu)}
  .alarm{display:none;margin:14px 0;padding:12px 16px;border-radius:12px;border:1px solid var(--kotu);
         background:color-mix(in srgb,var(--kotu) 10%,var(--kart));align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap}
  .alarm.goster{display:flex}
  .bolum{margin-top:18px}
  .ust{display:flex;justify-content:space-between;align-items:center;gap:10px;flex-wrap:wrap;margin-bottom:10px}
  .secim{display:flex;background:var(--kart);border:1px solid var(--cizgi);border-radius:10px;padding:3px}
  .secim button{background:transparent;color:var(--soluk);padding:6px 12px}
  .secim button.secili{background:var(--yazi);color:var(--bg)}
  button,.buton{font:inherit;border:0;border-radius:9px;padding:9px 14px;cursor:pointer;background:var(--yazi);color:var(--bg);font-weight:600;text-decoration:none;display:inline-block}
  .ikincil{background:transparent;color:var(--yazi);border:1px solid var(--cizgi)}
  .grafik{position:relative;height:260px}
  .bos{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;color:var(--soluk);text-align:center;padding:20px;pointer-events:none}
  label{display:block;font-size:.85rem;color:var(--soluk);margin-bottom:4px}
  input{font:inherit;width:100%;padding:8px 10px;border-radius:8px;border:1px solid var(--cizgi);background:var(--bg);color:var(--yazi)}
  .form{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:12px}
  .mesaj{font-size:.88rem;margin-left:10px}
</style>
</head>
<body>
<main>
  <header>
    <h1>Sıcaklık & Nem Kayıtları</h1>
    <span class="rozet" id="sonOlcum">Yükleniyor…</span>
  </header>

  <div class="izgara">
    <div class="kart sicak">
      <div class="etiket">Sıcaklık</div>
      <div class="deger" id="sicaklik">--</div>
      <div class="alt">Eşik: <span id="sEsikYazi">--</span> °C</div>
      <span class="durum" id="sDurum"></span>
    </div>
    <div class="kart nem">
      <div class="etiket">Nem</div>
      <div class="deger" id="nem">--</div>
      <div class="alt">Eşik: %<span id="nEsikYazi">--</span></div>
      <span class="durum" id="nDurum"></span>
    </div>
  </div>

  <div class="alarm" id="alarmKutu">
    <span>🚨 <b id="alarmYazi">Üst eşik aşıldı!</b></span>
    <button onclick="sustur()">Alarmı sustur</button>
  </div>

  <div class="kart bolum">
    <div class="ust">
      <h2 style="margin:0">Geçmiş ölçümler</h2>
      <div class="secim">
        <button data-saat="24" class="secili">24 saat</button>
        <button data-saat="168">7 gün</button>
        <button data-saat="0">Tümü</button>
      </div>
    </div>
    <div class="etiket" style="margin:6px 0">Sıcaklık (°C)</div>
    <div class="grafik"><canvas id="gSicaklik"></canvas><div class="bos" id="bos1"></div></div>
    <div class="etiket" style="margin:16px 0 6px">Nem (%)</div>
    <div class="grafik"><canvas id="gNem"></canvas><div class="bos" id="bos2"></div></div>
    <div class="ust" style="margin:14px 0 0">
      <span class="alt" id="kayitSayisi"></span>
      <a class="buton ikincil" href="/olcumler.csv" download="olcumler.csv">⬇ CSV indir</a>
    </div>
  </div>

  <div class="kart bolum">
    <h2>Eşik değerleri</h2>
    <div class="form">
      <div><label for="sAlt">Sıcaklık alt (°C)</label><input id="sAlt" type="number" step="0.5"></div>
      <div><label for="sUst">Sıcaklık üst (°C)</label><input id="sUst" type="number" step="0.5"></div>
      <div><label for="nAlt">Nem alt (%)</label><input id="nAlt" type="number" step="1"></div>
      <div><label for="nUst">Nem üst (%)</label><input id="nUst" type="number" step="1"></div>
    </div>
    <div style="margin-top:12px">
      <button onclick="esikKaydet()">Kaydet</button><span class="mesaj" id="esikMesaj"></span>
    </div>
  </div>
</main>

<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.js"
        onerror="this.onerror=null;this.src='https://cdnjs.cloudflare.com/ajax/libs/Chart.js/4.4.1/chart.umd.js'"></script>
<script>
const $ = id => document.getElementById(id);
let durum = null, olcumler = [], gorunen = [], aralikSaat = 24, esiklerDolu = false;
let grafikS = null, grafikN = null;

const iki = n => String(n).padStart(2, "0");
const tamZaman = d => `${iki(d.getDate())}.${iki(d.getMonth()+1)}.${d.getFullYear()} ${iki(d.getHours())}:${iki(d.getMinutes())}`;
const kisaZaman = d => aralikSaat && aralikSaat <= 24
  ? `${iki(d.getHours())}:${iki(d.getMinutes())}`
  : `${iki(d.getDate())}.${iki(d.getMonth()+1)} ${iki(d.getHours())}:${iki(d.getMinutes())}`;

// ---- Anlık durum ----
async function durumYukle(){
  try{
    durum = await (await fetch("/api/durum?t=" + Date.now())).json();
    durumCiz();
    grafikCiz();   // eşik çizgileri güncel kalsın
  }catch(e){ $("sonOlcum").textContent = "ESP32'ye ulaşılamıyor"; }
}

function rozet(el, yuksek, dusuk){
  el.className = "durum " + (yuksek ? "yuksek" : dusuk ? "dusuk" : "normal");
  el.textContent = yuksek ? "Yüksek!" : dusuk ? "Düşük" : "Normal";
}

function durumCiz(){
  const d = durum;
  $("sicaklik").textContent = d.sicaklik != null ? d.sicaklik.toFixed(1) + "°C" : "--";
  $("nem").textContent      = d.nem != null ? "%" + d.nem.toFixed(0) : "--";
  $("sEsikYazi").textContent = `${d.sicAlt} – ${d.sicUst}`;
  $("nEsikYazi").textContent = `${d.nemAlt} – ${d.nemUst}`;
  rozet($("sDurum"), d.sicYuksek, d.sicDusuk);
  rozet($("nDurum"), d.nemYuksek, d.nemDusuk);
  $("sonOlcum").textContent = "ESP32'ye bağlı · " + d.saat;

  const alarm = (d.sicYuksek || d.nemYuksek) && !d.susturuldu;
  $("alarmKutu").classList.toggle("goster", alarm);
  $("alarmYazi").textContent = [d.sicYuksek && "Sıcaklık", d.nemYuksek && "Nem"].filter(Boolean).join(" ve ") + " üst eşiği aştı!";

  if(!esiklerDolu){   // kutuları sadece ilk açılışta doldur (yazarken silinmesin)
    $("sAlt").value = d.sicAlt; $("sUst").value = d.sicUst;
    $("nAlt").value = d.nemAlt; $("nUst").value = d.nemUst;
    esiklerDolu = true;
  }
}

// ---- Kayıtlar (CSV) ----
async function kayitYukle(){
  try{
    const metin = await (await fetch("/olcumler.csv?t=" + Date.now())).text();
    olcumler = metin.trim().split("\n").slice(1).map(satir => {
      const p = satir.split(",");
      return { z: new Date(p[0].replace(" ", "T")), s: parseFloat(p[1]), n: parseFloat(p[2]) };
    }).filter(o => !isNaN(o.z) && !isNaN(o.s) && !isNaN(o.n));
    grafikCiz();
  }catch(e){}
}

function grafikOlustur(canvas, renk, birim){
  const stil = getComputedStyle(document.body);
  const soluk = stil.getPropertyValue("--soluk").trim(), cizgi = stil.getPropertyValue("--cizgi").trim();
  const esikCizgisi = (etiket, r) => ({ label: etiket, data: [], borderColor: r, borderWidth: 1.5,
                                        borderDash: [6, 4], pointRadius: 0, pointHoverRadius: 0, fill: false });
  return new Chart(canvas, {
    type: "line",
    data: { labels: [], datasets: [
      { label: "Ölçüm", data: [], borderColor: renk, backgroundColor: renk, borderWidth: 2,
        pointRadius: 3, pointHoverRadius: 6, tension: 0.25 },
      esikCizgisi("Üst eşik", "#e03131"),
      esikCizgisi("Alt eşik", "#f08c00")
    ]},
    options: {
      responsive: true, maintainAspectRatio: false, animation: false,
      interaction: { mode: "index", intersect: false },
      scales: {
        x: { ticks: { color: soluk, maxTicksLimit: 8, maxRotation: 0 }, grid: { color: cizgi } },
        y: { ticks: { color: soluk }, grid: { color: cizgi } }
      },
      plugins: {
        legend: { labels: { color: soluk, boxWidth: 14 } },
        tooltip: {
          filter: item => item.datasetIndex === 0,   // sadece ölçüm noktasını göster
          callbacks: {
            title: items => "🕒 " + tamZaman(gorunen[items[0].dataIndex].z),
            label: item => " " + item.formattedValue + " " + birim
          }
        }
      }
    }
  });
}

function grafikCiz(){
  if(typeof Chart === "undefined"){
    $("bos1").textContent = $("bos2").textContent = "Grafik kütüphanesi yüklenemedi. Telefon/bilgisayarın internete bağlı olmalı.";
    return;
  }
  if(!grafikS){
    grafikS = grafikOlustur($("gSicaklik"), "#e8590c", "°C");
    grafikN = grafikOlustur($("gNem"), "#1c7ed6", "%");
  }

  // Seçilen aralığı en son kayda göre filtrele
  const son = olcumler.length ? olcumler[olcumler.length - 1].z.getTime() : Date.now();
  gorunen = aralikSaat ? olcumler.filter(o => o.z.getTime() >= son - aralikSaat * 3600e3) : olcumler;

  const bosYazi = gorunen.length ? "" : "Henüz kayıt yok. İlk kayıt bir sonraki 10 dakikanın başında alınacak.";
  $("bos1").textContent = $("bos2").textContent = bosYazi;
  $("kayitSayisi").textContent = `Gösterilen: ${gorunen.length} ölçüm · Toplam: ${olcumler.length}`;

  const etiketler = gorunen.map(o => kisaZaman(o.z));
  const doldur = (g, alan, alt, ust) => {
    g.data.labels = etiketler;
    g.data.datasets[0].data = gorunen.map(o => o[alan]);
    g.data.datasets[1].data = gorunen.map(() => ust);
    g.data.datasets[2].data = gorunen.map(() => alt);
    g.update();
  };
  doldur(grafikS, "s", durum?.sicAlt, durum?.sicUst);
  doldur(grafikN, "n", durum?.nemAlt, durum?.nemUst);
}

// ---- Eylemler ----
async function esikKaydet(){
  const govde = { sicAlt: +$("sAlt").value, sicUst: +$("sUst").value, nemAlt: +$("nAlt").value, nemUst: +$("nUst").value };
  try{
    const r = await fetch("/api/esik", { method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify(govde) });
    const cevap = await r.json();
    $("esikMesaj").textContent = r.ok ? "✅ Kaydedildi" : "❌ " + cevap.hata;
    $("esikMesaj").style.color = r.ok ? "var(--iyi)" : "var(--kotu)";
  }catch(e){ $("esikMesaj").textContent = "❌ ESP32'ye ulaşılamadı"; }
  durumYukle();
}

async function sustur(){
  await fetch("/api/sustur", { method: "POST" });
  durumYukle();
}

document.querySelectorAll(".secim button").forEach(b => b.onclick = () => {
  document.querySelectorAll(".secim button").forEach(x => x.classList.remove("secili"));
  b.classList.add("secili");
  aralikSaat = +b.dataset.saat;
  grafikCiz();
});

durumYukle().then(kayitYukle);
setInterval(durumYukle, 10000);   // anlık değerler: 10 sn'de bir
setInterval(kayitYukle, 60000);   // grafik: dakikada bir
</script>
</body>
</html>
)rawliteral";


// ===============================================================
// GLOBAL DEĞİŞKENLER
// ===============================================================
DHT dht(DHT_PIN, DHT_TYPE);
Preferences ayarlar;
WebServer server(80);

float sicAlt, sicUst, nemAlt, nemUst;
float sonSicaklik = NAN, sonNem = NAN;

bool sicYuksek = false, nemYuksek = false;
bool sicDusuk  = false, nemDusuk  = false;
bool susturuldu = false;

int  sonKayitDilimi = -1;
bool saatHazir = false;
String sonKayitDakikasi = "";   // dosyadaki son kaydın dakikası, örn. "2026-10-18 03:10"

// Telegram işleri ayrı bir görevde (Çekirdek 0) yapılıyor. Ana döngüyle iki
// "kuyruk" üzerinden haberleşiyorlar: biri giden mesajlar, biri gelen komutlar.
struct GidenMesaj { char chat[24]; char metin[700]; };
struct GelenKomut { char chat[24]; char metin[160]; };
QueueHandle_t gidenKuyruk, gelenKuyruk;


// ===============================================================
// YARDIMCI FONKSİYONLAR
// ===============================================================
void wifiBaglan() {
  Serial.printf("WiFi'a baglaniliyor: %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  // Wi-Fi verici gücünü düşür: telefon hotspot'u yakında olduğu için yeterli, ama ESP32'nin
  // veri gönderirken çektiği anlık akım azalır (zayıf USB'de brownout'u önler)
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  unsigned long baslangic = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - baslangic < 20000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nBaglandi! IP: %s\n", WiFi.localIP().toString().c_str());
    MDNS.end();
    if (MDNS.begin(CIHAZ_ADI)) MDNS.addService("http", "tcp", 80);
    Serial.printf(">>> Web sayfasi: http://%s  veya  http://%s.local\n",
                  WiFi.localIP().toString().c_str(), CIHAZ_ADI);
  } else {
    Serial.println("\nWiFi baglantisi basarisiz, sonra tekrar denenecek.");
  }
}

bool saatiAyarla() {
  Serial.print("Saat aliniyor");
  configTzTime(SAAT_DILIMI, "pool.ntp.org", "time.google.com");
  struct tm zaman;
  for (int i = 0; i < 20; i++) {
    if (getLocalTime(&zaman, 500)) {
      Serial.println(" OK");
      return true;
    }
    Serial.print(".");
  }
  Serial.println("\nSaat alinamadi.");
  return false;
}

String zamanYazisi(const struct tm& zaman) {
  char tampon[20];
  strftime(tampon, sizeof(tampon), "%Y-%m-%d %H:%M:%S", &zaman);
  return String(tampon);
}

String kisaZaman() {
  struct tm zaman;
  if (!getLocalTime(&zaman, 50)) return "";
  char tampon[20];
  strftime(tampon, sizeof(tampon), "%d.%m.%Y %H:%M", &zaman);
  return String(tampon);
}

String panelAdresi() {
  return "http://" + WiFi.localIP().toString();
}

bool sensorOku(float& sicaklik, float& nem) {
  for (int deneme = 0; deneme < 3; deneme++) {
    sicaklik = dht.readTemperature();
    nem = dht.readHumidity();
    if (!isnan(sicaklik) && !isnan(nem)) return true;
    delay(2000);
  }
  return false;
}


// ===============================================================
// TELEGRAM (ayrı görev, Çekirdek 0)
// ===============================================================

// Ana döngüden çağrılır: mesajı kuyruğa koyar ve HEMEN döner (internet beklenmez)
void telegramGonder(const String& metin, const char* hedefChat = nullptr) {
  const char* chat = hedefChat ? hedefChat : CHAT_ID;
  if (strlen(chat) == 0 || strlen(BOT_TOKEN) < 20) return;
  GidenMesaj m;
  strlcpy(m.chat, chat, sizeof(m.chat));
  strlcpy(m.metin, metin.c_str(), sizeof(m.metin));
  if (xQueueSend(gidenKuyruk, &m, 0) != pdTRUE) Serial.println("Telegram kuyrugu dolu, mesaj atlandi.");
}

// Telegram görevinin kullandığı fonksiyonlar ----------------------
bool tgGonder(WiFiClientSecure& client, const GidenMesaj& m) {
  JsonDocument doc;
  doc["chat_id"] = m.chat;
  doc["text"] = m.metin;
  JsonArray tuslar = doc["reply_markup"]["keyboard"].to<JsonArray>();
  JsonArray satir1 = tuslar.add<JsonArray>();
  satir1.add("/durum"); satir1.add("/esikler");
  JsonArray satir2 = tuslar.add<JsonArray>();
  satir2.add("/sustur"); satir2.add("/panel");
  doc["reply_markup"]["resize_keyboard"] = true;
  String govde;
  serializeJson(doc, govde);

  HTTPClient http;
  http.begin(client, String("https://api.telegram.org/bot") + BOT_TOKEN + "/sendMessage");
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(8000);
  int kod = http.POST(govde);
  http.end();
  if (kod != 200) Serial.printf("Telegram gonderme hatasi: %d\n", kod);
  return kod == 200;
}

// Telegram'a GET isteği at, cevabı JSON olarak döndür
bool tgOku(WiFiClientSecure& client, const String& yol, JsonDocument& doc) {
  HTTPClient http;
  http.begin(client, String("https://api.telegram.org/bot") + BOT_TOKEN + yol);
  http.setTimeout(8000);
  int kod = http.GET();
  bool ok = false;
  if (kod == 200) ok = !deserializeJson(doc, http.getString());
  else if (kod == 401) Serial.println("Telegram: BOT_TOKEN hatali!");
  else if (kod == 409) Serial.println("Telegram: Bu bot baska bir yerde de calisiyor (409)!");
  else if (kod > 0) Serial.printf("Telegram okuma hatasi: %d\n", kod);
  http.end();
  return ok;
}

void telegramGorevi(void* parametre) {
  WiFiClientSecure client;
  client.setInsecure();   // HTTPS sertifika doğrulamasını atla (prototip için yeterli)
  long offset = 0;
  bool eskilerAtlandi = false;
  unsigned long sonOkuma = 0;
  static GidenMesaj giden;   // büyük olduğu için static (görev belleğini doldurmasın)

  for (;;) {
    if (WiFi.status() != WL_CONNECTED || strlen(BOT_TOKEN) < 20) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    // Açılışta: ESP32 kapalıyken birikmiş eski mesajları atla
    if (!eskilerAtlandi) {
      JsonDocument doc;
      if (tgOku(client, "/getUpdates?offset=-1&limit=1", doc)) {
        if (doc["result"].size() > 0) offset = doc["result"][0]["update_id"].as<long>() + 1;
        eskilerAtlandi = true;
      } else {
        vTaskDelay(pdMS_TO_TICKS(3000));
        continue;
      }
    }

    // 1) Kuyrukta bekleyen mesajları gönder
    while (xQueueReceive(gidenKuyruk, &giden, 0) == pdTRUE) tgGonder(client, giden);

    // 2) Yeni gelen mesajları oku
    if (millis() - sonOkuma >= TELEGRAM_ARALIGI_MS) {
      sonOkuma = millis();
      JsonDocument doc;
      if (tgOku(client, "/getUpdates?timeout=0&limit=5&offset=" + String(offset), doc)) {
        for (JsonObject g : doc["result"].as<JsonArray>()) {
          offset = g["update_id"].as<long>() + 1;
          const char* metin = g["message"]["text"];
          if (!metin) continue;

          GelenKomut k;
          snprintf(k.chat, sizeof(k.chat), "%lld", g["message"]["chat"]["id"].as<long long>());
          strlcpy(k.metin, metin, sizeof(k.metin));

          // Güvenlik: sadece senin chat'inden gelen komutları kabul et
          if (strcmp(k.chat, CHAT_ID) != 0) {
            Serial.printf("Yetkisiz kisiden mesaj (chat id: %s), yok sayildi.\n", k.chat);
            continue;
          }
          xQueueSend(gelenKuyruk, &k, 0);   // komutu ana döngüye ilet
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}


// ===============================================================
// EŞİKLER
// ===============================================================
void esikleriYukle() {
  ayarlar.begin("esikler", true);
  sicAlt = ayarlar.getFloat("sicAlt", VARSAYILAN_SIC_ALT);
  sicUst = ayarlar.getFloat("sicUst", VARSAYILAN_SIC_UST);
  nemAlt = ayarlar.getFloat("nemAlt", VARSAYILAN_NEM_ALT);
  nemUst = ayarlar.getFloat("nemUst", VARSAYILAN_NEM_UST);
  ayarlar.end();
}

void esikleriKaydet() {
  ayarlar.begin("esikler", false);
  ayarlar.putFloat("sicAlt", sicAlt);
  ayarlar.putFloat("sicUst", sicUst);
  ayarlar.putFloat("nemAlt", nemAlt);
  ayarlar.putFloat("nemUst", nemUst);
  ayarlar.end();
}

String esikMetni() {
  char tampon[120];
  snprintf(tampon, sizeof(tampon),
           "🌡 Sıcaklık: %.1f – %.1f °C\n💧 Nem: %%%.0f – %%%.0f",
           sicAlt, sicUst, nemAlt, nemUst);
  return String(tampon);
}

// Yeni eşikleri kontrol et. Sorun varsa hata mesajı döndürür, yoksa boş.
String esikHatasi(float sa, float su, float na, float nu) {
  if (isnan(sa) || isnan(su) || isnan(na) || isnan(nu)) return "Geçersiz sayı.";
  if (sa >= su) return "Sıcaklık alt eşiği, üst eşikten küçük olmalı.";
  if (na >= nu) return "Nem alt eşiği, üst eşikten küçük olmalı.";
  if (sa < -10 || su > 60) return "Sıcaklık -10 ile 60 arasında olmalı.";
  if (na < 0 || nu > 100) return "Nem 0 ile 100 arasında olmalı.";
  return "";
}

void alarmlariGuncelle();   // aşağıda tanımlı

void esikleriUygula(float sa, float su, float na, float nu) {
  sicAlt = sa; sicUst = su; nemAlt = na; nemUst = nu;
  esikleriKaydet();
  alarmlariGuncelle();   // yeni eşiklere göre hemen kontrol et
}


// ===============================================================
// ALARM
// ===============================================================
void bildir(const String& metin) {
  Serial.println(metin);
  telegramGonder(metin);
}

void alarmlariGuncelle() {
  if (isnan(sonSicaklik) || isnan(sonNem)) return;

  bool eskiSicYuksek = sicYuksek, eskiNemYuksek = nemYuksek;
  bool eskiSicDusuk  = sicDusuk,  eskiNemDusuk  = nemDusuk;

  if (sonSicaklik > sicUst)                     sicYuksek = true;
  else if (sonSicaklik <= sicUst - SIC_TAMPON)  sicYuksek = false;
  if (sonNem > nemUst)                          nemYuksek = true;
  else if (sonNem <= nemUst - NEM_TAMPON)       nemYuksek = false;
  if (sonSicaklik < sicAlt)                     sicDusuk = true;
  else if (sonSicaklik >= sicAlt + SIC_TAMPON)  sicDusuk = false;
  if (sonNem < nemAlt)                          nemDusuk = true;
  else if (sonNem >= nemAlt + NEM_TAMPON)       nemDusuk = false;

  if (sicYuksek && !eskiSicYuksek) bildir("🚨 ALARM: Sıcaklık yüksek! " + String(sonSicaklik, 1) + " °C (üst eşik " + String(sicUst, 1) + ")");
  if (!sicYuksek && eskiSicYuksek) bildir("✅ Sıcaklık normale döndü: " + String(sonSicaklik, 1) + " °C");
  if (nemYuksek && !eskiNemYuksek) bildir("🚨 ALARM: Nem yüksek! %" + String(sonNem, 0) + " (üst eşik %" + String(nemUst, 0) + ")");
  if (!nemYuksek && eskiNemYuksek) bildir("✅ Nem normale döndü: %" + String(sonNem, 0));
  if (sicDusuk && !eskiSicDusuk)   bildir("⚠️ UYARI: Sıcaklık düşük. " + String(sonSicaklik, 1) + " °C (alt eşik " + String(sicAlt, 1) + ")");
  if (!sicDusuk && eskiSicDusuk)   bildir("✅ Sıcaklık alt eşiğin üstüne çıktı: " + String(sonSicaklik, 1) + " °C");
  if (nemDusuk && !eskiNemDusuk)   bildir("⚠️ UYARI: Nem düşük. %" + String(sonNem, 0) + " (alt eşik %" + String(nemAlt, 0) + ")");
  if (!nemDusuk && eskiNemDusuk)   bildir("✅ Nem alt eşiğin üstüne çıktı: %" + String(sonNem, 0));

  if (!sicYuksek && !nemYuksek) susturuldu = false;
}

// Buzzer (loop'u bekletmeden): 3 kısa bip + 5 sn sessizlik
void buzzerYaz(bool ac) {
  if (BUZZER_PASIF) {
    if (ac) tone(BUZZER_PIN, 2000); else noTone(BUZZER_PIN);
  } else {
    digitalWrite(BUZZER_PIN, ac ? HIGH : LOW);
  }
}

void buzzerGuncelle() {
  static bool sonDurum = false;
  bool calmali = (sicYuksek || nemYuksek) && !susturuldu && !SESSIZ_MOD;
  bool ac = false;
  if (calmali) {
    unsigned long t = millis() % 6000;
    ac = (t < 1000) && ((t / 200) % 2 == 0);
  }
  if (ac != sonDurum) {
    buzzerYaz(ac);
    sonDurum = ac;
  }
}


// ===============================================================
// ÖLÇÜM VE KAYIT
// ===============================================================
void dosyayiHazirla() {
  if (!DOSYA_SISTEMI.exists(KAYIT_DOSYASI)) {
    File f = DOSYA_SISTEMI.open(KAYIT_DOSYASI, FILE_WRITE);
    if (f) {
      f.println("tarih,sicaklik,nem");
      f.close();
      Serial.println("Yeni kayit dosyasi olusturuldu.");
    }
  }
}

// Dosyadaki son kaydın dakikasını oku. Böylece ESP32 yeniden başlasa bile
// aynı dakikaya ikinci bir kayıt yazmıyoruz.
void sonKaydiOku() {
  sonKayitDakikasi = "";
  File f = DOSYA_SISTEMI.open(KAYIT_DOSYASI);
  if (!f) return;
  size_t boyut = f.size();
  f.seek(boyut > 64 ? boyut - 64 : 0);   // sadece dosyanın sonunu oku
  String son = f.readString();
  f.close();
  son.trim();
  int i = son.lastIndexOf('\n');
  String satir = (i >= 0) ? son.substring(i + 1) : son;
  if (satir.length() >= 16 && isDigit(satir[0])) sonKayitDakikasi = satir.substring(0, 16);
}

bool olcumYap() {
  float t, h;
  if (!sensorOku(t, h)) {
    Serial.println("DHT11 okunamadi! Kablolari kontrol et.");
    return false;
  }
  sonSicaklik = t;
  sonNem = h;
  alarmlariGuncelle();
  return true;
}

String durumMetni(const String& baslik) {
  String m = baslik + " – " + kisaZaman() + "\n";
  if (isnan(sonSicaklik)) return m + "Henüz ölçüm yok.";
  m += "🌡 Sıcaklık: " + String(sonSicaklik, 1) + " °C  (" + String(sicAlt, 0) + "–" + String(sicUst, 0) + ")\n";
  m += "💧 Nem: %" + String(sonNem, 0) + "  (" + String(nemAlt, 0) + "–" + String(nemUst, 0) + ")\n";
  if (sicYuksek) m += "🚨 Sıcaklık yüksek\n";
  if (nemYuksek) m += "🚨 Nem yüksek\n";
  if (sicDusuk)  m += "⚠️ Sıcaklık düşük\n";
  if (nemDusuk)  m += "⚠️ Nem düşük\n";
  if (!sicYuksek && !nemYuksek && !sicDusuk && !nemDusuk) m += "✅ Değerler normal\n";
  if (susturuldu) m += "🔕 Alarm susturuldu\n";
  return m;
}

void olcVeKaydet() {
  struct tm zaman;
  if (!getLocalTime(&zaman, 100)) {
    Serial.println("Saat bilinmiyor, kayit atlandi.");
    return;
  }
  String dakika = zamanYazisi(zaman).substring(0, 16);   // "2026-10-18 03:10"
  if (dakika == sonKayitDakikasi) {
    Serial.println("Bu dakika zaten kaydedilmis, tekrar yazilmadi.");
    return;
  }
  if (!olcumYap()) return;

  String satir = zamanYazisi(zaman) + "," + String(sonSicaklik, 1) + "," + String(sonNem, 0);
  File f = DOSYA_SISTEMI.open(KAYIT_DOSYASI, FILE_APPEND);
  if (!f) {
    Serial.println("HATA: Kayit dosyasi acilamadi!");
    return;
  }
  f.println(satir);
  f.close();
  sonKayitDakikasi = dakika;
  Serial.printf("KAYDEDILDI -> %s\n", satir.c_str());

  telegramGonder(durumMetni("📊 Ölçüm"));
}

void dosyayiListele() {
  File f = DOSYA_SISTEMI.open(KAYIT_DOSYASI);
  if (!f) { Serial.println("Kayit dosyasi yok."); return; }
  Serial.println("----- olcumler.csv -----");
  while (f.available()) Serial.write(f.read());
  Serial.println("------------------------");
  f.close();
}

void hafizaBilgisi() {
  size_t toplam = DOSYA_SISTEMI.totalBytes();
  size_t kullanilan = DOSYA_SISTEMI.usedBytes();
  size_t dosyaBoyutu = 0;
  File f = DOSYA_SISTEMI.open(KAYIT_DOSYASI);
  if (f) { dosyaBoyutu = f.size(); f.close(); }
  Serial.printf("Hafiza: %u / %u KB kullaniliyor (%%%.1f)\n",
                kullanilan / 1024, toplam / 1024, 100.0 * kullanilan / toplam);
  Serial.printf("Kayit dosyasi: %u byte (~%u kayit)\n", dosyaBoyutu, dosyaBoyutu / 28);
}


// ===============================================================
// KOMUTLAR (Telegram ve Serial aynı fonksiyondan geçiyor)
// ===============================================================
const char* YARDIM_METNI =
  "Komutlar:\n"
  "/durum – anlık ölçüm ve alarm durumu\n"
  "/esikler – mevcut eşikler\n"
  "/sicaklik 18 28 – sıcaklık alt ve üst eşik\n"
  "/nem 30 70 – nem alt ve üst eşik\n"
  "/sustur – çalan alarmı sustur\n"
  "/panel – web sayfasının adresi\n"
  "/yardim – bu liste";

void cevapla(const String& metin, const String& chatId) {
  Serial.println(metin);
  if (chatId.length() > 0) telegramGonder(metin, chatId.c_str());
}

void komutIsle(String metin, const String& chatId) {
  metin.trim();
  metin.toLowerCase();
  int at = metin.indexOf('@');   // gruplarda "/durum@botadi" gelebilir
  if (at > 0) {
    int bosluk = metin.indexOf(' ');
    metin = metin.substring(0, at) + (bosluk > at ? metin.substring(bosluk) : "");
  }
  if (!metin.startsWith("/")) metin = "/" + metin;
  if (metin.startsWith("/esik sicaklik")) metin.replace("/esik sicaklik", "/sicaklik");
  if (metin.startsWith("/esik nem"))      metin.replace("/esik nem", "/nem");

  float alt, ust;

  if (metin == "/start" || metin == "/yardim") {
    cevapla(String("Merhaba! 👋 Sıcaklık ve nem takip botu.\n\n") + YARDIM_METNI, chatId);
  }
  else if (metin == "/durum") {
    olcumYap();
    cevapla(durumMetni("📍 Anlık durum"), chatId);
  }
  else if (metin == "/esikler") {
    cevapla("Mevcut eşikler:\n" + esikMetni(), chatId);
  }
  else if (metin == "/panel") {
    cevapla("🌐 Web sayfası (aynı Wi-Fi'dan):\n" + panelAdresi() + "\nveya http://" + CIHAZ_ADI + ".local", chatId);
  }
  else if (metin.startsWith("/sicaklik")) {
    if (sscanf(metin.c_str(), "/sicaklik %f %f", &alt, &ust) == 2) {
      String hata = esikHatasi(alt, ust, nemAlt, nemUst);
      if (hata.length()) cevapla("❌ " + hata, chatId);
      else { esikleriUygula(alt, ust, nemAlt, nemUst); cevapla("✅ Eşikler güncellendi:\n" + esikMetni(), chatId); }
    } else cevapla("Kullanım: /sicaklik 18 28\n(alt eşik, üst eşik)\n\nŞu an:\n" + esikMetni(), chatId);
  }
  else if (metin.startsWith("/nem")) {
    if (sscanf(metin.c_str(), "/nem %f %f", &alt, &ust) == 2) {
      String hata = esikHatasi(sicAlt, sicUst, alt, ust);
      if (hata.length()) cevapla("❌ " + hata, chatId);
      else { esikleriUygula(sicAlt, sicUst, alt, ust); cevapla("✅ Eşikler güncellendi:\n" + esikMetni(), chatId); }
    } else cevapla("Kullanım: /nem 30 70\n(alt eşik, üst eşik)\n\nŞu an:\n" + esikMetni(), chatId);
  }
  else if (metin == "/sustur") {
    if (sicYuksek || nemYuksek) { susturuldu = true; cevapla("🔕 Alarm susturuldu. Değer normale dönene kadar buzzer ötmeyecek.", chatId); }
    else cevapla("Çalan bir alarm yok.", chatId);
  }
  else if (chatId.length() == 0 && metin == "/liste") dosyayiListele();
  else if (chatId.length() == 0 && metin == "/olc")   olcVeKaydet();
  else if (chatId.length() == 0 && metin == "/bilgi") hafizaBilgisi();
  else if (chatId.length() == 0 && metin == "/sil") {
    DOSYA_SISTEMI.remove(KAYIT_DOSYASI);
    dosyayiHazirla();
    sonKayitDakikasi = "";
    Serial.println("Kayitlar silindi. Temiz bir baslangic!");
  }
  else {
    cevapla(String("Bu komutu tanımadım. 🤔\n\n") + YARDIM_METNI, chatId);
  }
}


// ===============================================================
// WEB SUNUCU
// ===============================================================

// "/" -> sayfa
void sayfaGonder() {
  server.send_P(200, "text/html; charset=utf-8", SAYFA_HTML);
}

// "/olcumler.csv" -> kayıt dosyasının kendisi (grafik + indirme için)
void csvGonder() {
  File f = DOSYA_SISTEMI.open(KAYIT_DOSYASI);
  if (!f) { server.send(404, "text/plain", "Kayit yok"); return; }
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(f, "text/csv");
  f.close();
}

// "/api/durum" -> anlık değerler, eşikler, alarm durumu
void durumGonder() {
  JsonDocument doc;
  if (!isnan(sonSicaklik)) doc["sicaklik"] = round(sonSicaklik * 10) / 10.0;
  if (!isnan(sonNem))      doc["nem"]      = round(sonNem);
  doc["sicAlt"] = sicAlt; doc["sicUst"] = sicUst;
  doc["nemAlt"] = nemAlt; doc["nemUst"] = nemUst;
  doc["sicYuksek"] = sicYuksek; doc["nemYuksek"] = nemYuksek;
  doc["sicDusuk"]  = sicDusuk;  doc["nemDusuk"]  = nemDusuk;
  doc["susturuldu"] = susturuldu;
  doc["saat"] = kisaZaman();
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

// "/api/esik" (POST) -> sayfadan gelen yeni eşikler
void esikAl() {
  JsonDocument gelen;
  if (deserializeJson(gelen, server.arg("plain"))) {
    server.send(400, "application/json", "{\"hata\":\"Geçersiz veri\"}");
    return;
  }
  float sa = gelen["sicAlt"] | NAN, su = gelen["sicUst"] | NAN;
  float na = gelen["nemAlt"] | NAN, nu = gelen["nemUst"] | NAN;
  String hata = esikHatasi(sa, su, na, nu);
  if (hata.length()) {
    server.send(400, "application/json", "{\"hata\":\"" + hata + "\"}");
    return;
  }
  esikleriUygula(sa, su, na, nu);
  server.send(200, "application/json", "{\"ok\":true}");
  bildir("🌐 Eşikler web sayfasından güncellendi:\n" + esikMetni());
}

// "/api/sustur" (POST)
void susturAl() {
  if (sicYuksek || nemYuksek) {
    susturuldu = true;
   
    bildir("🔕 Alarm web sayfasından susturuldu.");
  }
  server.send(200, "application/json", "{\"ok\":true}");
}


// ===============================================================
// SETUP & LOOP
// ===============================================================
void serialKontrol() {
  if (!Serial.available()) return;
  String komut = Serial.readStringUntil('\n');
  komut.trim();
  if (komut.length() > 0) komutIsle(komut, "");
}

void setup() {
  // Brownout (düşük voltaj) korumasını kapat: USB gücü sınırdayken ESP32'nin
  // kısa voltaj düşüşlerinde kendini sürekli resetlemesini engeller.
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== SICAKLIK & NEM KAYIT SISTEMI ===");

  pinMode(BUZZER_PIN, OUTPUT);
  buzzerYaz(false);
  dht.begin();
  esikleriYukle();

  if (!DOSYA_SISTEMI.begin(true)) {
    Serial.println("HATA: Dosya sistemi baslatilamadi!");
  } else {
    dosyayiHazirla();
    sonKaydiOku();
    hafizaBilgisi();
  }

  // Telegram kuyrukları ve görevi
  gidenKuyruk = xQueueCreate(8, sizeof(GidenMesaj));
  gelenKuyruk = xQueueCreate(5, sizeof(GelenKomut));
  xTaskCreatePinnedToCore(telegramGorevi, "telegram", 12288, NULL, 1, NULL, 0);

  wifiBaglan();
  if (WiFi.status() == WL_CONNECTED) saatHazir = saatiAyarla();

  server.on("/", HTTP_GET, sayfaGonder);
  server.on("/olcumler.csv", HTTP_GET, csvGonder);
  server.on("/api/durum", HTTP_GET, durumGonder);
  server.on("/api/esik", HTTP_POST, esikAl);
  server.on("/api/sustur", HTTP_POST, susturAl);
  server.onNotFound([]() { server.send(404, "text/plain", "Bulunamadi"); });
  server.begin();

  olcumYap();
  telegramGonder(durumMetni("🔌 ESP32 açıldı") + "\n🌐 Web sayfası: " + panelAdresi());
  Serial.println(durumMetni("Durum"));
}

void loop() {
  server.handleClient();
  serialKontrol();
  buzzerGuncelle();

  // Telegram'dan gelen komutları işle
  GelenKomut k;
  while (xQueueReceive(gelenKuyruk, &k, 0) == pdTRUE) {
    Serial.printf("Telegram komutu: %s\n", k.metin);
    komutIsle(String(k.metin), String(k.chat));
  }

  // Wi-Fi / saat yoksa dakikada bir tekrar dene
  static unsigned long sonDeneme = 0;
  if ((WiFi.status() != WL_CONNECTED || !saatHazir) && millis() - sonDeneme > 60000) {
    sonDeneme = millis();
    if (WiFi.status() != WL_CONNECTED) wifiBaglan();
    if (WiFi.status() == WL_CONNECTED && !saatHazir) saatHazir = saatiAyarla();
  }

  // 30 sn'de bir alarm kontrolü
  static unsigned long sonKontrol = 0;
  if (millis() - sonKontrol >= KONTROL_ARALIGI_MS) {
    sonKontrol = millis();
    olcumYap();
  }

  // 10 dakikada bir kayıt + Telegram raporu
  struct tm zaman;
  if (saatHazir && getLocalTime(&zaman, 10)) {
    int dilim = (zaman.tm_hour * 60 + zaman.tm_min) / KAYIT_ARALIGI_DK;
    if (zaman.tm_min % KAYIT_ARALIGI_DK == 0 && dilim != sonKayitDilimi) {
      sonKayitDilimi = dilim;
      olcVeKaydet();
      sonKontrol = millis();
    }
  }

  delay(5);
}
