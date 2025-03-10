#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <time.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "FS.h"
#include "SPIFFS.h"
#include <math.h>  // Para usar a função sqrt()
#include <PubSubClient.h>  // Biblioteca MQTT


// Definições do sensor DHT22
#define DHTPIN 4         // Pino onde o DHT22 está conectado
#define DHTTYPE DHT22    // Tipo de sensor DHT
DHT dht(DHTPIN, DHTTYPE);

#define DEVICE_ID 1      // ID do dispositivo
#define CSV_FILENAME "/esp02dataset.csv"

// Definições para a rede neural
#define INPUT_NODES 4
#define HIDDEN_NODES_1 16   //64
#define HIDDEN_NODES_2 8   //32
#define HIDDEN_NODES_3 4   //16
#define OUTPUT_NODES 2
#define LEARNING_RATE 0.01
#define THRESHOLD 0.50
// Nome do arquivo CSV
//#define CHUNK_SIZE 1024

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -10800;  // Ajuste para seu fuso horário (-10800 = UTC -3)
const int   daylightOffset_sec = 0;

// Credenciais Wi-Fi
const char* ssid = "VIVOFIBRA-CBF1";
const char* password = "rCsbK2TQHt";
const char* mqtt_server = "192.168.15.11";  // IP do broker MQTT
const int mqtt_port = 1883;                  // Porta do broker MQTT
const char* mqtt_topic = "esp32/esp02/weights";       // Substitua pelo seu tópico MQTT
const char* mqtt_topic_agregado = "esp32/weights_agregado";  // Tópico para receber pesos agregados

WiFiClient espClient;
PubSubClient client(espClient);

// Tamanho do chunk (partição) a ser enviado
const int CHUNK_SIZE = 256;  // Ajuste conforme necessário

// URL do servidor HTTP
const char* serverUrl = "http://192.168.15.11:8000/";  

// Variáveis globais para pesos
float* weights1;
float* weights2;
float* weights3;
float* weights_out;

#define TOTAL_EPOCHS 5



// Estrutura para armazenar métricas
struct Metrics {
    float mse;
    float mae;
    float accuracy;
    float precision;
    float recall;
    float f1;
};

// Função para avaliar o modelo e calcular métricas
Metrics evalModel(float input[INPUT_NODES], float output[INPUT_NODES], float true_label, int total_samples) {
    Metrics metrics = {0, 0, 0, 0, 0, 0};  // Inicializar estrutura para métricas
    float reconstruction_error = 0.0;
    float total_absolute_error = 0.0;
    int true_positive = 0;
    int false_positive = 0;
    int true_negative = 0;
    int false_negative = 0;

    // Calcular o erro de reconstrução (MSE e MAE)
    for (int i = 0; i < INPUT_NODES; i++) {
        float error = input[i] - output[i];
        reconstruction_error += pow(error, 2);  // Soma dos erros quadráticos (MSE)
        total_absolute_error += abs(error);     // Soma dos erros absolutos (MAE)
    }

    // Calcular MSE e MAE
    metrics.mse = reconstruction_error / INPUT_NODES;
    metrics.mae = total_absolute_error / INPUT_NODES;

    // Prever anomalia com base no erro de reconstrução
    float predicted_label = (reconstruction_error > THRESHOLD) ? 1.0 : 0.0;

    // Contabilizar TP, FP, TN, FN
    if (true_label == 1.0) {  // Caso seja uma anomalia verdadeira
        if (predicted_label == 1.0) {
            true_positive++;
        } else {
            false_negative++;
        }
    } else {  // Caso seja um dado normal
        if (predicted_label == 0.0) {
            true_negative++;
        } else {
            false_positive++;
        }
    }

    // Calcular precisão, recall, e F1-score
    if ((true_positive + false_positive) > 0) {
        metrics.precision = (float)true_positive / (true_positive + false_positive);
    }
    if ((true_positive + false_negative) > 0) {
        metrics.recall = (float)true_positive / (true_positive + false_negative);
    }
    if ((metrics.precision + metrics.recall) > 0) {
        metrics.f1 = 2 * ((metrics.precision * metrics.recall) / (metrics.precision + metrics.recall));
    }

    // Calcular acurácia
    int total_predictions = true_positive + true_negative + false_positive + false_negative;
    if (total_predictions > 0) {
        metrics.accuracy = (float)(true_positive + true_negative) / total_predictions;
    }

    return metrics;  // Retornar as métricas calculadas
}

void freeMemory(){
  // Memória heap livre no momento
  size_t freeHeap = esp_get_free_heap_size();
  Serial.print("Memória Heap Livre: ");
  Serial.print(freeHeap);
  Serial.println(" bytes");

  // Memória heap mínima livre (menor valor alcançado desde o boot)
  size_t minFreeHeap = esp_get_minimum_free_heap_size();
  Serial.print("Memória Heap Mínima Livre: ");
  Serial.print(minFreeHeap);
  Serial.println(" bytes");
}

// Inicialização e conexão
// Função para inicializar SPIFFS
void initSPIFFS() {
    if (!SPIFFS.begin(true)) {
        Serial.println("Erro ao montar o sistema de arquivos SPIFFS.");
    } else {
        Serial.println("SPIFFS montado com sucesso.");
    }
}

// Conectando ao Wi-Fi

void connectToWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Conectando-se ao Wi-Fi...");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  
  Serial.println(" Conectado!");
}



// funções de manipulação dos arquivos
void deleteCSV() {
    if (SPIFFS.exists(CSV_FILENAME)) {  // Verificar se o arquivo existe
        if (SPIFFS.remove(CSV_FILENAME)) {  // Tentar remover o arquivo
            Serial.println("Arquivo esp01dataset.csv apagado com sucesso.");
        } else {
            Serial.println("Erro ao apagar o arquivo esp01dataset.csv.");
        }
    } else {
        Serial.println("Arquivo esp01dataset.csv não encontrado.");
    }
}



