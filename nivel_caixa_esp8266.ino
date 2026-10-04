#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Firebase_ESP_Client.h>

// Instale no Arduino IDE: "Firebase Arduino Client Library for ESP8266 and ESP32"
#include "addons/TokenHelper.h"

// Preencha antes de gravar na placa.
#define WIFI_SSID "SUA_REDE_WIFI"
#define WIFI_PASSWORD "SUA_SENHA_WIFI"
#define API_KEY "AIzaSyAfYVdltzv8WVR-JS29t2-nfWrYbpBCjCc"

#define DATABASE_URL "https://nivel-caixa-5l-esp8266-default-rtdb.firebaseio.com/"

// NodeMCU ESP8266
constexpr uint8_t PIN_TRIG = D5; // GPIO14
constexpr uint8_t PIN_ECHO = D6; // GPIO12, sempre por divisor 1 kΩ / 2 kΩ

constexpr float ALTURA_UTIL_CM = 26.0f;
constexpr float VOLUME_MAX_L = 5.0f;
constexpr float OFFSET_SENSOR_CM = 0.0f; // ajuste após medir vazio e cheio
constexpr unsigned long INTERVALO_ENVIO_MS = 2000;

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool autenticado = false;
unsigned long ultimoEnvio = 0;

float medirDistanciaCm() {
  float leituras[5];
  for (uint8_t i = 0; i < 5; i++) {
    digitalWrite(PIN_TRIG, LOW);
    delayMicroseconds(3);
    digitalWrite(PIN_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);

    const unsigned long duracao = pulseIn(PIN_ECHO, HIGH, 30000UL);
    leituras[i] = duracao == 0 ? NAN : (duracao * 0.0343f / 2.0f);
    delay(60);
  }

  // Mediana simples: reduz respingos e ecos falsos.
  for (uint8_t i = 0; i < 4; i++) {
    for (uint8_t j = i + 1; j < 5; j++) {
      if (isnan(leituras[i]) || (!isnan(leituras[j]) && leituras[j] < leituras[i])) {
        const float tmp = leituras[i];
        leituras[i] = leituras[j];
        leituras[j] = tmp;
      }
    }
  }
  return leituras[2];
}

void conectarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Conectando ao Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print('.');
  }
  Serial.println(" conectado");
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);

  conectarWiFi();

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;

  // Login anônimo: requer o provedor Anônimo ativado no Firebase Authentication.
  autenticado = Firebase.signUp(&config, &auth, "", "");
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) conectarWiFi();

  if (!autenticado || !Firebase.ready() || millis() - ultimoEnvio < INTERVALO_ENVIO_MS) {
    delay(20);
    return;
  }
  ultimoEnvio = millis();

  const float distancia = medirDistanciaCm();
  if (isnan(distancia)) {
    Firebase.RTDB.setString(&fbdo, "/caixa/status", "sem_eco");
    return;
  }

  const float nivel = constrain(ALTURA_UTIL_CM - distancia + OFFSET_SENSOR_CM, 0.0f, ALTURA_UTIL_CM);
  const float percentual = nivel * 100.0f / ALTURA_UTIL_CM;
  const float volume = percentual * VOLUME_MAX_L / 100.0f;

  FirebaseJson json;
  json.set("nivel_cm", nivel);
  json.set("percentual", percentual);
  json.set("volume_l", volume);
  json.set("distancia_cm", distancia);
  json.set("atualizado_ms", (int)millis());
  json.set("status", "online");

  if (!Firebase.RTDB.updateNode(&fbdo, "/caixa", &json)) {
    Serial.printf("Erro Firebase: %s\n", fbdo.errorReason().c_str());
  }
}
