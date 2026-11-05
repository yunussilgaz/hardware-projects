#include <Arduino.h>

// =====================================================
//  01 - DC Motor + L298N (H köprüsü) - ESP32
//  Serial Monitor komutları (115200 baud):
//    i = ileri   g = geri   d = dur
//    + = hızlan  - = yavaşla
// =====================================================

// ---------- Pinler ----------
const int ENA = 22;   // Hız (PWM)
const int IN1 = 16;   // Yön pini 1
const int IN2 = 17;   // Yön pini 2

// ---------- Hız ayarları ----------
const int MIN_HIZ  = 100;  // Bunun altında motor dönmüyor, sadece akım çekiyordu
const int MAX_HIZ  = 255;  // Tam hız
const int HIZ_ADIM = 25;   // + / - ile değişim miktarı
const int KALKIS_MS = 150; // Duruştan kalkarken tam güç verilen süre

int  hiz = 200;       // Başlangıç hızı (MIN_HIZ..MAX_HIZ)
char yon = 'd';       // 'i' = ileri, 'g' = geri, 'd' = dur

// Yön ve hızı L298N'ye uygular
void motorUygula() {
  if (yon == 'i') {            // İleri: IN1=1, IN2=0
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
  } else if (yon == 'g') {     // Geri: IN1=0, IN2=1
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, HIGH);
  } else {                     // Dur: ikisi de 0
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
  }
  analogWrite(ENA, (yon == 'd') ? 0 : hiz);
}

// Duruştan harekete geçerken kısa bir tam güç "itmesi" verir.
// Düşük hızlarda motorun ilk sürtünmeyi yenip dönmeye başlamasını sağlar.
void kalkisTakviyesi() {
  analogWrite(ENA, MAX_HIZ);
  delay(KALKIS_MS);
}

void durumYaz() {
  const char* ad = (yon == 'i') ? "ILERI" : (yon == 'g') ? "GERI" : "DUR";
  int yuzde = map(hiz, MIN_HIZ, MAX_HIZ, 0, 100);   // Kullanılabilir aralıkta %
  Serial.printf("Yon: %s | Hiz: %d/255 (%%%d)\n", ad, hiz, yuzde);
}

void setup() {
  Serial.begin(115200);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENA, OUTPUT);
  motorUygula();               // Açılışta motor kesinlikle dursun

  Serial.println();
  Serial.println("=== DC Motor Testi ===");
  Serial.println("i = ileri, g = geri, d = dur, + = hizlan, - = yavasla");
  Serial.printf("Hiz araligi: %d - %d\n", MIN_HIZ, MAX_HIZ);
  durumYaz();
}

void loop() {
  if (!Serial.available()) return;   // Komut yoksa bekle
  char c = Serial.read();

  switch (c) {
    case 'i':
    case 'g': {
      if (yon == c) break;           // Zaten o yönde dönüyor
      // Dönerken ters yöne geçiliyorsa önce kısa bir duruş:
      // motoru ve sürücüyü ani yön değişiminden korur
      if (yon != 'd') {
        yon = 'd';
        motorUygula();
        delay(300);
      }
      yon = c;
      motorUygula();                 // Yön pinlerini ayarla
      kalkisTakviyesi();             // Kısa tam güç itmesi
      break;
    }
    case 'd':
      yon = 'd';
      break;
    case '+':
      hiz = min(MAX_HIZ, hiz + HIZ_ADIM);
      if (hiz == MAX_HIZ) Serial.println("(Maksimum hiz)");
      break;
    case '-':
      hiz = max(MIN_HIZ, hiz - HIZ_ADIM);
      if (hiz == MIN_HIZ) Serial.println("(Minimum hiz - daha asagisinda motor donmuyor)");
      break;
    default:
      return;                        // Enter vb. karakterleri yok say
  }
  motorUygula();
  durumYaz();
}
