//*********************************************
//*** LIBRERIAS
//*********************************************
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Time.h>
#include "EEPROM.h"

//*********************************************
//*** WIFI SMARTCONFIG
//*********************************************
#define LENGTH(x) (strlen(x) + 1) // length of char string
#define EEPROM_SIZE 200           // EEPROM size
#define WiFi_rst 0                // WiFi credential reset pin (Boot button on ESP32)
String ssid;                      // string variable to store ssid
String pss;                       // string variable to store password
unsigned long rst_millis;

//*********************************************
//*** ASIGNACIONES DE MQTT
//*********************************************
const char *mqtt_server = "ceres-iot.cl";
const int mqtt_port = 1883;
const char *mqtt_user = "admin";
const char *mqtt_pass = "@c3r3s_bot#";
char topico[90];
long lstMsg = 0;
char msg[30];
String to_send = "";

//*********************************************
//*** ASIGNACIONES DE SERVIDOR NTP
//*********************************************
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -4 * 3600;
const int daylightOffset_sec = 3600;

//*********************************************
//*** ASIGNAR INSTANCIAS
//*********************************************
WiFiClient espClient;
PubSubClient client(espClient);
ESP32Time rtc;

//*********************************************
//*** VARIABLES GLOBALES
//*********************************************
// Variables con PINES
byte wifi_led = 21;
byte mqtt_led = 19;
byte led_var = 18;
byte llave1 = 26;
byte llave2 = 27;
byte llave3 = 25;
byte llave4 = 33;
byte senHum1 = 34;
int cont = 0;
bool smartWater = false;
bool estadoRiego = false;
bool sensorHum1 = false;
// Variables del sensor de humedad
int S1min = 2880; // Maximo nivel seco 2841
int S1max = 900;  // Maximo nivel Humedo 915
// VARIABLES MODIFICABLES
int humAmb = 0;
int humedadSen = 0;   // Recibe valor del sensor de humedad 1
byte humedadx100 = 0; // mapea a porcentaje el valor del sensor 1
int Hr = -1;
// CONFIGURACION DE SISTEMA
byte HrRiegoM = 26;      // Hora de riego mañana
byte HrRiegoT = 1;     // Hora de riego tarde
byte nivelHmdAlto = 95; // Nivel de humedad para no regar
byte nivelHmdBajo = 50; // Nivel de humedad para activar regadores en horario correspondiente
int comando = 0;        // recibe comandos para accionar llaves
unsigned long sensorReadTime = 0;
bool msjUnaVez = true;
String device_id;
String idUnique();
String hexStr();

//*********************************************
//*** DECLARACION DE FUNCIONES
//*********************************************
void setup_wifi();
void blynk_led(int _led, int _timer);
void on_Pin(int _pin);
void off_Pin(int _pin);
void callback(char *topic, byte *payload, unsigned int length);
void reconnect();
void leerSensorHum1();
void sendMqttVal(String dato, String colaTopic);
void topicSuscription(String suscTopic);
void ActivaRiego(int _regador, int _sensor);
void ciclosDeRiego(String _ciclo, int _min, int _max, int _pin, int _hum, int _pOff);
void apagarRiego(int _regador);
void regarxCiclo(String _ciclo, int _min, int _max, int _pin, int _pinAnt);
void sendMqttID(String dato);
void pinesOut();
void on_Llave(int _pin);
void off_Llave(int _pin);

    //*********************************************
    //*** SMART CONFIG
    //*********************************************

    void writeStringToFlash(const char *toStore, int startAddr)
{
  int i = 0;
  for (; i < LENGTH(toStore); i++)
  {
    EEPROM.write(startAddr + i, toStore[i]);
  }
  EEPROM.write(startAddr + i, '\0');
  EEPROM.commit();
}

String readStringFromFlash(int startAddr)
{
  char in[128]; // char array of size 128 for reading the stored data
  int i = 0;
  for (; i < 128; i++)
  {
    in[i] = EEPROM.read(startAddr + i);
  }
  return String(in);
}

