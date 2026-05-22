#include <ESPmDNS.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP_Mail_Client.h>

#define PIN_TRIG        4
#define PIN_ECHO        18
#define PIN_LED_VERT    25
#define PIN_LED_ORANGE  33
#define PIN_LED_ROUGE   32
#define PIN_BUZZER      27

#define OLED_W    128
#define OLED_H     64
#define OLED_ADDR 0x3C

// Coordonnées du compte email (utilisé par tout les ESP32 à venir : SENDER)
#define GMAIL_SMTP_HOST "smtp.gmail.com"
#define GMAIL_SMTP_PORT 465
#define GMAIL_SENDER_EMAIL "sender@gmail.com"
#define GMAIL_SENDER_PASSWORD "wwww xxxx yyyy zzzz" // Mot de passe application pour la sécurité

// Coordonnées du destinataire (mon email perso mais en réalité celui du gérant)
#define EMAIL_RECIPIENT "youremail@gmail.com"

// Objet SMTP
SMTPSession smtp;

#define FIREBASE_SECRET "THE-FIRE-BASE-SECRET"

// Prototype carton : 15cm × 15cm × 23cm de hauteur
// Zone morte capteur : 3cm minimum
#define HAUTEUR_TOTALE_CM   23.0
#define DISTANCE_MIN_CM      3.0

#define SEUIL_PLEINE_MONTE    90
#define SEUIL_PLEINE_DESCENTE 85
#define SEUIL_ATTENTION_MONTE 75
#define SEUIL_ATTENTION_DESCENTE 70

#define SEUIL_PLEINE    SEUIL_PLEINE_MONTE
#define SEUIL_ATTENTION SEUIL_ATTENTION_MONTE

const char* WIFI_SSID     = "ssid-wifi";
const char* WIFI_PASSWORD = "mot-de-passse-wifi";

const char* FIREBASE_HOST = "https://votre-projet-dans-firebase000.com";

// Identifiant unique de la poubelle
const char* POUBELLE_ID   = "poubelle_01";
const char* POUBELLE_NOM  = "Poubelle nom-qartier";

// Pour le Bot Telegram
const char* BOT_TOKEN = "LE-TOKEN-DE-VOTRE-BOT";
const char* CHAT_ID   = "VOTRE-CHAT-ID";

Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);

unsigned long t_buzzer_on = 0;
bool buzzer_en_cours = false;

// Variables d'états
float g_distance = 0.0;
float g_remplissage = 0.0;
String g_etat = "Initialisation";

bool alerteEnvoyee = false;     // anti-spam Telegram
bool led_rouge_etat = false;    // clignotement LED rouge

unsigned long t_mesure = 0;
unsigned long t_firebase = 0;   // timer envoi Firebase
unsigned long t_clignotement = 0;
unsigned long t_buzzer = 0;

//   Mesure distance
float lireDistance() {
  digitalWrite(PIN_TRIG, LOW); delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  long duree = pulseIn(PIN_ECHO, HIGH, 12000);
  
  // Clampage physique pour empêcher les valeurs impossibles
  // dist > 23cm = couvercle ouvert → on considère vide
  // dist < 3cm  = main collée → on considère plein
  float dist = (duree == 0) ? HAUTEUR_TOTALE_CM : (duree / 2.0) * 0.0343;
  if (dist > HAUTEUR_TOTALE_CM) dist = HAUTEUR_TOTALE_CM;
  if (dist < DISTANCE_MIN_CM)   dist = DISTANCE_MIN_CM;

  return dist;
}

//   Calcul pourcentage
//   on met dist=23cm → 0% (vide)
float calculerPct(float dist) {
  float pct = ((HAUTEUR_TOTALE_CM - dist) / (HAUTEUR_TOTALE_CM - DISTANCE_MIN_CM)) * 100.0;
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

//  Envoie firebase
void envoyerFirebase() {

  // Vérifier WiFi avant d'essayer
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Firebase : WiFi absent");
    return;
  }

  // Construire l'URL complète
  String url = String(FIREBASE_HOST) + "/poubelles/" + POUBELLE_ID + ".json?auth=" + FIREBASE_SECRET;
  
  // Construire le JSON à envoyer
  // Ce JSON sera stocké dans Firebase exactement tel quel
  String json = "{";
  json += "\"nom\":\"" + String(POUBELLE_NOM) + "\",";
  json += "\"distance\":" + String(g_distance, 1) + ",";
  json += "\"remplissage\":" + String(g_remplissage, 1) + ",";
  json += "\"etat\":\"" + g_etat + "\"";
  json += "}";

  // Envoyer la requête PATCH
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  // PATCH = mise à jour partielle
  int code = http.PATCH(json);    

  if (code > 0) Serial.print("Firebase OK : "); else Serial.print("Firebase erreur : ");
  Serial.println(code); // 200 = succès
  http.end();
}