// Função para salvar os dados no CSV
void saveDataToCSV(float temperature, float humidity, float luminosity, float voltage, float anomaly) {
    File file = SPIFFS.open(CSV_FILENAME, FILE_APPEND);
    if (!file) {
        Serial.println("Erro ao abrir o arquivo CSV.");
        return;
    }
    // Obter o horário atual
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Erro ao obter a hora");
        return;
    }
    
    // Formatar o tempo no formato desejado
    char timeStringBuff[50];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);

    // Grava os dados no formato CSV
    //String dataLine = "1,";  // 'device=1' fixo
    //dataLine += "2024-10-15 12:00:00,";  // Data e hora obtida via NTP
    //dataLine += String(timeStringBuff) + ",";  // Data e hora
    String dataLine = String(temperature) + ",";
    dataLine += String(humidity) + ",";
    dataLine += String(luminosity) + ",";
    dataLine += String(voltage) + ",";
    dataLine += String(anomaly) + "\n";

    file.print(dataLine);
    file.close();
    //Serial.println("Dados salvos no arquivo CSV.");
}

void checkFileContent(String filename) {
  File file = SPIFFS.open(filename, FILE_READ);
  if (!file) {
    Serial.println("Erro ao abrir o arquivo para leitura.");
    return;
  }

//  Serial.print("Conteúdo do arquivo:");
//  while (file.available()) {
//    Serial.write(file.read());  // Imprime o conteúdo do arquivo no Serial Monitor
//  }
  file.close();
}
void freeWeights() {
    if (weights1 != NULL) {
        free(weights1);
        weights1 = NULL;  
    }
    if (weights2 != NULL) {
        free(weights2);
        weights2 = NULL;
    }
    if (weights3 != NULL) {
        free(weights3);
        weights3 = NULL;
    }
    if (weights_out != NULL) {
        free(weights_out);
        weights_out = NULL;
    }
};
void listWeights(){
//Serial.println("Verificando inicialização dos pesos...");
//
// Verificar weights1
for (int i = 0; i < HIDDEN_NODES_1 * INPUT_NODES; i++) {
    Serial.print("weights1["); Serial.print(i); Serial.print("] = ");
    Serial.println(weights1[i]);
}

// Verificar weights2
for (int i = 0; i < HIDDEN_NODES_2 * HIDDEN_NODES_1; i++) {
    Serial.print("weights2["); Serial.print(i); Serial.print("] = ");
    Serial.println(weights2[i]);
}

// Verificar weights3
for (int i = 0; i < HIDDEN_NODES_3 * HIDDEN_NODES_2; i++) {
    Serial.print("weights3["); Serial.print(i); Serial.print("] = ");
    Serial.println(weights3[i]);
}

// Verificar weights_out
for (int i = 0; i < OUTPUT_NODES * HIDDEN_NODES_3; i++) {
    Serial.print("weights_out["); Serial.print(i); Serial.print("] = ");
    Serial.println(weights_out[i]);
}
   Serial.println("Fim verificação inicialização dos pesos");
}

// Função para salvar os pesos localmente no arquivo
void saveWeightsToFile(String weightsJson) {
    File file = SPIFFS.open("/weights.json", FILE_WRITE);
    if (!file) {
        Serial.println("Erro ao abrir o arquivo para salvar os pesos.");
        return;
    }

    if (file.print(weightsJson)) {
        Serial.println("Pesos salvos com sucesso no arquivo /weights.json.");
    } else {
        Serial.println("Erro ao salvar os pesos no arquivo.");
    }

    file.close();
    // Passo 1: Verificar o conteúdo do arquivo após salvar
    checkFileContent("/weightsesp01.json");
}



// Função para enviar pesos particionados via MQTT
void sendWeightsViaMQTTPartitioned(const String& espID) {
    String filename = "/weights.json";  
    String mqtt_topic = "esp32/" + espID + "/weights";  // Tópico dinâmico baseado no espID

    if (!SPIFFS.exists(filename)) {
        Serial.println("Arquivo de pesos não encontrado para o ESP: " + espID);
        return;
    }

    // Abrir o arquivo
    File file = SPIFFS.open(filename, FILE_READ);
    if (!file) {
        Serial.println("Erro ao abrir o arquivo de pesos");
        return;
    }

    // Ler o arquivo em partes e enviar via MQTT
    Serial.println("Iniciando o envio dos pesos particionados via MQTT...");

    // Enviar o flag de início
    client.publish(mqtt_topic.c_str(), "INICIO_TRANSMISSAO");

    // Definir o tamanho do chunk (partição)
    const int CHUNK_SIZE = 128;  // Ajuste conforme necessário
    char buffer[CHUNK_SIZE + 1];  // Buffer com espaço para o terminador de string

    // Enviar o arquivo em partes
    while (file.available()) {
        int bytesRead = file.readBytes(buffer, CHUNK_SIZE);
        
        // Adicionar um terminador de string (null-terminator)
        buffer[bytesRead] = '\0';
        
//        // Verificar quantos bytes foram lidos
//        Serial.print("Bytes lidos: ");
//        Serial.println(bytesRead);
//        
//        // Verificar o conteúdo do buffer que será enviado
//        Serial.print("Buffer enviado: ");
//        Serial.write(buffer, bytesRead);  // Imprime o conteúdo do buffer no Serial Monitor

        if (bytesRead > 0) {
            // Enviar o chunk via MQTT
            if (client.publish(mqtt_topic.c_str(), buffer, bytesRead)) {
                //Serial.println("Dados enviados com sucesso via MQTT.");
            } else {
                Serial.println("Erro ao enviar dados via MQTT.");
            }
        } else {
            Serial.println("Nenhum dado lido.");
        }

        delay(100);  // Pequeno atraso para evitar sobrecarregar a transmissão
    }

    // Enviar o flag de fim
    client.publish(mqtt_topic.c_str(), "FIM_TRANSMISSAO");

    Serial.println("Envio completo.");
    file.close();
}


void checkCSVFileSize() {
    File file = SPIFFS.open(CSV_FILENAME, FILE_READ);
    if (!file) {
        Serial.println("Erro ao abrir o arquivo CSV para leitura.");
        return;
    }

    size_t fileSize = file.size();  // Obtém o tamanho do arquivo em bytes
    Serial.printf("Tamanho do arquivo CSV: %d bytes\n", fileSize);

    file.close();
}

// Função de tarefa para enviar dados a cada 10 minutos
// funções NN

// Função Sigmoid
float sigmoid(float x) {
  return 1 / (1 + exp(-x));  
}

