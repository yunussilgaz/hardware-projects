# 02 · Sıcaklık & Nem Kayıt Sistemi

DHT11 ile 10 dakikada bir sıcaklık ve nem ölçer, ölçümleri ESP32'nin kendi hafızasına CSV olarak kaydeder ve bir web sayfasında grafikle gösterir. Eşik aşımlarında buzzer ve Telegram ile uyarır.

## Özellikler
- **Kayıt:** her 10 dakikada bir `/olcumler.csv` (LittleFS). İnternet kesilse de kayıt devam eder; zaman NTP ile alınır.
- **Yerel web sayfası** (`http://kayit.local` ya da ESP32'nin IP adresi):
  - Chart.js ile sıcaklık ve nem grafikleri; noktanın üstüne gelince ölçüm zamanı görünür
  - Alt/üst eşik çizgileri, eşikleri sayfadan değiştirme, alarmı susturma
  - CSV dosyasını indirme
- **Eşikler:** sıcaklık ve nem için alt/üst eşik, histerezisli; ESP32 kapansa da hafızada (Preferences) saklanır
- **Buzzer:** üst eşik aşılınca öter (gece için sessiz mod var)
- **Telegram botu:** her ölçümde rapor, alarm/uyarı mesajları ve komutlar  
  `/durum` `/esikler` `/sicaklik 18 28` `/nem 30 70` `/sustur` `/panel` `/yardim`
- Telegram iletişimi ayrı bir FreeRTOS görevinde (çekirdek 0) çalışır, ölçüm ve web sayfası yavaşlamaz

## Bağlantılar
| Parça | ESP32 pini |
|---|---|
| DHT11 DATA | GPIO 4 |
| Buzzer (+) | GPIO 25 |

## Sonuç
Bir gecelik testte 52 kayıt alındı (sıcaklık 18,0 → 24,3 °C, nem %28–38).

## Kurulum
- `include/secrets.example.h` → `include/secrets.h` olarak kopyala; Wi-Fi bilgilerini, Telegram bot token'ını (@BotFather) ve chat ID'ni yaz.
