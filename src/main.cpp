#include <SPI.h>
#include <LoRa.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <LittleFS.h> // Switch from SPIFFS to LittleFS for file handling

#define SS_PIN 15  // Slave Select pin
#define RST_PIN -1 // Reset pin
#define DIO0_PIN 5 // DIO0 pin
#define LORA_FREQUENCY 868.1E6
#define AUDIO_BUFFER_SIZE 320 // Buffer for 4 seconds of audio

// Packet Types
#define PACKET_TYPE_GPS 0x01
#define PACKET_TYPE_AUDIO 0x02

// WiFi Configuration
const char *ssid = "Mr.Uzumaki";
const char *password = "aaronKay";
const char *apiEndpoint = "http://192.168.43.48:8000/detect"; // Your API endpoint

byte audioBuffer[AUDIO_BUFFER_SIZE];
int bufferIndex = 0;

// Declare global variables for GPS data
String lastLatitude = "";
String lastLongitude = "";
String lastTimestamp = "";
String gpsData = "";

// Function declarations
bool setupWiFi();
bool setupLoRa();
bool setupFileSystem();
void handleGPSData();
void handleAudioData();
void saveWavFile();
void sendAudioToAPI();

void setup()
{
  Serial.begin(115200);

  // Setup WiFi, LoRa, and FileSystem
  if (!setupWiFi() || !setupLoRa() || !setupFileSystem())
  {
    Serial.println("Failed to initialize one or more modules. Retrying...");
    delay(5000);   // Retry initialization every 5 seconds instead of stopping
    ESP.restart(); // Restart to reattempt setup
  }
  Serial.println("ESP8266 Receiver ready");
}

void loop()
{
  int packetSize = LoRa.parsePacket();

  if (packetSize)
  {
    uint8_t packetType = LoRa.read(); // Read the packet type
    Serial.print("Packet Type: ");
    Serial.println(packetType);

    if (packetType == PACKET_TYPE_GPS)
    {
      handleGPSData(); // Handle GPS data packet
    }
    else if (packetType == PACKET_TYPE_AUDIO)
    {
      handleAudioData(); // Handle Audio data packet
    }

    // Print RSSI of the received packet
    Serial.print("Received packet RSSI: ");
    Serial.println(LoRa.packetRssi());
  }

  // Check if audio buffer is full
  if (bufferIndex >= AUDIO_BUFFER_SIZE)
  {
    Serial.println("Audio buffer is full. Saving file and sending to API...");
    saveWavFile();    // Save the audio buffer as a .wav file
    sendAudioToAPI(); // Send the .wav file and GPS data to the API endpoint
    bufferIndex = 0;  // Reset the buffer index after sending
  }

  // Optional delay to avoid busy-waiting
  delay(100);
}

bool setupWiFi()
{
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  int attempts = 0;

  while (WiFi.status() != WL_CONNECTED && attempts < 20)
  {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nWiFi connected!");
    return true;
  }
  else
  {
    Serial.println("Failed to connect to WiFi.");
    return false;
  }
}

bool setupLoRa()
{
  Serial.println("Initializing LoRa...");

  LoRa.setPins(SS_PIN, RST_PIN, DIO0_PIN);

  if (!LoRa.begin(LORA_FREQUENCY))
  {
    Serial.println("LoRa initialization failed!");
    return false;
  }

  LoRa.setSpreadingFactor(12);
  LoRa.setSyncWord(0xF3);
  Serial.println("LoRa initialized successfully!");
  return true;
}

bool setupFileSystem()
{
  Serial.println("Mounting LittleFS...");
  if (!LittleFS.begin())
  {
    Serial.println("Failed to mount file system.");
    return false;
  }
  Serial.println("LittleFS mounted successfully!");
  return true;
}

// Handle GPS Data
void handleGPSData()
{
  gpsData = ""; // Reset gpsData for new reading
  while (LoRa.available())
  {
    gpsData += (char)LoRa.read(); // Store the entire string directly
  }

  // Example: Assuming GPS data format is "lat,lon,timestamp"
  int commaIndex1 = gpsData.indexOf(',');
  int commaIndex2 = gpsData.indexOf(',', commaIndex1 + 1);

  if (commaIndex1 != -1 && commaIndex2 != -1)
  {
    lastLatitude = gpsData.substring(0, commaIndex1);
    lastLongitude = gpsData.substring(commaIndex1 + 1, commaIndex2);
    lastTimestamp = gpsData.substring(commaIndex2 + 1);

    Serial.print("Latitude: ");
    Serial.print(lastLatitude);
    Serial.print(", Longitude: ");
    Serial.print(lastLongitude);
    Serial.print(", Timestamp: ");
    Serial.println(lastTimestamp);
  }
  else
  {
    Serial.println("Invalid GPS data received");
  }
}