// Derivada da Sigmoid
float sigmoid_derivative(float x) {
  return x * (1 - x);  
}

// Função Softmax para a camada de saída
void softmax(float* input, int size) {
  float max = input[0];
  for (int i = 1; i < size; i++) {
    if (input[i] > max) {
      max = input[i];
    }
  }

  float sum = 0.0;
  for (int i = 0; i < size; i++) {
    input[i] = exp(input[i] - max);
    sum += input[i];
  }

  for (int i = 0; i < size; i++) {
    input[i] /= sum;
  }
}

// Variáveis e normalização

// Função para normalizar os valores de entrada com tratamento para valores fora do intervalo
float normalize(float value, float min, float max) {
    // Verificar se min == max para evitar divisão por zero
    if (min == max) {
        Serial.println("Aviso: min e max são iguais. Retornando 0 para evitar divisão por zero.");
        return 0;
    }

    // Verificar se o valor está fora do intervalo esperado e evitar NaN/Inf
    if (isnan(value) || isinf(value)) {
        Serial.println("Erro: Valor inválido encontrado. Substituindo por 0.");
        return 0;
    }

    return (value - min) / (max - min);
}

void normalize_input(float input[INPUT_NODES]) {
  input[0] = normalize(input[0], 0, 50);  // Normalizar temperatura
  input[1] = normalize(input[1], 0, 100); // Normalizar umidade
  input[2] = normalize(input[2], 95, 150);  // Normalizar luminosidade
  input[3] = normalize(input[3], 2.9, 7.3);  // Normalizar voltagem
}

void initialize_weights(float* weights, int rows, int cols) {
  for (int i = 0; i < rows * cols; i++) {
    weights[i] = ((float) random(-500, 500)) / 1000.0;  // Valores entre -0.5 e 0.5
  }  
}

void initialize_weights_xavier(float* weights, int rows, int cols) {
    // Calcular o limite para a inicialização Xavier
    float limit = sqrt(6.0 / (rows + cols));  // Xavier para sigmoid/tanh
    for (int i = 0; i < rows * cols; i++) {
        // Inicializar pesos com valores entre -limit e limit
        weights[i] = ((float) random(-1000, 1000) / 1000.0) * limit;
    }
}

// Função para leitura de luminosidade simulada (valores mais amplos)
float readLuminosity() {
  return random(95, 100);  // Mais variação
}

// Função para leitura de voltagem simulada (valores mais amplos)
float readVoltage() {
  return random(290, 330) / 100.0;  // Gera voltagem entre 0 e 5.6
}

// Função auxiliar para dividir uma string em partes, com base em um delimitador
void split(const String &str, char delimiter, String result[], int maxParts) {
    int currentPart = 0;
    int start = 0;
    int end = 0;
    
    while (end < str.length() && currentPart < maxParts) {
        end = str.indexOf(delimiter, start);
        if (end == -1) {
            result[currentPart] = str.substring(start);  // Última parte
            break;
        } else {
            result[currentPart] = str.substring(start, end);
        }
        start = end + 1;
        currentPart++;
    }
}
// Função para converter os pesos da rede neural em formato JSON
String convertWeightsToJson() {
  DynamicJsonDocument doc(1024);
  JsonArray w1 = doc.createNestedArray("weights1");
  JsonArray w2 = doc.createNestedArray("weights2");
  JsonArray w3 = doc.createNestedArray("weights3");
  JsonArray w_out = doc.createNestedArray("weights_out");

  for (int i = 0; i < HIDDEN_NODES_1 * INPUT_NODES; i++) w1.add(weights1[i]);
  for (int i = 0; i < HIDDEN_NODES_2 * HIDDEN_NODES_1; i++) w2.add(weights2[i]);
  for (int i = 0; i < HIDDEN_NODES_3 * HIDDEN_NODES_2; i++) w3.add(weights3[i]);
  for (int i = 0; i < OUTPUT_NODES * HIDDEN_NODES_3; i++) w_out.add(weights_out[i]);

  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}
void loadWeightsFromFile(String fileName) {
   
    if (SPIFFS.exists(fileName)){ //"/weights.json")) {
        Serial.printf("Carregando pesos do arquivo: %s\n",fileName);
        File file = SPIFFS.open(fileName, FILE_READ);      //("/weights.json", FILE_READ);
        if (file) {
            // Lê o conteúdo do arquivo de pesos
            String weightsJson = file.readString();
            file.close();

            // Parse o JSON e carregar os pesos
            DynamicJsonDocument doc(1024);
            DeserializationError error = deserializeJson(doc, weightsJson);
            if (error) {
                Serial.println("Erro ao carregar pesos do arquivo JSON.");
                return;
            }

            JsonArray w1 = doc["weights1"];
            JsonArray w2 = doc["weights2"];
            JsonArray w3 = doc["weights3"];
            JsonArray w_out = doc["weights_out"];

            // Carregar os pesos nos arrays globais
            for (int i = 0; i < HIDDEN_NODES_1 * INPUT_NODES; i++) {
                weights1[i] = w1[i];
            }
            for (int i = 0; i < HIDDEN_NODES_2 * HIDDEN_NODES_1; i++) {
                weights2[i] = w2[i];
            }
            for (int i = 0; i < HIDDEN_NODES_3 * HIDDEN_NODES_2; i++) {
                weights3[i] = w3[i];
            }
            for (int i = 0; i < OUTPUT_NODES * HIDDEN_NODES_3; i++) {
                weights_out[i] = w_out[i];
            }

            Serial.println("Pesos carregados com sucesso do arquivo.");
        } else {
            Serial.println("Erro ao abrir o arquivo de pesos.");
        }
    } else {
       Serial.println("Arquivo de pesos não encontrado. Inicializando pesos aleatoriamente.");

        // Inicialize os pesos usando Xavier se o arquivo não for encontrado
        initialize_weights_xavier(weights1, HIDDEN_NODES_1, INPUT_NODES);
        initialize_weights_xavier(weights2, HIDDEN_NODES_2, HIDDEN_NODES_1);
        initialize_weights_xavier(weights3, HIDDEN_NODES_3, HIDDEN_NODES_2);
        initialize_weights_xavier(weights_out, OUTPUT_NODES, HIDDEN_NODES_3);
    }
}

