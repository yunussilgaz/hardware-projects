# ESP32 Staj Projeleri

ESP32 ve PlatformIO (Arduino framework) ile yaptığım staj çalışmaları. Her proje kendi klasöründe, ayrı bir PlatformIO projesidir.

| Klasör | Proje | Durum |
|---|---|---|
| [01-akilli-sera](01-akilli-sera) | Toprak nemi ve sıcaklığa göre sulama/klima kontrolü; Firebase üzerinden uzaktan panel + ESP32 üzerinde yerel panel | ✅ Tamamlandı |
| [02-sicaklik-nem-kayit](02-sicaklik-nem-kayit) | 10 dakikada bir ölçüm kaydı (LittleFS/CSV), grafikli yerel web sayfası, eşik alarmı, Telegram botu | ✅ Tamamlandı |
| [03-motor-kontrol/01_dc_motor_l298n](03-motor-kontrol/01_dc_motor_l298n) | DC motoru L298N H köprüsü ile yön ve hız (PWM) kontrolü | ✅ Tamamlandı |
| [03-motor-kontrol/02_bldc_akmot](03-motor-kontrol/02_bldc_akmot) | Fırçasız (BLDC) motor + AKMOT sürücü | 🔧 Devam ediyor |

## Kullanılan donanım
- ESP32 DevKit (ESP-WROOM-32)
- DHT11 sıcaklık/nem sensörü, toprak nem sensörü, LED'ler, buzzer
- L298N motor sürücü, DC motor
- AKMOT BLDC motor sürücü (DC 8–30 V), Roboweb RW-ML-1649 redüktörlü fırçasız motor
- Laboratuvar güç kaynağı, multimetre

## Kurulum
1. VS Code + PlatformIO eklentisini kur.
2. **File → Open Folder** ile çalıştırmak istediğin proje klasörünü aç (ana klasörü değil).
3. Wi-Fi / Telegram / Firebase kullanan projelerde `include/secrets.example.h` dosyasını **`include/secrets.h`** adıyla kopyala ve kendi bilgilerinle doldur. `secrets.h` `.gitignore` içinde olduğu için GitHub'a yüklenmez.
4. **Upload** ile ESP32'ye yükle, **Monitor** ile Serial çıktısını izle (115200 baud).