//*********************************************
//*** ID UNICO
//*********************************************

String idUnique()
{
  // Retorna los ultimos 4 Bytes del MAC rotados
  char idunique[15];
  uint64_t chipid = ESP.getEfuseMac();
  uint16_t chip = (uint16_t)(chipid >> 32);
  snprintf(idunique, 15, "%04X", chip);
  return idunique;
}

String hexStr(const unsigned long &h, const byte &l = 8)
{
  String s;
  s = String(h, HEX);
  s.toUpperCase();
  s = ("00000000" + s).substring(s.length() + 8 - l);
  return s;
}

String platform()
{
// Obtiene la plataforma de hardware
#ifdef ARDUINO_ESP32_DEV
  return "ESP32V2";
#endif
}
String deviceID()
{
  return String(platform()) + hexStr(ESP.getEfuseMac()) + String(idUnique());
}

void pinesOut(){  
  digitalWrite(llave1, 1);
  digitalWrite(llave2, 1);
  digitalWrite(llave3, 1);
  digitalWrite(llave4, 1);
  delay(500);
}
void pines()
{

  pinesOut();
  pinMode(wifi_led, OUTPUT);
  pinMode(mqtt_led, OUTPUT);
  pinMode(led_var, OUTPUT);
  pinMode(llave1, OUTPUT);
  pinMode(llave2, OUTPUT);
  pinMode(llave3, OUTPUT);
  pinMode(llave4, OUTPUT);
  pinMode(senHum1, INPUT_PULLUP);
}

void setup()
{
  Serial.begin(115200); // Init serial
  delay(100);
  pines();
  delay(100);
  
  pinMode(WiFi_rst, INPUT);
  if (!EEPROM.begin(EEPROM_SIZE))
  { // Init EEPROM
    Serial.println("[ ** WARNING **] ERROR AL INICIAR EEPROM");
    delay(1000);
  }
  else
  {
    ssid = readStringFromFlash(0); // Read SSID stored at address 0
    Serial.print("SSID = ");
    Serial.println(ssid);
    pss = readStringFromFlash(40); // Read Password stored at address 40
    Serial.println("PASSWORD = [**********] ");
    // Serial.println(pss);
  }

  WiFi.begin(ssid.c_str(), pss.c_str());

  delay(3500); // Wait for a while till ESP connects to WiFi

  if (WiFi.status() != WL_CONNECTED) // if WiFi is not connected
  {
    // Init WiFi as Station, start SmartConfig
    WiFi.mode(WIFI_AP_STA);
    WiFi.beginSmartConfig();

    // Wait for SmartConfig packet from mobile
    Serial.println("[ ** CONFIG  **] ESPERANDO CONFIGURACION WIFI.");
    int contWifi =0;
    while (!WiFi.smartConfigDone())
    {
      delay(300);
      blynk_led(wifi_led, 60);
      blynk_led(wifi_led, 30);
      Serial.print(".");
      contWifi++;
      Serial.println("[ ALERT ] REINICIANDO EN " + String(40 - contWifi));
      if (contWifi>=40){
        contWifi=0;
        Serial.println("[ ALERT ] REINICIANDO");
        ESP.restart();
      }
    }

    Serial.println("");
    Serial.println("[ O K ] CONFIGURACION RECIBIDA.");
    blynk_led(wifi_led, 20);
    blynk_led(wifi_led, 40);
    delay(500);
    // Wait for WiFi to connect to AP
    Serial.println("[ ALERT ] INTENTANDO CONECTAR A" + String(ssid));
    contWifi=0;
    while (WiFi.status() != WL_CONNECTED)
    {
      blynk_led(wifi_led, 60);
      blynk_led(wifi_led, 30);
      delay(500);
      Serial.print(".");
      contWifi++;
      Serial.println("[ ALERT ] REINICIANDO EN " + String(30 - contWifi));
      if (contWifi >= 30)
      {
        contWifi = 0;
        Serial.println("[ ALERT ] REINICIANDO");
        ESP.restart();
      }
    }

    Serial.println("[ O K ] STATUS ON LINE.");
    on_Pin(wifi_led);
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // read the connected WiFi SSID and password
    ssid = WiFi.SSID();
    pss = WiFi.psk();
    Serial.println("[ * * CONFIG * * ] GUARDANDO CREDENCIALES");
    writeStringToFlash(ssid.c_str(), 0); // storing ssid at address 0
    writeStringToFlash(pss.c_str(), 40); // storing pss at address 40
    blynk_led(led_var, 60);
    blynk_led(led_var, 30);
  }
  else
  {
    Serial.println("[ O K ] STATUS ON LINE.");
    on_Pin(wifi_led);
  }

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  delay(100);
  //**************************************
  // Configuración de hora  por ntp
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  if (getLocalTime(&timeinfo))
  {
    rtc.setTimeStruct(timeinfo);
  }
  //****************************************
  // Crear codigo unico
  device_id = hexStr(ESP.getEfuseMac()) + "CERB0T" + String(idUnique());
  Serial.println("[ CONFIG ] TOMANDO VALORES POR DEFECTO");
  sensorHum1 = true;
}

