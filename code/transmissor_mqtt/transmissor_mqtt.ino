/*--------------------------------------------------------------------------------------------------
Autor: Thales Martins

Título: Bifrost - Transmissor Software Serial para MQTT

Descrição: O seguinte código recebe as informações do Receptor EspNow via SoftwareSerial e envia via MQTT.

Criado: 05/07/2024
--------------------------------------------------------------------------------------------------*/

//-------------------------------------------------------------------
// Bibliotecas
#include <FS.h>                           // Precisa vir primeiro, se não tudo para de funcionar...
#include <WiFiManager.h>                  //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>                  //https://github.com/bblanchon/ArduinoJson

#include <Arduino.h>                      // Biblioteca para incluir funções padrão do Arduino
#include <ESP8266WiFi.h>                  // Biblioteca Wifi
#include <ESP8266mDNS.h>                  // Biblioteca para configurações de DNS
#include <WiFiUdp.h>                      // Biblioteca que envia dados Udp
#include <PubSubClient.h>                 // Biblioteca para comunicação MQTT
#include <SoftwareSerial.h>               // Biblioteca SoftwareSerial, para comunicação com o outro ESP

//-------------------------------------------------------------------
// Configuração do SoftwareSerial
#define D5 14
#define D6 12
SoftwareSerial serialData(D6, D5);         // RX, TX

//-------------------------------------------------------------------
// Informações Padrão do MQTT e de Rede
char device_name[20] = "Bifrost";          // Nome do Dispositivos, que é enviado para logar no Servidor MQTT
char mqtt_server[40] = "192.168.0.00";     // Servidor que está rodando o Broker MQTT.
char mqtt_port[6] =    "1883";             // Porta que está rodando o MQTT, por padrão é a porta 1883.
char api_user[34] =    "xxxxxx";           // Usuário MQTT.
char api_token[68] =   "xxxxxxxxxxxxxxxx"; // Senha do usuário MQTT;

WiFiClient espClient;                      // Inicializando biblioteca Wifi
PubSubClient client(espClient);            // Inicializando biblioteca MQTT

//-------------------------------------------------------------------
// Variáveis Gerais
bool shouldSaveConfig = false;             // Variável para controlar quando as informações devem ser salvas na memória Flash

//-------------------------------------------------------------------
// Structs de mensagem para a comunicação com tópicos
enum MessageType {PUB, SUB, SET};         // Usa um 'enum' para criar 3 novas variáveis, e atribuir um número a cada uma delas.
MessageType messageType;                  // cria uma variável do tipo MessageType.

// Estrutura de mensagens para publicar em um tópico
typedef struct struct_message_pub {
  byte msgType;                     // variável que define qual é o tipo de mensagem que está sendo passada
  char stateTopic[50];              // variável que armazena o tópico MQTT que será enviado o dado.
  char deviceStatus[50];            // variável que armazena o estado do sensor
} struct_message_pub;

// Estrutura de mensagens para se inscrever e publicar em um tópico
typedef struct struct_message_sub {
  byte msgType;                  // variável que define qual é o tipo de mensagem que está sendo passada
  char stateTopic[50];              // variável que armazena o tópico MQTT que será enviado o dado.
  char deviceStatus[50];            // variável que armazena o estado do sensor
  char commandTopic[50];            // variável que armazena o tópico de comando
} struct_message_sub;

// Estrutura de mensagens para enviar a mudança de estado de um dispositivo via Serial.
typedef struct struct_message_set {
  byte msgType;                     // variável que define qual é o tipo de mensagem que está sendo passada
  char device[50];                  // variável que armazena o tópico
  char deviceStatus[50];            // variável que armazena o estado do sensor
} struct_message_set;

struct_message_pub struct_pub;       // cria um struct de publicação
struct_message_sub struct_sub;       // cria um struct de inscrição