// NN
// Função de propagação para frente para o autoencoder
void forward_autoencoder(float input[INPUT_NODES], float* weights1, float* weights2, 
                         float* weights3, float* weights_out, float output[INPUT_NODES], 
                         float hidden1[HIDDEN_NODES_1], float hidden2[HIDDEN_NODES_2], 
                         float hidden3[HIDDEN_NODES_3]) {

  // Camada 1
//  Serial.println("Camada 1");
  for (int i = 0; i < HIDDEN_NODES_1; i++) {
    hidden1[i] = 0;
    for (int j = 0; j < INPUT_NODES; j++) {
      hidden1[i] += input[j] * weights1[i * INPUT_NODES + j];
    }
    hidden1[i] = sigmoid(hidden1[i]);
  }

  // Camada 2
//  Serial.println("Camada 2");
  for (int i = 0; i < HIDDEN_NODES_2; i++) {
    hidden2[i] = 0;
    for (int j = 0; j < HIDDEN_NODES_1; j++) {
      hidden2[i] += hidden1[j] * weights2[i * HIDDEN_NODES_1 + j];
    }
    hidden2[i] = sigmoid(hidden2[i]);
  }

  // Camada 3
//  Serial.println("Camada 3");
  for (int i = 0; i < HIDDEN_NODES_3; i++) {
    hidden3[i] = 0;
    for (int j = 0; j < HIDDEN_NODES_2; j++) {
      hidden3[i] += hidden2[j] * weights3[i * HIDDEN_NODES_2 + j];
    }
    hidden3[i] = sigmoid(hidden3[i]);
  }

  // Camada de saída (reconstrução)
//  Serial.println("Reconstrução");
  for (int i = 0; i < INPUT_NODES; i++) {
    output[i] = 0;
    for (int j = 0; j < HIDDEN_NODES_3; j++) {
      output[i] += hidden3[j] * weights_out[i * HIDDEN_NODES_3 + j];
    }
    output[i] = sigmoid(output[i]);  // Aplicando sigmoid na reconstrução
  }
}

void backpropagate_autoencoder(float input[INPUT_NODES], float* weights1, float* weights2, 
                               float* weights3, float* weights_out, float output[INPUT_NODES], 
                               float hidden1[HIDDEN_NODES_1], float hidden2[HIDDEN_NODES_2], 
                               float hidden3[HIDDEN_NODES_3]) {
    // Verifique se weights3 e hidden3_error estão corretamente alocados
    if (!weights1 || !weights2 || !weights3 || !weights_out) {
        Serial.println("Erro ao alocar memória para os pesos.");
        return;  // Saia da função se a memória não estiver alocada
    }
//
//    Serial.println("inicio backproagate");
    float* output_error = (float*)malloc(INPUT_NODES * sizeof(float));
    float* output_delta = (float*)malloc(INPUT_NODES * sizeof(float));
    float* hidden2_error = (float*)malloc(HIDDEN_NODES_2 * sizeof(float));
    
    for (int i = 0; i < INPUT_NODES; i++) {
        output_error[i] = 0.0;
        output_delta[i] = 0.0;
    }
    for (int i = 0; i < HIDDEN_NODES_2; i++) {
        hidden2_error[i] = 0.0;
    }
    // Verificar se a alocação foi bem-sucedida
    if (!output_error || !output_delta || !hidden2_error) {
        Serial.println("Erro: Não foi possível alocar memória.");
        return;
    }

    for (int i = 0; i < INPUT_NODES; i++) {
        output_error[i] = input[i] - output[i];  // Erro de reconstrução
        output_delta[i] = output_error[i] * sigmoid_derivative(output[i]);
    }

    // Adiciona um pequeno delay para liberar CPU para outras tarefas
    vTaskDelay(10 / portTICK_PERIOD_MS);

    // Camada 3 -> Saída
//    Serial.println("backproagate - 3 camada - saída");
    float* hidden3_error = (float*)malloc(HIDDEN_NODES_3 * sizeof(float));
    if (!hidden3_error) {
        Serial.println("Erro ao alocar hidden3_error.");
        return;
    }
    
    // Inicializando os valores de hidden3_error para zero
    for (int i = 0; i < HIDDEN_NODES_3; i++) {
        hidden3_error[i] = 0.0;
    }

    if (!hidden3_error) {
        Serial.println("Erro: Não foi possível alocar memória para hidden3_error.");
        return;
    }

    for (int i = 0; i < INPUT_NODES; i++) {
//        Serial.print("c 3 -> S i = "); Serial.println(i);
        for (int j = 0; j < HIDDEN_NODES_3; j++) {
//            Serial.print("j = "); Serial.println(j);
//            Serial.println(hidden3_error[j]);
//            Serial.println(output_delta[i]);
//            Serial.println(i * HIDDEN_NODES_3 + j);
//            Serial.println("ini");
//            Serial.println(weights_out[i * HIDDEN_NODES_3 + j]);
            hidden3_error[j] += output_delta[i] * weights_out[i * HIDDEN_NODES_3 + j];

            weights_out[i * HIDDEN_NODES_3 + j] += LEARNING_RATE * output_delta[i] * hidden3[j];

        }
    }

    // Adiciona delay
    vTaskDelay(10 / portTICK_PERIOD_MS);

    // Camada 2 -> Camada 3
//    Serial.println("backproagate - 3 camada - saída");
    for (int i = 0; i < HIDDEN_NODES_3; i++) {
          
        if (i >= HIDDEN_NODES_3) {
            Serial.println("Erro: Índice i fora dos limites em hidden3_error.");
            return;
        }
//          Serial.println("h3_err: ");
//          Serial.println(hidden3_error[i]);
//          Serial.println("sig");
//          Serial.println(sigmoid_derivative(hidden3[i]));
        float delta = hidden3_error[i] * sigmoid_derivative(hidden3[i]);
//          Serial.println("Delta: ");
//          Serial.println(delta);
        for (int j = 0; j < HIDDEN_NODES_2; j++) {
//            Serial.print("j = "); Serial.println(j);
            if (j >= HIDDEN_NODES_2) {
                Serial.println("Erro: Índice j fora dos limites em weights3.");
                return;
            }
//            Serial.println("Peso x hdn_2: ");
//            Serial.println(weights3[i * HIDDEN_NODES_2 + j]);
            hidden2_error[j] += delta * weights3[i * HIDDEN_NODES_2 + j];
            weights3[i * HIDDEN_NODES_2 + j] += LEARNING_RATE * delta * hidden2[j];
        }
    }

    // Adiciona delay
    vTaskDelay(10 / portTICK_PERIOD_MS);

    // Camada 1 -> Camada 2
//    Serial.println("backproagate - 1 - camada - saída");
    float* hidden1_error = (float*)malloc(HIDDEN_NODES_1 * sizeof(float));
    if (!hidden1_error) {
        Serial.println("Erro: Não foi possível alocar memória para hidden1_error.");
        return;
    }
    for (int i = 0; i < HIDDEN_NODES_1; i++) {
        hidden1_error[i] = 0.0;
    }
    for (int i = 0; i < HIDDEN_NODES_2; i++) {
//        Serial.print("c 1-> 2 i = "); Serial.println(i);
        if (i >= HIDDEN_NODES_2) {
            Serial.println("Erro: Índice i fora dos limites em hidden2_error ou hidden2.");
            return;
        }
        float delta = hidden2_error[i] * sigmoid_derivative(hidden2[i]);

        for (int j = 0; j < HIDDEN_NODES_1; j++) {
//            Serial.print("j = "); Serial.println(j);
            if (j >= HIDDEN_NODES_1) {
                Serial.println("Erro: Índice j fora dos limites em hidden1 ou weights2.");
                return;
            }
            hidden1_error[j] += delta * weights2[i * HIDDEN_NODES_1 + j];
            weights2[i * HIDDEN_NODES_1 + j] += LEARNING_RATE * delta * hidden1[j];
        }
    }

    // Adiciona delay
    vTaskDelay(10 / portTICK_PERIOD_MS);

    // Entrada -> Camada 1
//    Serial.println("backproagate - entrada");
    for (int i = 0; i < HIDDEN_NODES_1; i++) {
        float delta = hidden1_error[i] * sigmoid_derivative(hidden1[i]);
        for (int j = 0; j < INPUT_NODES; j++) {
            weights1[i * INPUT_NODES + j] += LEARNING_RATE * delta * input[j];
        }
    }

    // Libera a memória alocada
    free(output_error);
    free(output_delta);
    free(hidden3_error);
    free(hidden2_error);
    free(hidden1_error);
}


