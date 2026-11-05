# 01 · Akıllı Sera (Sulama + Klima)

Toprak nemi azalınca "sulama", sıcaklık eşiği aşılınca "klima" çalışır (bu projede LED'lerle temsil ediliyor). Sistem hem internetten hem de yerel ağdan izlenip kontrol edilebilir.

## Özellikler
- **Sensörler:** toprak nem sensörü (analog, kalibrasyonlu), DHT11 (sıcaklık + hava nemi)
- **Otomatik / manuel mod:** otomatikte ESP32 eşiklere göre karar verir, manuelde cihazlar panelden açılıp kapatılır
- **Uzaktan panel** (`web/index.html`): Firebase Realtime Database üzerinden çalışır, Netlify gibi bir statik barındırmada yayınlanabilir
- **Yerel panel:** ESP32 kendi IP adresinde ve `http://sera.local` adresinde aynı paneli yayınlar
- İki çekirdek: Firebase iletişimi çekirdek 0'da, sensör okuma ve LED kontrolü çekirdek 1'de

## Bağlantılar
| Parça | ESP32 pini |
|---|---|
| DHT11 DATA | GPIO 4 |
| Toprak nem sensörü AO | GPIO 34 (ADC1; Wi-Fi açıkken ADC2 kullanılamaz) |
| Klima LED'i (+ 220 Ω direnç) | GPIO 26 |
| Sulama LED'i (+ 220 Ω direnç) | GPIO 27 |

## Veritabanı yapısı
```
/sensor  -> { sicaklik, nem, hamNem, havaNem, zaman }   ESP32 yazar, panel okur
/kontrol -> { mod, klima, sulama, sicaklikEsik, nemEsik } panel yazar, ESP32 okur
/durum   -> { klima, sulama }                            LED'lerin gerçek durumu
```

## Kurulum
- `include/secrets.example.h` → `include/secrets.h` olarak kopyala; Wi-Fi bilgilerini ve Firebase adresini yaz.
- `web/index.html` içindeki `FIREBASE_URL` değerini kendi Firebase adresinle değiştir.
