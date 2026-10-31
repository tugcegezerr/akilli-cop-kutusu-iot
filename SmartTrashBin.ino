#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

// --- PİN TANIMLAMALARI ---
const int SEVIYE_TRIG = 18;  // 1. Sensör (İçerideki - Çöp Seviyesi)
const int SEVIYE_ECHO = 5;
const int EL_TRIG = 17;      // 2. Sensör (Dışarıdaki - El Algılama)
const int EL_ECHO = 16;

const int SERVO_PIN = 19;    // Servo Motor
const int LED_YESIL = 2;     // D2 Pini (Yeşil LED)
const int LED_KIRMIZI = 4;   // D4 Pini (Kırmızı LED)

// --- PROJE VE CİHAZ AYARLARI ---
const int COP_KUTUSU_BOYUTU = 30;   // Maket çöp kutusu boyutu (cm)
const int DOLULUK_ESIGI = 80;       // %80 ve üzeri dolu kabul edilir
const int EL_ALGILAMA_MESAFESI = 5;   // El algılama eşiği 5 cm

// --- WI-FI VE THINGSBOARD AYARLARI ---
const char* WIFI_SSID = "BURAYA_WIFI_ADINIZI_GIRIN";         
const char* WIFI_PASSWORD = "BURAYA_WIFI_SIFRENIZI_GIRIN";     

const char* TB_SERVER = "mqtt.thingsboard.cloud";
const int TB_PORT = 1883;
const char* TB_TOKEN = "BURAYA_THINGSBOARD_TOKEN_GIRIN"; 

// --- NESNELER VE YARDIMCI DEĞİŞKENLER ---
Servo kapakServosu;
WiFiClient espClient;
PubSubClient client(espClient);

unsigned long sonVeriGondermeZamani = 0;
const unsigned long VERI_GONDERME_ARALIGI = 1000; // Buluta 1 saniyede bir veri gönder (gecikmeyi azaltmak için)
unsigned long sonBaglantiDenemeZamani = 0;
const unsigned long BAGLANTI_DENEME_ARALIGI = 10000; // Bağlantı koparsa 10 saniyede bir dene (Non-blocking)

// --- SERVO DURUMLARI (360 Derece Servo İçin) ---
enum KapakDurumu {
  KAPALI,
  ACILIYOR,
  ACIK,
  KAPANIYOR
};
KapakDurumu kapakDurumu = KAPALI;
unsigned long kapakAksiyonZamani = 0;
const unsigned long KAPAK_HAREKET_SURESI = 800; // Kapak açılma/kapanma motor dönme süresi (milisaniye)
const unsigned long KAPAK_ACIK_BEKLEME_SURESI = 4000; // Kapağın açık kalma süresi (milisaniye)

// 360 Derece Servo için mikrosaniye (pulse width) tanımlamaları
const int SERVO_DUR = 1500;       // Motoru durduran nötr değer (Standart: 1500)
const int SERVO_AC_YON = 1700;    // Kapak açma yönü (1500-2000 arası). Ters ise 1300 yapın.
const int SERVO_KAPA_YON = 1300;  // Kapak kapama yönü (1000-1500 arası). Ters ise 1700 yapın.

// Mesafe ölçen yardımcı fonksiyon
int mesafeOlc(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duration = pulseIn(echoPin, HIGH, 15000); // Zaman aşımını 15ms'ye çektik (yaklaşık 2.5 metre için yeterli, okuma hızını artırır)
  int mesafe = duration * 0.034 / 2;
  return mesafe;
}