void train_autoencoder(float input[INPUT_NODES], float* weights1, float* weights2, 
                       float* weights3, float* weights_out, int epochs) {

  float hidden1[HIDDEN_NODES_1];
  float hidden2[HIDDEN_NODES_2];
  float hidden3[HIDDEN_NODES_3];
  float output[INPUT_NODES];  // Para o autoencoder, a saída tem o mesmo tamanho que a entrada

  for (int epoch = 0; epoch < epochs; epoch++) {
    Serial.print("Iniciando época: ");
    Serial.println(epoch);
    // Forward pass

    forward_autoencoder(input, weights1, weights2, weights3, weights_out, output, hidden1, hidden2, hidden3);


    // Retropropagação
    backpropagate_autoencoder(input, weights1, weights2, weights3, weights_out, output, hidden1, hidden2, hidden3);

    // Exibe o erro da época atual (erro de reconstrução)
    float total_error = 0;
    for (int i = 0; i < INPUT_NODES; i++) {
      total_error += pow(input[i] - output[i], 2);
    }
    Serial.print("Epoch ");
    Serial.print(epoch);
    Serial.print(": Error = ");
    Serial.println(total_error / 2.0);  // Erro quadrático médio
    vTaskDelay(100 / portTICK_PERIOD_MS);  // Pausa para evitar timeout
  }
}

void evalFinalTraining() {
    File file = SPIFFS.open(CSV_FILENAME, FILE_READ);
    if (!file) {
        Serial.println("Erro ao abrir o arquivo CSV para avaliação.");
        return;
    }

    String line;
    bool isFirstLine = true;

    // Inicializar métricas acumuladas
    Metrics final_metrics;
    final_metrics.mse = 0;
    final_metrics.mae = 0;
    final_metrics.accuracy = 0;
    
    int total_samples = 0;
    int TP = 0, FP = 0, FN = 0; // Verdadeiros positivos, falsos positivos, falsos negativos

    // Ler o arquivo CSV novamente para avaliar o modelo
    while (file.available()) {
        line = file.readStringUntil('\n');
        if (isFirstLine) {
            isFirstLine = false;
            continue;  // Pular a primeira linha (cabeçalho)
        }

        String parts[5];
        split(line, ',', parts, 5);

        // Verificar e validar os valores
        if (parts[0].toFloat() == 0.0 || parts[1].toFloat() == 0.0 || parts[2].toFloat() == 0.0 || parts[3].toFloat() == 0.0 || 
            isnan(parts[0].toFloat()) ||isnan(parts[1].toFloat()) ||isnan(parts[2].toFloat()) || isnan(parts[3].toFloat()) || isnan(parts[4].toFloat())) {
            Serial.println("Erro: Linha com valor inválido encontrada. Pulando essa linha.");
            continue;
        }

        // Preparar os dados de entrada
        float input[INPUT_NODES];
        input[0] = parts[0].toFloat();  // Temperatura
        input[1] = parts[1].toFloat();  // Umidade
        input[2] = parts[2].toFloat();  // Luminosidade
        input[3] = parts[3].toFloat();  // Voltagem

        // Normalizar entradas
        normalize_input(input);

        // Rótulo verdadeiro (true_label) da anomalia
        float true_label = parts[4].toFloat();  // Aqui você captura o valor da última coluna

        // Variáveis para armazenar as saídas das camadas ocultas e de saída
        float hidden1[HIDDEN_NODES_1];
        float hidden2[HIDDEN_NODES_2];
        float hidden3[HIDDEN_NODES_3];
        float output[INPUT_NODES];

        // Realizar a inferência (propagação para frente)
        forward_autoencoder(input, weights1, weights2, weights3, weights_out, output, hidden1, hidden2, hidden3);

        // Calcular as métricas de cada amostra
        Metrics sample_metrics = evalModel(input, output, true_label, 1);

        // Acumular as métricas
        final_metrics.mse += sample_metrics.mse;
        final_metrics.mae += sample_metrics.mae;
        final_metrics.accuracy += sample_metrics.accuracy;

        // Calcular TP, FP, FN
        bool is_anomaly_predicted = (output[0] > THRESHOLD); // Ajuste o limiar de decisão
        bool is_anomaly_actual = (true_label == 1.0); // Supondo que 1.0 representa anomalia

        if (is_anomaly_predicted && is_anomaly_actual) {
            TP++;  // Verdadeiro positivo
        } else if (is_anomaly_predicted && !is_anomaly_actual) {
            FP++;  // Falso positivo
        } else if (!is_anomaly_predicted && is_anomaly_actual) {
            FN++;  // Falso negativo
        }

        total_samples++;  // Contar o número total de amostras
    }

    file.close();

    // Calcular precision, recall, e F1-Score
    float precision = (TP + FP > 0) ? ((float)TP / (TP + FP)) : 0;
    float recall = (TP + FN > 0) ? ((float)TP / (TP + FN)) : 0;
    float f1_score = (precision + recall > 0) ? (2 * (precision * recall) / (precision + recall)) : 0;

    // Calcular e exibir as métricas finais médias
    Serial.printf("Avaliação final - MSE: %.6f, MAE: %.6f, Acurácia: %.2f%%\n", 
                  final_metrics.mse / total_samples, 
                  final_metrics.mae / total_samples, 
                  final_metrics.accuracy / total_samples * 100);
    Serial.printf("Precision: %.2f%%, Recall: %.2f%%, F1-Score: %.2f%%\n", 
                  precision * 100, recall * 100, f1_score * 100);
}