void loop()
{
  rst_millis = millis();
  while (digitalRead(WiFi_rst) == LOW)
  {
    // Wait till boot button is pressed
  }
  // check the button press time if it is greater than 3sec clear wifi cred and restart ESP
  if (millis() - rst_millis >= 5000)
  {
    Serial.println("[ ** WARNING ** ] ELIMANDO CREDENCIALES GUARDADAS");
    writeStringToFlash("", 0);  // Reset the SSID
    writeStringToFlash("", 40); // Reset the Password
    Serial.println("[ O K ] CREDENCIALES ELIMINADAS");
    Serial.println("[ ALERT ] REINICIANDO");
    delay(500);
    ESP.restart(); // Restart ESP
  }
delay(1000);
  if (WiFi.status() != WL_CONNECTED)
  {
    setup_wifi();
  }
  if (!client.connected())
  {
    reconnect();
  }
  client.loop();

  unsigned long now = millis(); // Enviar mensajes a broker
  byte Minut = 3;
  if ((now - lstMsg) > (Minut * 60000))
  {
    
    leerSensorHum1();
    lstMsg = now;
    Serial.println(" ");
    Serial.println("[ TIME ]" + rtc.getTime("%H:%M:%S"));
    Serial.println(" PREPARANDO MENSAJE PARA PUBLICAR ");
    sendMqttVal("H1:" + String(humedadx100), "humedad");
  }

  if ((now - sensorReadTime) > 30000)
  {
    msjUnaVez = true;
    sensorReadTime = now;
    Serial.println("[ TIME ]" + rtc.getTime("%H:%M:%S"));
    leerSensorHum1();
    Serial.println("[ INFO ] Humedad Sensor Tierra:" + String(humedadx100) + "%");

    blynk_led(led_var, 80);
    blynk_led(led_var, 30);
  }
  // obtener datos del reloj sincronizado con ntp
  // Serial.println(rtc.getTime("%d/%m/%Y %H:%M:%S"));
  // Serial.println(rtc.getHour("%H"));
  // Serial.println(rtc.getMinute());
  // Serial.println(rtc.getDayofWeek());

  if (HrRiegoM == rtc.getHour("%H") or HrRiegoT == rtc.getHour("%H"))
  //aqui establecer funcion smartwater si true, que siga como esta, sino crear funcion simple que solo riege
  {
    if (smartWater)
    {
    estadoRiego = true;
    leerSensorHum1();
    ciclosDeRiego("PRIMER", 0, 15, llave1, humedadx100, llave4);
    ciclosDeRiego("SEGUNDO", 15, 30, llave2, humedadx100, llave1);
    ciclosDeRiego("TERCER", 30, 45, llave3, humedadx100, llave2);
    ciclosDeRiego("CUARTO", 45, 59, llave4, humedadx100, llave3);
    }else{
    estadoRiego = true;
    regarxCiclo("Sector 1",0,15,llave1,llave4);
    regarxCiclo("Sector 2", 16, 30, llave2, llave1);
    regarxCiclo("Sector 3", 31, 45, llave3, llave2);
    regarxCiclo("Sector 4", 46, 59, llave4, llave3);
    }
  }
  else
  {
    // Serial.println("SIN COINCIDENCIAS PARA RIEGO, CERRANDO LLAVES");
    if (estadoRiego)
    {
      off_Llave(llave1);
      off_Llave(llave1);
      off_Llave(llave2);
      off_Llave(llave3);
      off_Llave(llave4);
      Serial.println("[ INACTIVE ] CERRANDO CICLOS DE RIEGO");
      estadoRiego = false;
    }
    client.loop();
  }

  // Hr =rtc.getHour("%H");

  delay(100);
}