// Envoie telegram
void envoyerTelegram() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  // Construction du message AVANT de l'utiliser dans l'URL
  String message = "ALERTE - " + String(POUBELLE_NOM) + "\n";
  message += "Remplissage : " + String((int)g_remplissage) + "%\n";
  message += "Intervention requise!";
  
  // Encodage URL
  message.replace(" ", "%20");
  message.replace("\n", "%0A");
  
  String url = "https://api.telegram.org/bot" + String(BOT_TOKEN);
  url += "/sendMessage?chat_id=" + String(CHAT_ID);
  url += "&text=" + message;
  
  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  Serial.println(code == 200 ? "Telegram : envoyé !" : "Telegram : erreur");
  http.end();
}

// Mise à jour des LEDs
void mettreAJourLEDs() {
  static int etat = 0;
  unsigned long now = millis();
  
  if (etat == 2 && g_remplissage <= SEUIL_PLEINE_DESCENTE)
    etat = 1;
  else if (etat == 1 && g_remplissage >= SEUIL_PLEINE_MONTE)
    etat = 2;
  else if (etat == 1 && g_remplissage <= SEUIL_ATTENTION_DESCENTE)
    etat = 0;
  else if (etat == 0 && g_remplissage >= SEUIL_ATTENTION_MONTE)
    etat = 1;
  
  switch (etat) {
    case 0:
      digitalWrite(PIN_LED_VERT, HIGH);
      digitalWrite(PIN_LED_ORANGE, LOW);
      digitalWrite(PIN_LED_ROUGE, LOW);
      digitalWrite(PIN_BUZZER, LOW);
      buzzer_en_cours = false;
      break;
    case 1:
      digitalWrite(PIN_LED_VERT, LOW);
      digitalWrite(PIN_LED_ORANGE, HIGH);
      digitalWrite(PIN_LED_ROUGE, LOW);
      digitalWrite(PIN_BUZZER, LOW);
      buzzer_en_cours = false;
      break;
    case 2:
      digitalWrite(PIN_LED_VERT, LOW);
      digitalWrite(PIN_LED_ORANGE, LOW);
      if (now - t_clignotement >= 500) {
        led_rouge_etat = !led_rouge_etat;
        digitalWrite(PIN_LED_ROUGE, led_rouge_etat);
        t_clignotement = now;
      }
      if (!buzzer_en_cours && (now - t_buzzer >= 500)) {
        digitalWrite(PIN_BUZZER, HIGH);
        buzzer_en_cours = true;
        t_buzzer_on = now;
        t_buzzer = now;
      } else if (buzzer_en_cours && (now - t_buzzer_on >= 80)) {
        digitalWrite(PIN_BUZZER, LOW);
        buzzer_en_cours = false;
      }
      break;
  }
}

// Affichage sur l'OLED
void afficherOLED() {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0); oled.println("FullBin Project");
  oled.setCursor(0, 11); oled.print("Libre: "); oled.print(g_distance, 1); oled.println(" cm");
  oled.setTextSize(2);
  oled.setCursor(0, 22); oled.print("Rem: "); oled.print((int)g_remplissage); oled.println("%");
  oled.setTextSize(1);
  oled.setCursor(0, 44); oled.print("Etat: "); oled.println(g_etat);
  oled.setCursor(0, 55); oled.println("Proprete = sante !");
  oled.display();
}

// Affiche le statut de l'email
void smtpCallback(SMTP_Status status) {
  Serial.println(status.info());
}