//-------------------------------------------------------------------
void setup() {
  Serial.begin(115200);                         // inicia a serial do ESP
  serialData.begin(9600);                       // inicial a serial entre os dispositivos (SoftwareSerial)

  setup_wifi();                                      // chama a função que inicia as configurações de wifi e leitura de SPIFFS
  client.setServer(mqtt_server, atoi(mqtt_port));    // Setta o servidor e a porta MQTT do servidor
  client.setCallback(callback);                      // Setta qual será a função callback, quando receber informações dos tópicos inscritos.
}

//------------------------------------------------------------------
// Função que lê os valores salvos na memória flash e abre um menu de configuração para alterar esses valores
void setup_wifi() {
  // Descomente a linha abaixo para formatar a memória Flash, PARA TESTES.
  //SPIFFS.format();              

  Serial.println("Tentando montar o Sistema de Arquivos...");
  if (SPIFFS.begin()) {                                       // Tenta iniciar o SPIFFS
    Serial.println("SPIFFS Iniciado com sucesso");

    if (SPIFFS.exists("/config.json")) {                      // Verifica se o arquivo de configuração existe
      Serial.println("Tentando ler arquivo de configs");
      File configFile = SPIFFS.open("/config.json", "r");     // Tenta abrir o arquivo de configurações para leitura

      if (configFile) {                                       // Se conseguir abrir:
        Serial.println("Arquivo de configuração aberto.");
        size_t size = configFile.size();
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);

        DynamicJsonDocument json(1024);
        auto deserializeError = deserializeJson(json, buf.get());
        serializeJson(json, Serial);

        if ( ! deserializeError ) {
          Serial.println("\nparsed json");
          strcpy(device_name, json["device_name"]);
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(api_user, json ["api_user"]);
          strcpy(api_token, json["api_token"]);
        } else {                                             // Se não conseguir abrir o arquivo de configurações,
          Serial.println("Falhou em abrir as configurações JSON");
        }
        configFile.close();                                 // Fecha o arquivo de configurações
      }
    }
  } else {                                                  // Se não conseguir iniciar a SPIFFS
    Serial.println("Falhou em montar o Sistema de Arquivos");
  }

  //---------------------------------
  // Webportal para conectar no wifi com a biblioteca WifiManager
  WiFiManagerParameter custom_device_name("devicename", "Nome", device_name, 20);          // Cria parâmetros para o usuário preencher.
  WiFiManagerParameter custom_mqtt_server("server", "IP Servidor MQTT", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "Porta MQTT", mqtt_port, 6);
  WiFiManagerParameter custom_api_user("apiuser", "Usuário", api_user, 34);
  WiFiManagerParameter custom_api_token("apikey", "Token", api_token, 68);

  WiFiManager wifiManager;                                    // "inicia" a biblioteca, momentâneamente
  wifiManager.setSaveConfigCallback(saveConfigCallback);      //  setta a função callback

  // Adiciona os parâmetros criados anteriormente no Webportal.
  wifiManager.addParameter(&custom_device_name);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_api_user);
  wifiManager.addParameter(&custom_api_token);

  wifiManager.setMinimumSignalQuality(20);                     // define a qualidade minima do sinal
  wifiManager.setTimeout(220);                                 // tempo em segundos para reiniciar
  
  // Abre o WebPortal com as informações passadas, se o esp não se conectar na rede, a função retorna 'false'.
  if (!wifiManager.autoConnect(device_name, "12345678")) {
    Serial.println("Falhou em conectar e esgotou o tempo");
    delay(1000);
    ESP.restart(); // Reinicia o ESP
    delay(1000);
  } Serial.println("conectado...yeey :)");

  // Depois de conectado no Wifi, chama a função 'parameter.getValue()' vai retornar o valor configurado
  // Lendo os valores configurados, e salvando na variável local.
  strcpy(device_name, custom_device_name.getValue());
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(api_user, custom_api_user.getValue());
  strcpy(api_token, custom_api_token.getValue());

  // Printando na serial
  Serial.println("Os valores do Webportal são: ");
  Serial.println("\tdevice_name : " + String(device_name));
  Serial.println("\tmqtt_server : " + String(mqtt_server));
  Serial.println("\tmqtt_port : " + String(mqtt_port));
  Serial.println("\tapi_user : " + String(api_user));
  Serial.println("\tapi_token : " + String(api_token));

  // Se a variável de salvar as configurações for 'true', usa a biblioteca JSON, para salvar na SPIFFS.
  if (shouldSaveConfig) {
    Serial.println("Salvando configurações");
    DynamicJsonDocument json(1024);
    json["device_name"] = device_name;
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["api_user"] = api_user;
    json["api_token"] = api_token;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("Falhou em abrir o arquivo de configurações para escrita");
    }

    serializeJson(json, Serial);
    serializeJson(json, configFile);
    configFile.close();
  }

  Serial.println("IP local");
  Serial.println(WiFi.localIP());
}