//*********************************************
//*** FUNCIONES SISTEMA RIEGO
//*********************************************

void ciclosDeRiego(String _ciclo, int _min, int _max, int _pin, int _hum, int _pOff)
{
  byte minuto = rtc.getMinute();
  if (minuto <= _max and minuto > _min)
  {
    if (msjUnaVez)
    {
      Serial.println("[ ACTIVE ] Hora de riego activada");
      Serial.println("[ INFO ] " + _ciclo + " CICLO ACTIVO");
      msjUnaVez = false;
      // Serial.println( "[ INFO ] " + String(_max - minuto) + String(" Minutos para Cerrar Ciclo"));
    }
    if (!digitalRead(_pOff))
    {
      apagarRiego(_pOff);
    }
    // aqui podria ir el smartwater
    ActivaRiego(_pin, _hum);
  }
}

void regarxCiclo(String _ciclo, int _min, int _max, int _pin, int _pinAnt)
{
  byte minuto = rtc.getMinute();
  if (minuto <= _max and minuto >= _min)
  {
    if (msjUnaVez)
    {
      Serial.println("[ ACTIVE ] Hora de riego activada");
      Serial.println("[ INFO ] " + _ciclo + " CICLO ACTIVO");
      msjUnaVez = false;
      
    }
    off_Llave(_pinAnt);
    on_Llave(_pin);
  }else{
    off_Llave(_pin);
  }
}

void ActivaRiego(int _regador, int _sensor){
  leerSensorHum1(); 
  if (_sensor < nivelHmdBajo)
  {
    if (digitalRead(_regador))
    {
      digitalWrite(_regador, 0);
      Serial.println("[ ACTION ] ACTIVANDO RIEGO EN PIN " + String(_regador));
      Serial.println("[INFO] HUMEDAD BAJO NIVEL " + String(nivelHmdBajo));
      Serial.println("[INFO] HUMEDAD : " + String(humedadx100) + "%");
      Serial.print("[ TIME ]" + rtc.getTime("%H:%M:%S"));
      //Serial.println(" PREPARANDO MENSAJE PARA PUBLICAR ");
      //sendMqttVal("1", "accion/llave/sector" + String(_regador));
    }
  }
  
  
   if (_sensor > nivelHmdAlto)
    {
     if (!digitalRead(_regador))
     {
      Serial.println("[ ACTION ] DESACTIVANDO RIEGO EN PIN " + String(_regador));
      Serial.println("[INFO] HUMEDAD SOBRE NIVEL " + String(nivelHmdAlto));
      Serial.println("[INFO] HUMEDAD : " + String(humedadx100) + "%");
      digitalWrite(_regador, 1);
      //Serial.println(" PREPARANDO MENSAJE PARA PUBLICAR ");
      //sendMqttVal("0", "accion/llave/sector" + String(_regador));
     }
   
  }
}


void apagarRiego(int _regador)
{
  if (!digitalRead(_regador))
  {
    digitalWrite(_regador, 1);
    Serial.println("[ ACTION ] APAGANDO RIEGO EN PIN " + String(_regador));
    //Serial.println(" PREPARANDO MENSAJE PARA PUBLICAR ");
    //sendMqttVal("0", "accion/llave/" + String(_regador));
  }
  // Serial.println("Pin APAGADO " + String(_regador) );
}