// Envoie email au gérant
void envoyerEmail(float remplissage, float distance) {
  if (WiFi.status() != WL_CONNECTED) return;
 
  // Configuration de la session SMTP
  Session_Config config;
  config.server.host_name = GMAIL_SMTP_HOST;
  config.server.port = GMAIL_SMTP_PORT;
  config.login.email = GMAIL_SENDER_EMAIL;
  config.login.password = GMAIL_SENDER_PASSWORD;

  // Configuration du fuseau horaire (Madagascar UTC+3)
  config.time.ntp_server = "pool.ntp.org";
  config.time.gmt_offset = 3;

  // Construction du message
  SMTP_Message message;
  message.sender.name = "Système Poubelle Smart";
  message.sender.email = GMAIL_SENDER_EMAIL;
  message.subject = "ALERTE - Poubelle pleine";
  message.addRecipient("Gestionnaire", EMAIL_RECIPIENT);

  // Corps de l'email : vous pouvez le modifier selon le type d'email que vous voulez recevoir.
  String corps = "OBJET : ALERTE – NIVEAU DE REMPLISSAGE CRITIQUE\n\n";
  corps += "Bonjour,\n\n";
  corps += "Le système de surveillance de la poubelle intelligente « " + String(POUBELLE_NOM) + " » a détecté un dépassement du seuil d’alerte.\n\n";
  corps += "État actuel :\n";
  corps += "   • Taux de remplissage : " + String(remplissage, 1) + " %\n";
  corps += "   • Hauteur libre restante : " + String(distance, 1) + " cm\n";
  corps += "   • Seuil critique : 90 %\n\n";
  corps += "Action requise :\n";
  corps += "   Intervention nécessaire pour la collecte.\n\n";
  corps += "Respectueusement,\n";
  corps += "Système IoT – Projet gestion de déchets\n---\n";

  message.text.content = corps.c_str();
  message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_high;
  
  // Envoi
  if (!smtp.connect(&config)) { Serial.println("Erreur SMTP"); return; }
  if (!MailClient.sendMail(&smtp, &message)) Serial.println("Erreur envoi email");
  else Serial.println("Email envoyé !");
  smtp.closeSession();
}

void setup() {
  Serial.begin(115200);
  smtp.debug(1);            // Active les logs SMTP (0 = silence)
  smtp.callback(smtpCallback);
  
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_LED_VERT, OUTPUT);
  pinMode(PIN_LED_ORANGE, OUTPUT);
  pinMode(PIN_LED_ROUGE, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_TRIG, LOW);
  digitalWrite(PIN_LED_VERT, LOW);
  digitalWrite(PIN_LED_ORANGE, LOW);
  digitalWrite(PIN_LED_ROUGE, LOW);
  digitalWrite(PIN_BUZZER, LOW);
  
  // OLED
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED introuvable !");
    while (true);
  }
  oled.clearDisplay();
  oled.setTextSize(1); oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0); oled.println("FullBin");
  oled.setCursor(0, 14); oled.println("Connexion WiFi...");
  oled.display();
  
  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 20) {
    delay(500); Serial.print("."); t++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK : " + WiFi.localIP().toString());
    oled.clearDisplay();
    oled.setCursor(0, 0); oled.println("WiFi OK !");
    oled.setCursor(0, 14); oled.println(WiFi.localIP().toString());
    oled.display();
    delay(2000);
  }
  Serial.println("Systeme pret.");
}

void loop() {
  unsigned long now = millis();
  
  // Mesure toutes les 800ms
  if (now - t_mesure >= 200) {
    t_mesure = now;
    g_distance = lireDistance(); 
    g_remplissage = calculerPct(g_distance);
    
    if (g_remplissage >= SEUIL_PLEINE)    g_etat = "PLEINE !!!";
    else if (g_remplissage >= SEUIL_ATTENTION) g_etat = "ATTENTION";
    else                                 g_etat = "Disponible";
    
    afficherOLED();
    Serial.print("dist="); Serial.print(g_distance, 1);
    Serial.print(" rem="); Serial.print(g_remplissage, 1);
    Serial.print("% etat="); Serial.println(g_etat);
    
    // Alerte Telegram et Email, une seule fois par remplissage
    if (g_remplissage >= SEUIL_PLEINE && !alerteEnvoyee) {
      envoyerTelegram();
      envoyerEmail(g_remplissage, g_distance);
      alerteEnvoyee = true;
    }
    if (g_remplissage < SEUIL_ATTENTION) {
      alerteEnvoyee = false;
    }
  }
  
  // Envoi Firebase toutes les 5 secondes : vous pouvez le modifier aussi selon vos besoins
  if (now - t_firebase >= 5000) {
    t_firebase = now;
    envoyerFirebase();
  }
  
  mettreAJourLEDs();
}