// Wi-Fi bağlantı fonksiyonu
void wifiKur() {
  delay(10);
  Serial.println();
  Serial.print("Wi-Fi baglantisi kuruluyor: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Kurulumda baglanana kadar bekleyebiliriz
  int denemeSayisi = 0;
  while (WiFi.status() != WL_CONNECTED && denemeSayisi < 20) {
    delay(500);
    Serial.print(".");
    denemeSayisi++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("Wi-Fi baglandi!");
    Serial.print("IP adresi: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWi-Fi baglantisi kurulamadi! Sistem offline modda baslatiliyor.");
  }
}

// ThingsBoard MQTT Bağlantı fonksiyonu (Non-blocking yapabilmek için sorgulu)
boolean tbBaglan() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  
  Serial.print("ThingsBoard MQTT baglantisi deneniyor (");
  Serial.print(TB_SERVER);
  Serial.println(")...");

  // ThingsBoard baglantisi icin Token'i Kullanıcı Adı (Username) alanına yazıyoruz, şifre boş kalıyor.
  if (client.connect("ESP32_SmartTrashBin", TB_TOKEN, NULL)) {
    Serial.println("ThingsBoard baglantisi basarili!");
    return true;
  } else {
    Serial.print("Baglanti basarisiz, hata kodu: ");
    Serial.println(client.state());
    return false;
  }
}

void setup() {
  Serial.begin(115200);
  
  // Sensör pin ayarları
  pinMode(SEVIYE_TRIG, OUTPUT);
  pinMode(SEVIYE_ECHO, INPUT);
  pinMode(EL_TRIG, OUTPUT);
  pinMode(EL_ECHO, INPUT);
  
  // LED pin ayarları
  pinMode(LED_YESIL, OUTPUT);
  pinMode(LED_KIRMIZI, OUTPUT);
  
  // Servo başlangıç ayarı
  kapakServosu.attach(SERVO_PIN);
  kapakServosu.writeMicroseconds(SERVO_DUR); // Kapak durur vaziyette başlar
  
  // Wi-Fi Kurulumu
  wifiKur();
  client.setServer(TB_SERVER, TB_PORT);
}