void leerSensorHum1()
{
  humedadSen = 0;
  humedadx100 = 0;
  humedadSen = analogRead(senHum1);
  humedadx100 = map(humedadSen, S1max, S1min, 100, 0);
   Serial.println("[ VALOR REAL DE HUMEDAD ["+ String(humedadSen)+"]]");
  if (humedadx100 < -25)
  {
    // Error en lectura
    if (msjUnaVez){
    Serial.println("[ WARNING ] Error en lectura de Sensor 1 [" + String(humedadx100) + "]");
    msjUnaVez=false;
    }
    if (sensorHum1)
    {
    sendMqttVal("H1-:" + String(humedadx100), "error/sensor");
    //sendMqttVal(String(humedadx100), "estados/humedad1/value");
    sensorHum1 = false;
    }
    // Notificar error del sensor....via mail?
  }
  if (humedadx100 > 125)
  {
    // Error en lectura
    if (msjUnaVez){
    Serial.println("[ WARNING ] Error en lectura de Sensor 1 [" + String(humedadx100) + "]");
    msjUnaVez=false;
    }
    if (sensorHum1)
    {
    sendMqttVal("H1+:" + String(humedadx100), "estados/humedad");
    //sendMqttVal(String(humedadx100), "estados/humedad1/value");
    sensorHum1 = false;
    }
    // Notificar error del sensor....via mail?
  }

  if (humedadx100 < 0)
  {
    humedadx100 = 0;
  }
  if (humedadx100 > 100)
  {
    humedadx100 = 100;
  }
}

//*********************************************
//*** FUNCIONES LED
//*********************************************
void blynk_led(int _led, int _timer)
{
  for (int i = 0; i < 3; i++)
  {
    digitalWrite(_led, 1);
    delay(_timer);
    digitalWrite(_led, 0);
    delay(_timer);
  }
}

void on_Pin(int _pin)
{
  digitalWrite(_pin, 1);
}

void off_Pin(int _pin)
{
  digitalWrite(_pin, 0);
}

void on_Llave(int _pin)
{
  digitalWrite(_pin, 0);
}

void off_Llave(int _pin)
{
  digitalWrite(_pin, 1);
}