// Handle Audio Data
void handleAudioData()
{
  Serial.println("Receiving audio data...");
  while (LoRa.available() && bufferIndex < AUDIO_BUFFER_SIZE)
  {
    audioBuffer[bufferIndex++] = LoRa.read(); // Store received audio byte in buffer
  }
}

void saveWavFile()
{
  File wavFile = LittleFS.open("/audio.wav", "w");

  if (!wavFile)
  {
    Serial.println("Failed to create WAV file");
    return;
  }

  // Prepare the WAV header
  uint32_t chunkSize = 36 + AUDIO_BUFFER_SIZE;
  uint32_t subChunk2Size = AUDIO_BUFFER_SIZE;
  uint16_t audioFormat = 1;
  uint16_t numChannels = 1;
  uint32_t sampleRate = 8000; // 8kHz sample rate
  uint16_t bitsPerSample = 16;
  uint16_t blockAlign = numChannels * (bitsPerSample / 8);
  uint32_t byteRate = sampleRate * numChannels * (bitsPerSample / 8);

  // Write WAV header
  wavFile.write("RIFF", 4);
  wavFile.write((const byte *)&chunkSize, 4);
  wavFile.write("WAVE", 4);
  wavFile.write("fmt ", 4);

  uint32_t subChunk1Size = 16; // PCM
  wavFile.write((const byte *)&subChunk1Size, 4);
  wavFile.write((const byte *)&audioFormat, 2);
  wavFile.write((const byte *)&numChannels, 2);
  wavFile.write((const byte *)&sampleRate, 4);
  wavFile.write((const byte *)&byteRate, 4);
  wavFile.write((const byte *)&blockAlign, 2);
  wavFile.write((const byte *)&bitsPerSample, 2);

  wavFile.write("data", 4);
  wavFile.write((const byte *)&subChunk2Size, 4);

  // Write audio data
  wavFile.write(audioBuffer, AUDIO_BUFFER_SIZE);
  wavFile.close();

  Serial.println("WAV file saved successfully");
}

void sendAudioToAPI()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    WiFiClient client;

    // Define the server and endpoint
    String post_host = "192.168.43.48"; // Replace with your server
    const int post_port = 8000;         // Replace with your server port
    String url = "/detect";             // Replace with your API endpoint

    // Open the WAV file to be sent
    File wavFile = LittleFS.open("/audio.wav", "r");
    if (!wavFile)
    {
      Serial.println("Failed to open WAV file for sending.");
      return;
    }

    // Calculate file size
    String fileSize = String(wavFile.size());
    Serial.println("File size: " + fileSize + " bytes");

    // Boundary for multipart data
    String boundary = "--SaJeBoundary";
    String contentType = "audio/x-wav";

    // Prepare the headers and request body start
    String postHeader = "POST " + url + " HTTP/1.1\r\n";
    postHeader += "Host: " + post_host + ":" + String(post_port) + "\r\n";
    postHeader += "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n";
    postHeader += "Accept-Charset: utf-8\r\n";
    postHeader += "gps-data: " + gpsData + "\r\n"; // GPS data added to the headers

    // Prepare the form data
    String bodyStart = "--" + boundary + "\r\n";
    bodyStart += "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n";
    bodyStart += "Content-Type: " + contentType + "\r\n\r\n";

    String bodyEnd = "\r\n--" + boundary + "--\r\n";

    // Calculate content length
    int contentLength = bodyStart.length() + wavFile.size() + bodyEnd.length();
    postHeader += "Content-Length: " + String(contentLength) + "\r\n\r\n";

    // Send HTTP request
    Serial.println("Sending data to API...");

    if (client.connect(post_host.c_str(), post_port))
    {
      client.print(postHeader); // Send header
      client.print(bodyStart);  // Send start of body

      // Send WAV file data
      while (wavFile.available())
      {
        client.write(wavFile.read());
      }

      // Send end of body
      client.print(bodyEnd);

      // Wait for server response
      while (client.connected())
      {
        String line = client.readStringUntil('\n');
        if (line == "\r")
        {
          break;
        }
      }

      Serial.println("Response received:");
      while (client.available())
      {
        String response = client.readStringUntil('\n');
        Serial.println(response);
      }
    }
    else
    {
      Serial.println("Failed to connect to server.");
    }

    wavFile.close();
  }
  else
  {
    Serial.println("WiFi not connected.");
  }
}