void loop() {
  // MQTT istemcisini canlı tut
  client.loop();

  // Wi-Fi veya MQTT bağlantısı koptuğunda loop'u kilitlemeden (non-blocking) belirli aralıklarla bağlanmayı dene
  unsigned long simdikiZaman = millis();
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      if (simdikiZaman - sonBaglantiDenemeZamani >= BAGLANTI_DENEME_ARALIGI) {
        sonBaglantiDenemeZamani = simdikiZaman;
        tbBaglan();
      }
    }
  }

  // Sensörlerden verileri oku
  int icMesafe = mesafeOlc(SEVIYE_TRIG, SEVIYE_ECHO);
  int disMesafe = mesafeOlc(EL_TRIG, EL_ECHO);
  
  // Hatalı/sınır dışı okuma düzeltmeleri
  if (icMesafe > COP_KUTUSU_BOYUTU || icMesafe <= 0) icMesafe = COP_KUTUSU_BOYUTU;
  if (disMesafe <= 0) disMesafe = 400;

  // Doluluk oranı hesabı
  int dolulukOrani = ((COP_KUTUSU_BOYUTU - icMesafe) * 100) / COP_KUTUSU_BOYUTU;
  if (dolulukOrani < 0) dolulukOrani = 0;

  boolean kutuDolu = (dolulukOrani >= DOLULUK_ESIGI);

  // --- ALGORİTMA MANTIĞI (360 Derece Servo State Machine) ---
  if (kutuDolu) {
    // DURUM 1: Kutu Dolu -> Kırmızı LED, Kapak Kilitli
    digitalWrite(LED_KIRMIZI, HIGH);
    digitalWrite(LED_YESIL, LOW);
    
    // Eğer kapak açık veya hareket halindeyse acilen kapatıp kilitleyelim
    if (kapakDurumu != KAPALI) {
      kapakServosu.writeMicroseconds(SERVO_KAPA_YON);
      delay(KAPAK_HAREKET_SURESI); // Acil kapatma için küçük bir bloke edici gecikme kabul edilebilir
      kapakServosu.writeMicroseconds(SERVO_DUR);
      kapakDurumu = KAPALI;
    } else {
      kapakServosu.writeMicroseconds(SERVO_DUR); // Kilitliyken sürekli dur sinyali gönder
    }
  } else {
    // DURUM 2: Güvenli Mod -> Yeşil LED
    digitalWrite(LED_YESIL, HIGH);
    digitalWrite(LED_KIRMIZI, LOW);
    
    // Kapak kapalıyken el algılanırsa açma motorunu çalıştır
    if (kapakDurumu == KAPALI && disMesafe <= EL_ALGILAMA_MESAFESI) {
      Serial.println("El Algilandi -> Kapak Aciliyor...");
      kapakServosu.writeMicroseconds(SERVO_AC_YON);
      kapakAksiyonZamani = simdikiZaman;
      kapakDurumu = ACILIYOR;
    } else if (kapakDurumu == KAPALI) {
      kapakServosu.writeMicroseconds(SERVO_DUR); // El algılanmadığında sürekli dur sinyali gönder
    }
  }

  // Açılma süresi dolunca motoru durdur ve açık beklemeye geç
  if (kapakDurumu == ACILIYOR && (simdikiZaman - kapakAksiyonZamani >= KAPAK_HAREKET_SURESI)) {
    Serial.println("Kapak Acildi -> Motor Durduruluyor...");
    kapakServosu.writeMicroseconds(SERVO_DUR);
    kapakAksiyonZamani = simdikiZaman;
    kapakDurumu = ACIK;
  }

  // Açık bekleme süresi dolunca kapatma motorunu çalıştır
  if (kapakDurumu == ACIK && (simdikiZaman - kapakAksiyonZamani >= KAPAK_ACIK_BEKLEME_SURESI)) {
    Serial.println("Sure Bitti -> Kapak Kapaniyor...");
    kapakServosu.writeMicroseconds(SERVO_KAPA_YON);
    kapakAksiyonZamani = simdikiZaman;
    kapakDurumu = KAPANIYOR;
  }

  // Kapanma süresi dolunca motoru durdur ve kapalı moda geç
  if (kapakDurumu == KAPANIYOR && (simdikiZaman - kapakAksiyonZamani >= KAPAK_HAREKET_SURESI)) {
    Serial.println("Kapak Kapandi -> Motor Durduruluyor...");
    kapakServosu.writeMicroseconds(SERVO_DUR);
    kapakDurumu = KAPALI;
  }

  // --- THINGSBOARD BULUTA VERİ GÖNDERME (2 saniyede bir) ---
  if (client.connected() && (simdikiZaman - sonVeriGondermeZamani >= VERI_GONDERME_ARALIGI)) {
    sonVeriGondermeZamani = simdikiZaman;

    // Telemetri verisini JSON formatında hazırlıyoruz
    String payload = "{";
    payload += "\"icMesafe\":" + String(icMesafe) + ",";
    payload += "\"dolulukOrani\":" + String(dolulukOrani) + ",";
    payload += "\"disMesafe\":" + String(disMesafe) + ",";
    payload += "\"kilitli\":" + String(kutuDolu ? "true" : "false");
    payload += "}";

    // ThingsBoard'un telemetri yayınlama (publish) konusu: v1/devices/me/telemetry
    if (client.publish("v1/devices/me/telemetry", payload.c_str())) {
      Serial.print("Buluta gonderilen veri: ");
      Serial.println(payload);
    } else {
      Serial.println("Buluta veri gonderme basarisiz!");
    }
  }

  // Seri port ekranına çıktı ver
  Serial.print("Cop: "); Serial.print(icMesafe);
  Serial.print(" cm (%"); Serial.print(dolulukOrani);
  Serial.print(") | El: "); Serial.print(disMesafe);
  Serial.print(" cm | TB: ");
  Serial.println(client.connected() ? "Bagli" : "Bagli Degil");

  delay(50); // Ölçüm hassasiyetini ve kapak tepki hızını artırmak için döngü süresi kısaltıldı
}