//*********************************************
//*** FUNCION SETUP WIFI
//*********************************************
void setup_wifi()
{
  Serial.println("[ ACTION ] CERRANDO TODOS LOS CIRCUITOS");
  off_Llave(llave1);
  off_Llave(llave2);
  off_Llave(llave3);
  off_Llave(llave4);
  Serial.println("[ INFO ] CIRCUITOS CERRADOS");
  delay(100);
  Serial.println();
  Serial.print("[ CONNECTION ] Conectando a ");
  Serial.print(ssid);
  WiFi.begin(ssid.c_str(), pss.c_str());
  while (WiFi.status() != WL_CONNECTED)
  {
    blynk_led(wifi_led, 100);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("[ O K ] Conectado a " + String(ssid));
  Serial.println();
  Serial.print("[ INFO ] Dirección IP: ");
  Serial.println(WiFi.localIP());
  on_Pin(wifi_led);
}

//*********************************************
//*** FUNCIONES RECONNECT MQTT
//*********************************************
void reconnect()
{

  off_Llave(llave1);
  off_Llave(llave2);
  off_Llave(llave3);
  off_Llave(llave4);
  while (!client.connected())
  {
    off_Pin(mqtt_led);
    Serial.println("[ CONNECTION ] Intentando conexión MQTT " + String(cont));
    blynk_led(mqtt_led, 100);
    String ClientId = "Cs_";
    ClientId += device_id;

    if (client.connect(ClientId.c_str(), mqtt_user, mqtt_pass))
    {
      Serial.println("[ O K ] Conectado");
      Serial.println("[ TIME ]" + rtc.getTime("%H:%M:%S"));
      //*********************************************
      //*** SUSCRIPCION DE TOPICOS
      //*********************************************

      topicSuscription("/command");               // RECIBE COMANDOS
      topicSuscription("/horarios/regar/mañana"); // HORARIO REGAR AM
      topicSuscription("/horarios/regar/tarde");  // HORARIO REGAR PM
      topicSuscription("/humedad/nivel/alto");    //% HUMEDAD ALTO
      topicSuscription("/humedad/nivel/bajo");    // % HUMEDAD BAJO
      Serial.println("[ INFO ] Suscripciones terminadas");
      // ENVIAMOS PRIMER MENSAJE PARA INICIAR
      sendMqttVal("start", "inicia/config");
      // ENVIAMOS ID A TOPICO GENERAL
      sendMqttID(device_id);
    }
    else
    {
      cont++;
      off_Pin(mqtt_led);
      Serial.println(" ");
      Serial.print("[ ERROR ] Error en la conexión MQTT - ");
      Serial.println(client.state());
      Serial.println("[ ACTION ] Nuevo intento de conexión en 3 segundos...");
      
      
      if (cont>=2){
        cont=1;
        Serial.println("[ ACTION ] Verificando conexión WIFI");
        setup_wifi();
      }
      delay(3000);
    }
  }
}

//*********************************************
//*** FUNCION CALLBACK
//*********************************************
void callback(char *topic, byte *payload, unsigned int length)
{
  String incoming = "";
  Serial.println(" ");
  Serial.println("[ **  DOWNLOAD MESSAGE  ** ] ");
  Serial.print(topic);
  Serial.println(" ");
  off_Pin(mqtt_led);
  for (int i = 0; i < length; i++)
  {
    incoming += (char)payload[i];
  }
  blynk_led(mqtt_led, 15);
  blynk_led(mqtt_led, 50);
  blynk_led(mqtt_led, 30);
  incoming.trim();
  // Serial.println(incoming);
  on_Pin(mqtt_led);
  // COMPROBAR DESDE QUE TOPICO VIENE EL MENSAJE Y TOMAR ACCIONES
  if (strstr(topic, "command"))
  {
    Serial.println("[ WARNING ] Recibiendo comandos");
    comando = incoming.toInt();
    if (comando == 0)
    {
      off_Llave(llave1);
      Serial.println("[ INACTIVE ] DESACTIVANDO SECTOR 1 POR USER");
      sendMqttVal("0", "accion/llave/sector1");
    }
    if (comando == 1)
    {
      on_Llave(llave1);
      Serial.println("[ ACTIVE ] ACTIVANDO SECTOR 1 POR USER");
      sendMqttVal("1", "accion/llave/sector1");
    }
    if (comando == 2)
    {
      off_Llave(llave2);
      Serial.println("[ INACTIVE ] DESACTIVANDO SECTOR 2 POR USER");
      sendMqttVal("0", "accion/llave/sector2");
    }
    if (comando == 3)
    {
      on_Llave(llave2);
      Serial.println("[ ACTIVE ] ACTIVANDO SECTOR 2 POR USER");
      sendMqttVal("1", "accion/llave/sector2");
    }
    if (comando == 4)
    {
      off_Llave(llave3);
      Serial.println("[ INACTIVE ] DESACTIVANDO SECTOR 3 POR USER");
      sendMqttVal("0", "accion/llave/sector3");
    }
    if (comando == 5)
    {
      on_Llave(llave3);
      Serial.println("[ ACTIVE ] ACTIVANDO SECTOR 3 POR USER");
      sendMqttVal("1", "accion/llave/sector3");
    }
    if (comando == 6)
    {
      off_Llave(llave4);
      Serial.println("[ INACTIVE ] DESACTIVANDO SECTOR 4 POR USER");
      sendMqttVal("0", "accion/llave/sector4");
    }
    if (comando == 7)
    {
      on_Llave(llave4);
      Serial.println("[ ACTIVE ] ACTIVANDO SECTOR 4 POR USER");
      sendMqttVal("1", "accion/llave/sector4");
    }
    if (comando == 8)
    {
      smartWater = false;
      Serial.println("[ INACTIVE ] DESACTIVANDO SMARTWATER POR USER");
    }

    if (comando == 9)
    {
      smartWater = true;
      Serial.println("[ ACTIVE ] ACTIVANDO SMART WATER");
    }

    if (comando== 10){
      int contdown = 0;
      contdown = millis();
      int contd=10;
      while ((millis() - contdown)<10000)
      {
        Serial.println("[ WARNING ] REINICIO EN:" + String(contd));
        contd--;
        delay(1000);
        //Serial.print("'\r'[ WARNING ] REINICIO EN: " + String(contd));
      }
      ESP.restart(); // Restart ESP
      }
    
    // fin comandos
  }

  if (strstr(topic, "regar/mañana"))
  {
    Serial.println("[ RECEIVING ] Asignado nuevo valor Horario de Riego AM");
    HrRiegoM = incoming.toInt();
    Serial.println("[ INFO ] Hora de riego AM: " + String(HrRiegoM) + "Hr.");
  }
  if (strstr(topic, "regar/tarde"))
  {
    Serial.println("[ RECEIVING ] Asignado nuevo valor Horario de Riego PM");
    HrRiegoT = incoming.toInt();
    Serial.println("[ INFO ] Hora de riego PM: " + String(HrRiegoT) + "Hr.");
  }
  if (strstr(topic, "humedad/nivel/alto"))
  {
    Serial.println("[ RECEIVING ] Asignado nuevo valor de Humedad Alto");
    nivelHmdAlto = incoming.toInt();
    Serial.println("[ INFO ] Humedad Alto: " + String(nivelHmdAlto) + "%");
  }
  if (strstr(topic, "humedad/nivel/bajo"))
  {
    Serial.println("[ RECEIVING ] Asignado nuevo valor de Humedad Bajo");
    nivelHmdBajo = incoming.toInt();
    Serial.println("[ INFO ] Humedad Bajo: " + String(nivelHmdBajo) + "%");
  }


  Serial.println(" ");
}

//*********************************************
//*** ENVIO DE DATOS A TOPICO
//*********************************************
void sendMqttVal(String dato, String colaTopic)
{
  dato.toCharArray(msg, 30);
  Serial.println("");
  Serial.print("[ INFO ] Mensaje a enviar [");
  Serial.print(msg);
  Serial.print("]");
  off_Pin(mqtt_led);
  blynk_led(mqtt_led, 20);
  blynk_led(mqtt_led, 80);
  String topico_publish = "ceres/garden/" + device_id + "/" + colaTopic;
  topico_publish.toCharArray(topico, 150);
  client.publish(topico, msg);
  Serial.println(" ");
  Serial.print(" [SEND OK ] Enviado a: ");
  Serial.println(topico);
  on_Pin(mqtt_led);
}

//*********************************************
//*** SUSCRIPCION A TOPICO
//*********************************************
void topicSuscription(String suscTopic)
{
  String topico_subscribe = "ceres/garden/" + device_id + suscTopic;
  topico_subscribe.toCharArray(topico, 90);
  client.subscribe(topico);
  Serial.print("[ O K ] Suscrito:");
  Serial.println(topico);
}

//*********************************************
//*** ENVIO DE ID A BROKER
//*********************************************
void sendMqttID(String dato)
{
  dato.toCharArray(msg, 30);
  Serial.println("");
  Serial.print("[ INFO ] Mensaje a enviar [");
  Serial.print(msg);
  Serial.print("]");
  off_Pin(mqtt_led);
  blynk_led(mqtt_led, 20);
  blynk_led(mqtt_led, 80);
  String topico_publish = "ceres/garden/id";
  topico_publish.toCharArray(topico, 150);
  client.publish(topico, msg);
  Serial.println(" ");
  Serial.print(" [SEND OK ] Enviado a: ");
  Serial.println(topico);
  on_Pin(mqtt_led);
}