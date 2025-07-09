#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>

#define PART_BOUNDARY "123456789000000000000987654321"

static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: %s\r\nContent-Length: %u\r\n\r\n";


// ====== WiFi Credentials ======
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

// ====== Motor Pins ======
#define MOTOR_A1 12
#define MOTOR_A2 13
#define MOTOR_B1 14
#define MOTOR_B2 15

// ====== Global Flags ======
bool drivingEnabled = false;

// ====== Camera Pins (AI Thinker) ======
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM     0
#define SIOD_GPIO_NUM    26
#define SIOC_GPIO_NUM    27
#define Y9_GPIO_NUM      35
#define Y8_GPIO_NUM      34
#define Y7_GPIO_NUM      39
#define Y6_GPIO_NUM      36
#define Y5_GPIO_NUM      21
#define Y4_GPIO_NUM      19
#define Y3_GPIO_NUM      18
#define Y2_GPIO_NUM       5
#define VSYNC_GPIO_NUM   25
#define HREF_GPIO_NUM    23
#define PCLK_GPIO_NUM    22

WebServer server(80);

void setupMotors() {
  pinMode(MOTOR_A1, OUTPUT);
  pinMode(MOTOR_A2, OUTPUT);
  pinMode(MOTOR_B1, OUTPUT);
  pinMode(MOTOR_B2, OUTPUT);
}

void moveForward() {
  digitalWrite(MOTOR_A1, HIGH);
  digitalWrite(MOTOR_A2, LOW);
  digitalWrite(MOTOR_B1, HIGH);
  digitalWrite(MOTOR_B2, LOW);
}

void turnLeft() {
  digitalWrite(MOTOR_A1, LOW);
  digitalWrite(MOTOR_A2, HIGH);
  digitalWrite(MOTOR_B1, HIGH);
  digitalWrite(MOTOR_B2, LOW);
}

void turnRight() {
  digitalWrite(MOTOR_A1, HIGH);
  digitalWrite(MOTOR_A2, LOW);
  digitalWrite(MOTOR_B1, LOW);
  digitalWrite(MOTOR_B2, HIGH);
}

void stopMotors() {
  digitalWrite(MOTOR_A1, LOW);
  digitalWrite(MOTOR_A2, LOW);
  digitalWrite(MOTOR_B1, LOW);
  digitalWrite(MOTOR_B2, LOW);
}

void handleRoot() {
  server.send(200, "text/html", R"rawliteral(
    <html>
    <head><title>ESP32-CAM Car</title></head>
    <body>
    <h2>ESP32-CAM Obstacle Avoidance</h2>
    <p><a href="/toggle"><button>Toggle Auto-Drive</button></a></p>
    <p>Status: <b>)rawliteral" + String(drivingEnabled ? "DRIVING" : "STOPPED") + R"rawliteral(</b></p>
    <img src="http://" + String(WiFi.localIP().toString()) + ":81/stream" width="320">
    </body></html>
  )rawliteral");
}

void handleToggle() {
  drivingEnabled = !drivingEnabled;
  stopMotors();
  handleRoot();
}

#include "esp_http_server.h"

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      continue;
    }

    res = httpd_resp_send_chunk(req, (const char *)_STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    if (res == ESP_OK) {
      char header[128];
      size_t hlen = snprintf(header, sizeof(header), _STREAM_PART, "image/jpeg", fb->len);
      res = httpd_resp_send_chunk(req, header, hlen);
    }

    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    }

    esp_camera_fb_return(fb);
    if (res != ESP_OK) break;
    delay(30);
  }

  return res;
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  httpd_start(&camera_httpd, &config);
  httpd_uri_t uri_stream = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };
  httpd_register_uri_handler(camera_httpd, &uri_stream);
}

void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_GRAYSCALE;
  config.frame_size = FRAMESIZE_96X96;
  config.fb_count = 1;

  esp_camera_init(&config);
}

void setup() {
  Serial.begin(115200);
  setupMotors();
  setupCamera();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  server.on("/", handleRoot);
  server.on("/toggle", handleToggle);
  server.begin();
  startCameraServer();
}

void loop() {
  server.handleClient();

  if (!drivingEnabled) {
    stopMotors();
    delay(200);
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return;

  int darkLeft = 0, darkRight = 0;
  uint8_t *gray = fb->buf;
  int width = fb->width, height = fb->height;

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      uint8_t pixel = gray[y * width + x];
      if (pixel < 60) {
        if (x < width / 2) darkLeft++;
        else darkRight++;
      }
    }
  }
  esp_camera_fb_return(fb);

  if (darkLeft > 300 && darkRight > 300) {
    stopMotors();
    delay(300);
    turnRight();
    delay(400);
  } else if (darkLeft > darkRight + 100) {
    turnRight();
  } else if (darkRight > darkLeft + 100) {
    turnLeft();
  } else {
    moveForward();
  }

  delay(100);
}
