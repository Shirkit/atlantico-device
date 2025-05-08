# Atlantico Device - Federated Learning Device Module

Este repositório contém o código e os dados para execução de aprendizado federado (Federated Learning) em dispositivos embarcados. É projetado para funcionar em conjunto com o servidor do projeto Atlantico, realizando tarefas locais de inferência, treinamento e envio de pesos.

## Estrutura do Projeto

```
atlantico-device-main/
├── platformio.ini              # Configuração para o PlatformIO (ESP32, etc.)
├── data/                       # Dados para treino/teste
├── data_ready/                 # Dados particionados por dispositivo
├── datasets/                   # Conjuntos de dados adicionais
├── include/
│   └── ModelUtil               # Utilitários para manipulação de modelos neurais
├── src/
│   └── main.cpp                # Ponto de entrada principal da aplicação
├── .vscode/                    # Configurações do VSCode
└── single_app_partition.csv    # Configuração de partição para o ESP32
```

## 🚀 Funcionalidades

- Execução local de inferência e treinamento de modelo.
- Manipulação de conjuntos de dados locais (`train/test`).
- Comunicação com servidor FL para envio e recebimento de pesos.
- Compatível com microcontroladores via PlatformIO (ex. ESP32).


## 🛠️ Requisitos

- [PlatformIO](https://platformio.org/) instalado (recomendado com VSCode)
- Ambiente de desenvolvimento (ex: VSCode ou algum fork)
- Placa de desenvolvimento compatível (ex: ESP32)
- Bibliotecas (instaladas automaticamente pelo PlatformIO):
  - NeuralNetworks (3.1.7)
  - ArduinoJson (v7.3.0+)
  - PicoMQTT (v1.3.0+)

## Como Configurar

1. Instale o [PlatformIO](https://platformio.org/install) e abra o projeto no VSCode.
2. Conecte sua placa (ex: ESP32).
3. Compile e carregue o firmware:
   - Configure suas credenciais Wi-Fi e servidor MQTT:
   - Abra o arquivo ModelUtil.h
   - Localize e modifique as seguintes constantes
   ```
   #define WIFI_SSID "sua-rede-wifi"
   #define WIFI_PASSWORD "sua-senha-wifi"
   #define MQTT_BROKER "endereco-do-broker"
   #define CLIENT_NAME "esp32-seu-dispositivo"
   ```
4. Configure a partição do dispositivo:
   - O arquivo single_app_partition.csv já está configurado para utilizar 4MB de flash
   - Ajuste conforme necessário para seu modelo específico de ESP32

## 📥 Carregando Dados
1. Prepare seus arquivos CSV para treinamento:
   - Formato dos arquivos: CSV
   - `x_train_0.csv`: dados de entrada para treinamento
   - `y_train_0.csv`: rótulos (saídas desejadas) para treinamento
   - `x_test_0.csv`: dados de entrada para teste/validação (planejado)
   - `y_test_0.csv`: rótulos para teste/validação (planejado)
2. Carregue os arquivos para o ESP32:
   1. Escolha a opção `Build Filesystem Image` para construir a imagem do bootloader com as partições especificadas
   2. Rode o comando `Upload Filesystem Image` para aplicar as modificações e carregar os arquivos dentro da pasta `data`.

## ⚙️ Compilando e Executando
1. Compile o projeto escolhendo `Build` no PlatformIO ou rode:

```pio run```

2. Carregue a aplicação rodando `Upload` ou rode:

```pio run --target upload```

3. (opcional) Monitore a saída do terminal rodando `Upload and Monitor` (pule o passo 2) ou execute apenas `Monitor` ou então rode:

```pio device monitor```

## 📂 Diretórios Importantes

- `data/`: Arquivos CSV com dados de treino e teste.
- `data_ready/`: Dados divididos por dispositivo (ex: device 0, device 1...).
- `metrics/`: Diretório opcional para salvar métricas de avaliação.
- `tasks/`: Representações visuais das tarefas do dispositivo.