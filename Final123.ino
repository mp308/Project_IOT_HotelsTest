#include <PubSubClient.h>
#include <WiFi.h>
#include <DHT.h>
#include <HTTPClient.h>

const char* ssid = "FIFA_LAPTOP";
const char* password = "12345678";
const char* mqttServer = "mqtt.netpie.io";
const int mqttPort = 1883;
const char* clientID = "f2cc914d-05c3-47f8-82ae-6fb0a2de6f1d";
const char* mqttUser = "fTM1RSC48WpMQPCLgZLFx1f5hgqEHXaL";
const char* mqttPassword = "ajqiagMMqMcphQCmyr7bVoi5yDnsb5nG";

const char* topic_pub = "@msg/lab_nodered_ict_kps/weather/data";
const char* topic_sub = "@msg/lab_nodered_ict_kps/command";

// Sensor pins
const int DHTPIN = 16;
const int DHTTYPE = DHT11;
const int sensorPin = 25;
const int IFsensorPin = 4;
const int greenLEDPin = 27;
const int buzzerPin = 5;
const int fireSensorPin = 34;
const int relayPin = 13;

// DHT sensor instance
DHT dht(DHTPIN, DHTTYPE);

// State variables
bool lineBlocked = false;
unsigned long previousLockTime = 0;
const unsigned long lockInterval = 4000; // 4 seconds in milliseconds
unsigned long lastDetectionTime = 0;
const unsigned long detectionDuration = 5000; // 5 seconds

// MQTT client instance
WiFiClient espClient;
PubSubClient client(espClient);

// Message buffer
String publishMessage;

// State variable for temporarily changing status_home
bool unlockReceived = false;

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnectMQTT() {
  char mqttinfo[80];
  snprintf(mqttinfo, 75, "Attempting MQTT connection at %s:%d (%s/%s)...", mqttServer, mqttPort, mqttUser, mqttPassword);
  while (!client.connected()) {
    Serial.println(mqttinfo);
    String clientId = clientID;
    if (client.connect(clientId.c_str(), mqttUser, mqttPassword)) {
      Serial.println("...Connected");
      client.subscribe(topic_sub);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void messageReceivedCallback(char* topic, byte* payload, unsigned int length) {
  char payloadMsg[80];

  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
  Serial.print("Message:");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    payloadMsg[i] = (char)payload[i];
  }
  payloadMsg[length] = '\0';
  Serial.println();
  Serial.println("-----------------------");

  // Process the payload as needed or remove the following line if not needed
  processMessage(payloadMsg);
}

void processMessage(String recvCommand) {
  // Add your processing logic here based on the received command
  // Example:
  if (recvCommand == "UNLOCK") {
    unlockReceived = true; // Set the flag to indicate unlock received
    sendLineNotification("Door Unlocked");
  } else if (recvCommand == "LOCK") {
    unlockReceived = false; // Reset the flag and status_home to 0 when locked
    sendLineNotification("Door Locked");
  }
}

void setup() {
  Serial.begin(115200); // Initialize serial communication
  setup_wifi();
  client.setServer(mqttServer, mqttPort);
  client.setCallback(messageReceivedCallback);
  // Additional setup tasks can be added here
  pinMode(sensorPin, INPUT);       // Set the sensor pin as input
  pinMode(IFsensorPin, INPUT);     // Set the IF sensor pin as input
  pinMode(greenLEDPin, OUTPUT);    // Set the green LED pin as output
  pinMode(buzzerPin, OUTPUT);      // Set the buzzer pin as output
  pinMode(fireSensorPin, INPUT);   // Set the fire sensor pin as input
  pinMode(relayPin, OUTPUT);       // Set the relay pin as output
  dht.begin();
}

unsigned long lastMQTTSendTime = 0;
const unsigned long MQTTInterval = 1000; // 1 second in milliseconds

void loop() {
  // Read sensor state
  int sensorState;
  if (unlockReceived) {
    sensorState = 1; // Set status_home to 1 if UNLOCK command received
  } else {
    sensorState = digitalRead(sensorPin); // Read the sensor state
  }

  // Read IF sensor state
  int IFsensorState = digitalRead(IFsensorPin);
  bool IFsensorTriggered = false;  // Set the default value to false

  if (sensorState == 0 && IFsensorState == LOW) { // Check if status_home is 0 and IF sensor is LOW
    Serial.println("Detected!");
    // Turn on the buzzer when something is detected and the door is unlocked
    tone(buzzerPin, 1000, 500);  // Adjusted tone duration
    lastDetectionTime = millis(); // Update the time of the last detection
    IFsensorTriggered = true;
    sendLineNotification("Intruder Detected");
  } else {
    // Check if it's been more than detectionDuration milliseconds since the last detection
    if (millis() - lastDetectionTime > detectionDuration) {
      IFsensorTriggered = false; // Reset IFsensorTriggered to false when no detection occurs
      digitalWrite(greenLEDPin, LOW);  // Turn off the green LED
    }
  }

  // Read fire sensor state
  int fireSensorState = digitalRead(fireSensorPin);
  bool fireDetected = false;
  if (fireSensorState == LOW) {
    Serial.println("Fire Detected!");
    // Activate relay to trigger fire alarm or take necessary action
    digitalWrite(relayPin, LOW);
    fireDetected = true;
    sendLineNotification("Fire Detected!"); // Sending Line Notification
  } else {
    // Deactivate relay
    digitalWrite(relayPin, HIGH);
  }

  // Turn on green LED if status_home is 1
  if (sensorState == 1) {
    digitalWrite(greenLEDPin, HIGH); // Turn on the green LED
  } else {
    digitalWrite(greenLEDPin, LOW);  // Turn off the green LED
  }

  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();

  dht.read();
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  // Check if it's time to send MQTT data
  if (millis() - lastMQTTSendTime >= MQTTInterval) {
    // Update the publishMessage line to include fireSensorState
    publishMessage = "{\"data\": {\"temperature\": " + String(t) + ", \"humidity\": " + String(h) + ", \"status_home\": " + String(sensorState) + ", \"status_detected\": " + String(IFsensorTriggered) + ", \"status_fire\": " + String(fireDetected) + "}}";

    Serial.println(publishMessage);
    client.publish(topic_pub, publishMessage.c_str());

    lastMQTTSendTime = millis(); // Update the time of the last MQTT send
  }
}


void sendLineNotification(String message) {
  HTTPClient http;

  String url = "https://notify-api.line.me/api/notify";
  String token = "ogGwWPHErQxxB8xnG06VAh0mVAve9LNEymq64QpsD2V"; // Replace with your Line Notify token

  http.begin(url);
  http.addHeader("Authorization", "Bearer " + token);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  int httpCode = http.POST("message=" + message);

  if (httpCode > 0) {
    String payload = http.getString();
    Serial.println(httpCode);
    Serial.println(payload);
  } else {
    Serial.println("Error sending Line notification");
  }

  http.end();
}