void monitorMemory(int epoch, int contAmostras) {
    if (contAmostras % 300 == 0) { // Ajuste o valor conforme necessário (ex: a cada 1000 amostras)
        Serial.printf("Época %d - Amostra %d: Memória Heap Livre: %d bytes, Marca d'água da Pilha: %d bytes\n",
                      epoch, contAmostras, ESP.getFreeHeap(), uxTaskGetStackHighWaterMark(NULL));
    }
}


void loadAndTrainFromCSV() {
    File file = SPIFFS.open(CSV_FILENAME, FILE_READ);
    if (!file) {
        Serial.println("Erro ao abrir o arquivo CSV para treinamento.");
        return;
    }

    String line;
    bool isFirstLine = true;
    int total_samples = 0;
    unsigned long startTime = millis();  // Captura o tempo no início do treinamento
    float total_mse_all_epochs = 0.0;
    float total_mae_all_epochs = 0.0;
    // Iterar pelas épocas
    for (int epoch = 0; epoch < TOTAL_EPOCHS; epoch++) {
        float total_mse = 0.0;
        float total_mae = 0.0;
        int sampleCount = 0;
        float total_error_epoch = 0.0;  // Variável para acumular o erro da época
        int contAmostras = 0;
        Serial.printf("Iniciando época %d\n", epoch);

        // Voltar o ponteiro do arquivo para o início para reprocessar todas as amostras
        file.seek(0);

        isFirstLine = true; // Resetar para pular o cabeçalho novamente
        // Ler todas as amostras do CSV
        while (file.available()) {
            line = file.readStringUntil('\n');

            if (isFirstLine) {
                isFirstLine = false;
                continue;  // Pular a primeira linha (cabeçalho)
            }

            String parts[5];  // Ajustar o número de colunas
            split(line, ',', parts, 5);

            // Validar se os campos críticos são números válidos
            if (parts[0].toFloat() == 0.0 || parts[1].toFloat() == 0.0 || parts[2].toFloat() == 0.0 || parts[3].toFloat() == 0.0 ||
                isnan(parts[0].toFloat()) || isnan(parts[1].toFloat()) ||
                isnan(parts[2].toFloat()) || isnan(parts[3].toFloat()) || isnan(parts[4].toFloat())) {
                Serial.println("Erro: Linha com valor inválido encontrada. Pulando essa linha.");
                continue;  // Pular linha inválida
            }
            //Serial.println(line);
            // Preparar os dados de entrada
            float input[INPUT_NODES];
            input[0] = parts[0].toFloat();  // Temperatura
            input[1] = parts[1].toFloat();  // Umidade
            input[2] = parts[2].toFloat();  // Luminosidade
            input[3] = parts[3].toFloat();  // Voltagem

            // Normalizar entradas
            normalize_input(input);
            // Rótulo verdadeiro (true_label) da anomalia
            float true_label = parts[4].toFloat();  // Aqui você captura o valor da última coluna
            //Serial.printf("Época %d - Amostra %d: \n",epoch, contAmostras);
            // Variáveis para armazenar as saídas das camadas ocultas e de saída
            float hidden1[HIDDEN_NODES_1];
            float hidden2[HIDDEN_NODES_2];
            float hidden3[HIDDEN_NODES_3];
            float output[INPUT_NODES];

            // Forward pass
            forward_autoencoder(input, weights1, weights2, weights3, weights_out, output, hidden1, hidden2, hidden3);

            // Retropropagação
            backpropagate_autoencoder(input, weights1, weights2, weights3, weights_out, output, hidden1, hidden2, hidden3);
            contAmostras++; 
            //Serial.printf("Amostras: %d, MSE: %.6f, MAE: %.6f\n", sampleCount, epoch_metrics.mse, epoch_metrics.mae);  
           
//            // Calcular o erro de reconstrução para esta amostra
//            float error = 0.0;
//            for (int i = 0; i < INPUT_NODES; i++) {
//                error += pow(input[i] - output[i], 2);
//            }
//            total_error_epoch += error;  // Acumular erro para a época
            
            Metrics epoch_metrics = evalModel(input, output, true_label, sampleCount);

            // Acumular os erros e outras métricas
            total_mse += epoch_metrics.mse;
            total_mae += epoch_metrics.mae;
 
            monitorMemory(epoch, contAmostras); 
            sampleCount++;
            total_samples++;
   
        }
        freeMemory();
        // Exibir o erro médio da época
        if (sampleCount > 0) {
            float mse_epoch_avg = total_mse / sampleCount;
            float mae_epoch_avg = total_mae / sampleCount;
            Serial.printf("Época %d - MSE Médio: %.6f, MAE Médio: %.6f\n", epoch, mse_epoch_avg, mae_epoch_avg);
            
            // Acumular o MSE e MAE para todas as épocas
            total_mse_all_epochs += total_mse;
            total_mae_all_epochs += total_mae;
        } else {
            Serial.println("Nenhuma amostra válida processada na época.");
        }

    }
    unsigned long endTime = millis();
    float trainingTimeSeconds = (endTime - startTime) / 1000.0;
    Serial.printf("Treinamento concluído em: %.2f segundos\n", trainingTimeSeconds);

    // Exibir o número total de amostras processadas
    Serial.printf("Total de amostras processadas no treinamento: %d\n", total_samples);

    // Calcular e exibir o MSE e MAE médios finais
    if (total_samples > 0) {
        Serial.printf("MSE Final Médio: %.6f, MAE Final Médio: %.6f\n",
                      total_mse_all_epochs / total_samples, 
                      total_mae_all_epochs / total_samples);
    }

    file.close();

    // Avaliação final após o treinamento
    evalFinalTraining();
}