//------------------------------------------------------------------
// Função que é executada em LOOP quando a conexão com o servidor cai
void reconnect() {
  while (!client.connected()) {                                 // Enquanto não se conectar com o MQTT
    Serial.print("Tentando conectar MQTT...");                  
    
    if (client.connect(device_name, api_user, api_token)) {    // Envia o nome do dispositivo, usuário e senha para o servior.
      Serial.println("conectado");
    } else {
      Serial.printf("Falhou, rc=%i Tentando novamente em 5 segundos\n\n", client.state()); // Printa o atual estado da conexão.
      delay(1000);
      ESP.restart();                                                                      // Reinicia o ESP.
    }
  }
}

//------------------------------------------------------------------
// Função Callback que Salva as configurações
void saveConfigCallback () {
  Serial.println("Salvando as configurações");
  shouldSaveConfig = true;
}

//-------------------------------------------------------------------
void loop() {
  if (!client.connected()) {                  // se o ESP não estiver conectado com o servidor MQTT
    reconnect();                              // chama a função reconnect()
  }
  client.loop();                              // função loop, para que a conexão MQTT continue funcionando
  serialReceiver();
}

//------------------------------------------------------------------
// Função que lê as informações da Serial
void serialReceiver() {
  if (serialData.available() > 0) {             // Se a quantidade de informações disponiveis na serial for mais do que 0
    byte type;                                  // Cria uma variável 'type', que armazena o primeiro byte de informação
    serialData.readBytes(&type, sizeof(type));  // Lê o primeiro byte da mensagem recebida;

    //Usando um Switch Case, para escolher em qual STRUCT salvar a mensagem, dependendo de qual for o tipo de mensagem.
    switch (type) {
      case PUB:
        // Se a mensagem for de PUB, salva as informações no struct e publica via MQTT.
        serialData.readBytes((char*)&struct_pub.msgType + sizeof(type), sizeof(struct_pub) - sizeof(type)); // Salva todos os dados no struct
        client.publish(struct_pub.stateTopic, struct_pub.deviceStatus);  // Publica o estado no tópico recebido
        Serial.printf("PUB -> msgType= %s mqttTopic= %s, state= %s\n", struct_pub.msgType, struct_pub.stateTopic, struct_pub.deviceStatus);
        break;
      case SUB:
        // Se a mensagem for de SUB, salva as informações no struct, se inscreve no tópico recebido e publica via MQTT.
        serialData.readBytes((char*)&struct_sub.msgType + sizeof(type), sizeof(struct_sub) - sizeof(type)); // Salva todos os dados no struct
        client.publish(struct_sub.stateTopic, struct_sub.deviceStatus);  // Publica o estado no tópico recebido
        client.subscribe(struct_sub.commandTopic);                       // Se inscreve no tópico recebido
        Serial.printf("SUB -> mqttTopic= %s, state= %s, commandTopic= %s\n", struct_sub.stateTopic, struct_sub.deviceStatus, struct_sub.commandTopic);
        break;
      default:
        Serial.printf("Tipo de mensagem desconhecido, recebi type=%i", type); // Debug, caso o byte recebido não seja PUB ou SUB
        break;
    }
  }
}

