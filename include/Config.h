#ifndef CONFIG_H_
#define CONFIG_H_

// Network / Broker
#define WIFI_SSID "PedroRapha"
#define WIFI_PASSWORD "456123789a"
#define MQTT_BROKER "192.168.15.3"

// Paths
#define DATASET_BINARY 1
// #define DATASET_ORIGINAL 1
#define MODEL_PATH "/model.nn"
#define NEW_MODEL_PATH "/new_model.nn"
#define TEMPORARY_NEW_MODEL_PATH "/new_model_temp.nn"
#define CONFIGURATION_PATH "/config.json"
#define DEVICE_DEFINITION_PATH "/device.json"
#define GATHERED_DATA_PATH "/data.db"
#ifdef DATASET_ORIGINAL
#define X_TRAIN_PATH "/x_train.csv"
#define Y_TRAIN_PATH "/y_train.csv"
#define X_TEST_PATH "/x_test.csv"
#define Y_TEST_PATH "/y_test.csv"
#endif
#ifdef DATASET_BINARY
#define XY_TRAIN_PATH "/xy_train.bin"
#define METADATA_JSON_PATH "/metadata.json"
#endif

// MQTT
#define MQTT_PUBLISH_TOPIC "esp32/fl/model/push"
#define MQTT_RAW_PUBLISH_TOPIC "esp32/fl/model/rawpush"
#define MQTT_RECEIVE_TOPIC "esp32/fl/model/pull"
#define MQTT_RAW_RECEIVE_TOPIC "esp32/fl/model/rawpull"
#define MQTT_RESUME_TOPIC "esp32/fl/model/resume"
#define MQTT_RAW_RESUME_TOPIC "esp32/fl/model/rawresume"
#define MQTT_RECEIVE_COMMANDS_TOPIC "esp32/fl/commands/pull"
#define MQTT_SEND_COMMANDS_TOPIC "esp32/fl/commands/push"

// Other
#define CONNECTION_TIMEOUT 30000 // in milliseconds

#endif /* CONFIG_H_ */