void detectAnomaly(float input[INPUT_NODES], float output[INPUT_NODES]) {
  float reconstruction_error = 0.0;
  for (int i = 0; i < INPUT_NODES; i++) {
    reconstruction_error += pow(input[i] - output[i], 2);  // Soma dos erros quadráticos
  }

  if (reconstruction_error > THRESHOLD) {
    Serial.printf("Anomalia detectada com base no erro de reconstrução: %.6f\n", reconstruction_error);
  } else {
    Serial.printf("Dados normais. Erro de reconstrução: %.6f\n", reconstruction_error);
  }

 
}

void isAnomaly(float temperature, float humidity, float luminosity, float voltage) {
    // Preparar os dados de entrada para a inferência
    float input[INPUT_NODES];

    input[0] = temperature;
    input[1] = humidity;
    input[2] = luminosity;
    input[3] = voltage;

    // Normalizar os dados de entrada
    normalize_input(input);

    // Variáveis para armazenar as saídas das camadas ocultas e de saída
    float* hidden1 = (float*)calloc(HIDDEN_NODES_1, sizeof(float));
    float* hidden2 = (float*)calloc(HIDDEN_NODES_2,sizeof(float));
    float* hidden3 = (float*)calloc(HIDDEN_NODES_3,sizeof(float));
    float* output = (float*)calloc(INPUT_NODES,sizeof(float));  // Saída tem o mesmo tamanho da entrada


    
    // Verificar se a alocação foi bem-sucedida
    if (!hidden1 || !hidden2 || !hidden3 || !output) {
        Serial.println("Erro: Não foi possível alocar memória.");
        return;
    }


    // Realizar a inferência (propagação para frente)
    forward_autoencoder(input, weights1, weights2, weights3, weights_out, output, hidden1, hidden2, hidden3);

    // Calcular o erro de reconstrução
    detectAnomaly(input, output);
    free(hidden1);
    free(hidden2);
    free(hidden3);
    free(output);    

}



void trainTask(void* parameter) {
   
    for (;;) {
        Serial.println("Iniciando treinamento da rede neural...");
        loadAndTrainFromCSV();  // Treinar a rede usando os dados gravados no CSV

        // Converter os pesos em JSON e salvar no SPIFFS após o treinamento
        String weightsJson = convertWeightsToJson();
        saveWeightsToFile(weightsJson);  // Função  que salva os pesos
        // Obter o ID do ESP32 (substitua pela forma como você determina o espID)
        String espID = "esp02";  // Exemplo de ID do ESP, ajuste conforme necessário

        // Enviar os pesos via MQTT após a atualização do arquivo
        //sendWeightsViaMQTTPartitioned(espID);  // Enviar os pesos atualizados

        
        vTaskDelay(300000/ portTICK_PERIOD_MS);  // Atraso de 10 minutos: 600000
    }
}

void readTask(void * parameter) {

       vTaskDelay(45000 / portTICK_PERIOD_MS);  // 360.000 milissegundos = 6 minutos

       int counter = 0;  // Contador de leituras
       for (;;) {
        float temperature = dht.readTemperature();
        float humidity = dht.readHumidity();
        float luminosity = readLuminosity();
        float voltage = readVoltage();

       
//        // Verificar se chegou à 20ª leitura para adicionar uma anomalia
        if (counter % 20 == 0 && counter > 0) {
            // Adicionar ruído aleatório à luminosidade ou voltagem para criar anomalia
            int anomalyType = random(1, 4);  // Escolher aleatoriamente entre 1, 2, ou 3

            if (anomalyType == 1) {
                // Adicionar ruído à luminosidade
                luminosity += random(30, 50);  // Adicionar um ruído significativo
                //Serial.println("Anomalia adicionada: Luminosidade alterada.");
            } else if (anomalyType == 2) {
                // Adicionar ruído à voltagem
                voltage += random(2, 4);  // Adicionar ruído à voltagem
                //Serial.println("Anomalia adicionada: Voltagem alterada.");
            } else {
                // Adicionar ruído em ambos, luminosidade e voltagem
                luminosity += random(30, 50);
                voltage += random(2, 4);
                //Serial.println("Anomalia adicionada: Luminosidade e voltagem alteradas.");
            }
        }     
        if (isnan(temperature) || isnan(humidity)) {
          Serial.println("Erro ao ler o DHT22!");
   
        }
        
        float anomaly = 0.0;
      
        if (temperature < 20 || temperature > 40 || humidity < 40 || humidity > 80 || luminosity < 85 || luminosity > 110 || voltage < 2.5 || voltage > 3.6){
          anomaly = 1.0;
        }
        Serial.print("Lendo dados: ");
        Serial.print("Temperatura: ");
        Serial.print(temperature);
        Serial.print(" °C, Umidade: ");
        Serial.print(humidity);
        Serial.print(" %, Luminosidade: ");
        Serial.print(luminosity);
        Serial.print(", Voltagem: ");
        Serial.print(voltage);
        Serial.print(" V");
        Serial.print(", Anomalia: ");  
        Serial.println(anomaly);
      
    
          // Verificar se é uma anomalia
        isAnomaly(temperature, humidity, luminosity, voltage);
      
        
        // Salva os dados brutos no arquivo CSV
        saveDataToCSV(temperature, humidity, luminosity, voltage, anomaly);
        
        counter++;
        
        vTaskDelay(5000 / portTICK_PERIOD_MS);  // Espera 5 segundos
       }
}
// Função para processar o recebimento das mensagens MQTT (envio e recepção de pesos)

