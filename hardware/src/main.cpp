/*
  Serrure connectee - ESP32 (version MQTT / EMQX Cloud)
  ------------------------------------------------------
  L'ESP32 se connecte en sortant vers EMQX Cloud et ecoute
  un topic. Cela evite tout probleme de NAT/pare-feu : pas besoin
  d'IP publique ni d'ouverture de port sur la box internet.

  Bibliotheque a installer (Gestionnaire de bibliotheques Arduino) :
    - PubSubClient (par Nick O'Leary)

  A renseigner ci-dessous :
    - identifiants WiFi
    - endpoint EMQX Cloud (visible sur la page Overview de votre deploiement)
    - identifiants MQTT crees dans Access Control sur EMQX Cloud
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

// === CONFIGURATION WIFI ===
const char* ssid     = "AC-inGit";
const char* password = "12345678";

// === CONFIGURATION EMQX CLOUD (MQTT) ===
const char* mqtt_host  = "iaa89f21.ala.us-east-1.emqxsl.com";  // Overview > Connection Address
const int   mqtt_port  = 8883;                          // port TLS d'EMQX Cloud
const char* mqtt_user  = "esp32cam";       // cree dans Access Control
const char* mqtt_pass  = "sVRwxZY9:s3VxfJ";
const char* mqtt_topic = "lock/unlock";

// === CONFIGURATION RELAIS ===
const int  RELAY_PIN          = 26;
const bool RELAY_ACTIVE_HIGH  = true;
const int  UNLOCK_DURATION_MS = 4000;
const char* ca_cert = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----
)EOF";
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

void setLock(bool unlocked) {
  bool level = RELAY_ACTIVE_HIGH ? unlocked : !unlocked;
  digitalWrite(RELAY_PIN, level ? HIGH : LOW);
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  Serial.println("Message recu [" + String(topic) + "] : " + message);

  if (message == "unlock") {
    Serial.println("Deverrouillage...");
    setLock(true);
    delay(UNLOCK_DURATION_MS);
    setLock(false);
  }
}

void connectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Connexion a EMQX Cloud...");
    String clientId = "esp32-serrure-" + String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println(" connecte.");
      mqttClient.subscribe(mqtt_topic);
      Serial.println("Abonne au topic : " + String(mqtt_topic));
    } else {
      Serial.print(" echec (code ");
      Serial.print(mqttClient.state());
      Serial.println("), nouvelle tentative dans 3s");
      delay(3000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  setLock(false); // verrouille par defaut

  WiFi.begin(ssid, password);
  Serial.print("Connexion au WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connecte. IP locale : " + WiFi.localIP().toString());

  // Pour simplifier : on ne verifie pas le certificat du serveur EMQX.
  // La connexion reste chiffree (TLS), seule l'identite du certificat
  // n'est pas validee. Voir notre discussion precedente sur ce sujet.
  espClient.setInsecure();

  mqttClient.setServer(mqtt_host, mqtt_port);
  mqttClient.setCallback(onMqttMessage);
}

void loop() {
  if (!mqttClient.connected()) {
    connectMQTT();
  }
  mqttClient.loop();
}
