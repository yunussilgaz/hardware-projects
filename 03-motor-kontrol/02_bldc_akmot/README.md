# 03.2 · BLDC Motor + AKMOT Sürücü (devam ediyor)

Roboweb RW-ML-1649 redüktörlü fırçasız motor (50 RPM, 100:1, 45 mm, hall sensörlü) ve AKMOT BLDC motor sürücü (DC 8–30 V, 5 A).

## Sürücü pinleri
| Pin | İsim | Bağlantı |
|---|---|---|
| 1 / 2 | VCC / GND | Güç kaynağı (12 V, 1 A sınır) |
| 3 / 4 / 5 | MOTOR MA / MB / MC | Faz kabloları: mavi / sarı / beyaz |
| 6 | GND | Sensör GND: siyah |
| 7 / 8 / 9 | HALL HA / HB / HC | Sensör: sarı / beyaz / mavi |
| 10 | +5V | Sensör beslemesi: kırmızı |
| 11 | GND | Kontrol GND |
| 12 | DIRECTION | Yön |
| 13 | ON/OFF | Boşta 5 V; GND'ye bağlanınca çalış |
| 14 | SPEED 0–5V | Hız |
| 15 | +5V | Hız için 5 V çıkışı |

Faz kabloları multimetreyle bulundu: üç faz kablosu birbirine düşük dirençle bağlıdır, sensör kabloları bağlı değildir.

## Durum
- ✅ Sürücü 12 V alıyor, kendi 5 V'unu üretiyor (motor bağlıyken ~4,4 V)
- ✅ SPEED girişi 5 V'ta, ON/OFF hem boşta hem GND'de denendi
- ❌ Motor dönmüyor, sürücü yalnızca ~0,04 A çekiyor
- ⚠️ Şüpheli: HALL HC girişi ~2 V gösteriyor (HA ve HB ~0 V). Sürücü geçersiz sensör sinyali gördüğü için motoru sürmüyor olabilir.

## Sonraki adımlar
1. HA ve HC sensör kablolarını yer değiştirip 2 V'un kabloyla mı yoksa sürücü girişiyle mi taşındığını kontrol etmek
2. Sensör sorunu çözülürse faz sırasını denemek
3. ESP32 ile kontrol: ON/OFF ve DIRECTION için dijital pin, SPEED için PWM + RC filtre (ESP32 en fazla 3,3 V verir)