//-------------------------------------------------------------------
// Função que recebe as informações MQTT dos tópicos inscritos
void callback(char* topic, byte* payload, unsigned int length) {
  String dados;                             // Cria uma variável para converter a payload em string. 
  String convertedTopic = String(topic);    // Cria uma nova variavel e converve a 'topic' (cadeia de caracteres) em String

  // Laço for para converter 'payload' em uma String, armazenando em 'dados'
  for (int i = 0; i < length; i++) {
    char c = (char)payload[i];
    dados += c;
  }

  // Printando dados recebidos, para Debugging
  Serial.printf("Topic=%s convertedTopic=%s Payload=%s Length=%i\n", topic, convertedTopic.c_str(), dados.c_str(), length);

  const int N_POSSIBLE_SET_TOPICS = 2;                                         // Cria uma variável constante, para criar uma lista logo em seguida
  String possibleSetTopics[N_POSSIBLE_SET_TOPICS] = {"/set", "/set_position"}; // Cria uma lista com possíveis tópicos de 'set'.

  // For loop, percorrendo os possíveis tópicos
  for (int i = 0; i < N_POSSIBLE_SET_TOPICS; i++) {
    if (convertedTopic.endsWith(possibleSetTopics[i])) {      // Se o tópico terminar com índece correspondente da lista.
      convertedTopic.replace(possibleSetTopics[i], "");       // Substitua o final por um caracter vazio.

      // Convertendo String para um char* para a função strtok()
      char topicBuffer[convertedTopic.length() + 1];          // Cria um char, passando o tamanho atual da string, +1, para o caracter '\0'
      strcpy(topicBuffer, convertedTopic.c_str());            // Usa a função strcpy() para copiar o valor do tópico para o Buffer.

      // Cria o primeiro token da função strtok()
      char* token = strtok(topicBuffer, "/");                 // Passa buffer como string e "/" como delimitador.

      // Crie uma String para armazenar o último token
      String lastToken;

      // Continua chamando a função strtok(), até que não tenham mais delitimadores ('/') presentes na string[].
      while (token != NULL) {                           // Faz a checagem para ver se o resultado da função foi nulo.
        //Serial.printf("O Token é: %s\n", token);      // PARA DEBBUGIN
        lastToken = String(token);                      // Armazenando o último token em uma String
        token = strtok(NULL, "/");                      // Procura o próximo delimitador na string.
      }

      convertedTopic = lastToken;           // Salva o nome do dispositivo na variável como o valor do último token
      convertedTopic.replace(" ", "");      // Remove os espaços em branco que podem ter ficado no nome.

      Serial.printf("O nome do dispositivo é %s\n", convertedTopic.c_str());
      break;
    }
  }
  
  // Empacotando dados para enviar via SerialSoftware.
  struct_message_set struct_set;                                                            // Cria um struct_set, para enviar os dados
  struct_set.msgType = SET;                                                                 // Define o tipo de mensagem como SET
  strncpy(struct_set.device, convertedTopic.c_str(), sizeof(struct_set.device) - 1);        // Copia o nome do dispostivo para o Struct
  strncpy(struct_set.deviceStatus, dados.c_str(), sizeof(struct_set.deviceStatus) - 1);     // Copia o status do dispositivos para o Struct

  // Certificando que todas as strings terminam com o caracter nulo.
  struct_set.device[sizeof(struct_set.device) - 1] = '\0';
  struct_set.deviceStatus[sizeof(struct_set.deviceStatus) - 1] = '\0';

  // Enviando Dados via SerialSoftware
  serialData.write((uint8_t*)&struct_set, sizeof(struct_set));
}