// Variável para armazenar os dados recebidos
String jsonBuffer = "";
String receivedPayload = "";  // Armazenará a mensagem completa (todos os chunks)

// Função de callback chamada quando uma mensagem é recebida pelo MQTT
void mqttCallback(char* topic, byte* payload, unsigned int length) {
//    Serial.print("Mensagem recebida do tópico: ");
//    Serial.println(topic);

    // Verifica o início da transmissão
    if (String((char*)payload).startsWith("INICIO_TRANSMISSAO")) {
        Serial.println("Iniciando recepção dos pesos agregados...");
        receivedPayload = "";  // Limpar o buffer de recepção para iniciar a nova transmissão
        return;  // Ignora o processamento do payload de INICIO_TRANSMISSAO
    }

    // Verifica o fim da transmissão
    if (String((char*)payload).startsWith("FIM_TRANSMISSAO")) {
        Serial.println("Transmissão completa. Agora, parseando os pesos recebidos...");

//        // Exibe o payload completo para debug
//        Serial.println("Conteúdo completo do payload recebido:");
//        Serial.println(receivedPayload);  // Verifica se o JSON recebido está correto

        // Parseia o JSON recebido
        DynamicJsonDocument doc(8192);  // Tamanho ajustado para o JSON
        DeserializationError error = deserializeJson(doc, receivedPayload);

        // Verifica se houve algum erro ao parsear o JSON
        if (error) {
            Serial.print("Erro ao parsear o JSON: ");
            Serial.println(error.c_str());
            return;
        }

        // Salvar os pesos no arquivo JSON com nome específico
        String filename = "/weights_aggregated.json";
        File file = SPIFFS.open(filename, FILE_WRITE);
        if (!file) {
            Serial.println("Erro ao abrir o arquivo para escrita: " + filename);
            return;
        }

        // Escreve o JSON completo no arquivo
        serializeJson(doc, file);
        file.close();
        Serial.println("Pesos recebidos via MQTT e salvos em " + filename);

        // Carrega os pesos salvos no arquivo na rede neural
        freeWeights();
   
        loadWeightsFromFile(filename);  // Chama a função para carregar os pesos do arquivo especificado

 //       listWeights();


        delay(1000);
        return;
    }

    // Caso seja parte do payload, adiciona ao buffer de recepção
    for (unsigned int i = 0; i < length; i++) {
        receivedPayload += (char)payload[i];
    }
    //Serial.println("Chunk recebido e adicionado ao buffer.");
}



// Função para conectar ao MQTT
void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Conectando ao servidor MQTT...");
    if (client.connect("ESP32Client")) {
      Serial.println("Conectado!");
      client.subscribe(mqtt_topic);  // Subscrição ao tópico (se necessário)
      client.subscribe(mqtt_topic_agregado); // Subscrição ao tópico de pesos agregados
    } else {
      Serial.print("Falha na conexão, rc=");
      Serial.print(client.state());
      Serial.println(" Tentando novamente em 5 segundos...");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  



  
  disableCore1WDT();  // Desabilita o Watchdog Timer da Core 1
  // Inicializa o sistema de arquivos SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Erro ao inicializar SPIFFS.");
    return;
  }

   deleteCSV();
   if (!SPIFFS.exists(CSV_FILENAME)) {
      Serial.println("O arquivo foi removido com sucesso.");
    } else {
      Serial.println("O arquivo ainda existe.");
   }
   checkCSVFileSize();

   // Conecta ao Wi-Fi
   connectToWiFi();


  // Total de memória heap disponível (RAM)
  size_t totalHeap = heap_caps_get_total_size(MALLOC_CAP_8BIT);
  Serial.print("Memória Heap Total: ");
  Serial.print(totalHeap);
  Serial.println(" bytes");
  
   // Cria ou abre o arquivo CSV se não existir
   if (!SPIFFS.exists(CSV_FILENAME)) {
     File file = SPIFFS.open(CSV_FILENAME, FILE_WRITE);
     if (file) {
       //file.println("Device,DateTime,Temperature,Humidity,Luminosity,Voltage,Anomaly");
       file.close();
     }
   }
   client.setServer(mqtt_server, mqtt_port);
   //client.setCallback(mqttCallback);
   
   // Conectar ao servidor MQTT
   reconnectMQTT();
    
    freeMemory();
 
   delay(2000);
   configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
   delay(1000);
   // Alocação de pesos
   weights1 = (float*)calloc(HIDDEN_NODES_1,  INPUT_NODES * sizeof(float));
   weights2 = (float*)calloc(HIDDEN_NODES_2, HIDDEN_NODES_1 * sizeof(float));
   weights3 = (float*)calloc(HIDDEN_NODES_3, HIDDEN_NODES_2 * sizeof(float));
   weights_out = (float*)calloc(OUTPUT_NODES, HIDDEN_NODES_3 * sizeof(float));

   if (weights1 == NULL || weights2 == NULL || weights3 == NULL || weights_out == NULL) {
     Serial.println("Erro ao alocar memória para os pesos.");
     while (true);
   }

    // Carregar os pesos do arquivo (se existirem)
    String fileName = "/weights77777.json";
    loadWeightsFromFile(fileName);
    
    freeMemory();
 
 //   listWeights();
    delay(2000);

   // Inicialização de tarefas
   xTaskCreatePinnedToCore(trainTask, "Treinamento", 20000, NULL, 1, NULL, 1); // Core 1
   delay(2000);
   xTaskCreatePinnedToCore(readTask, "Leitura", 4096, NULL, 1, NULL, 0); // Core 0
   Serial.println("Sistema inicializado.");

   freeMemory();
}

void loop() {
  // Manter a conexão com o servidor MQTT
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();  
}