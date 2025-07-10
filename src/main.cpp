#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>

// ====== WiFi Credentials ======
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

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

// ====== Motor Control Pins ======
#define MOTOR_A1 12
#define MOTOR_A2 13
#define MOTOR_B1 14
#define MOTOR_B2 15

// ====== HTTP Stream Constants ======
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: %s\r\nContent-Length: %u\r\n\r\n";

// ====== Server and Mode ======
WebServer server(80);
bool autoMode = false;

void setupMotors() {
  pinMode(MOTOR_A1, OUTPUT);
  pinMode(MOTOR_A2, OUTPUT);
  pinMode(MOTOR_B1, OUTPUT);
  pinMode(MOTOR_B2, OUTPUT);
  stopMotors();
}

void moveForward() {
  digitalWrite(MOTOR_A1, HIGH);
  digitalWrite(MOTOR_A2, LOW);
  digitalWrite(MOTOR_B1, HIGH);
  digitalWrite(MOTOR_B2, LOW);
}
void moveBackward() {
  digitalWrite(MOTOR_A1, LOW);
  digitalWrite(MOTOR_A2, HIGH);
  digitalWrite(MOTOR_B1, LOW);
  digitalWrite(MOTOR_B2, HIGH);
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

// ====== Web Interface ======
void handleRoot() {
  server.send(200, "text/html", R"rawliteral(
    <html>
    <head>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32-CAM Joystick</title>
    <style>
      body { font-family: sans-serif; text-align: center; }
      button { font-size: 18px; margin: 10px; padding: 10px 20px; }
      canvas { touch-action: none; border: 1px solid #888; }
    </style>
    </head>
    <body>
    <h2>ESP32-CAM Robot Control</h2>
    <p><img src="/stream" width="320"></p>
    <p><button onclick="toggleMode()">Toggle Mode (Now: <span id='mode'>MANUAL</span>)</button></p>
    <canvas id="joystick" width="200" height="200"></canvas>
    <script>
    let autoMode = false;
    const modeLabel = document.getElementById("mode");
    const joy = document.getElementById("joystick");
    const ctx = joy.getContext("2d");

    function toggleMode() {
      autoMode = !autoMode;
      fetch("/mode?value=" + (autoMode ? "auto" : "manual"));
      modeLabel.textContent = autoMode ? "AUTO" : "MANUAL";
    }

    function send(dir) {
      if (!autoMode) fetch("/control?dir=" + dir);
    }

    function drawJoystick(x, y) {
      ctx.clearRect(0, 0, 200, 200);
      ctx.beginPath();
      ctx.arc(100, 100, 80, 0, 2 * Math.PI);
      ctx.stroke();
      ctx.beginPath();
      ctx.arc(x, y, 15, 0, 2 * Math.PI);
      ctx.fill();
    }

    joy.addEventListener("touchmove", function(e) {
      const touch = e.touches[0];
      const rect = joy.getBoundingClientRect();
      const x = touch.clientX - rect.left;
      const y = touch.clientY - rect.top;
      drawJoystick(x, y);
      let dx = x - 100;
      let dy = y - 100;

      if (Math.abs(dy) > Math.abs(dx)) {
        if (dy < -30) send("forward");
        else if (dy > 30) send("backward");
        else send("stop");
      } else {
        if (dx > 30) send("right");
        else if (dx < -30) send("left");
        else send("stop");
      }
    }, false);
    joy.addEventListener("touchend", () => { drawJoystick(100,100); send("stop"); });

    drawJoystick(100, 100);
    </script>
    </body></html>
  )rawliteral");
}

void handleControl() {
  if (!server.hasArg("dir")) return server.send(400, "text/plain", "Missing dir");
  String dir = server.arg("dir");
  if (autoMode) return;

  if (dir == "forward") moveForward();
  else if (dir == "backward") moveBackward();
  else if (dir == "left") turnLeft();
  else if (dir == "right") turnRight();
  else stopMotors();

  server.send(200, "text/plain", "OK");
}

void handleMode() {
  if (!server.hasArg("value")) return server.send(400, "text/plain", "Missing value");
  autoMode = server.arg("value") == "auto";
  stopMotors();
  server.send(200, "text/plain", "Mode set");
}

// ====== MJPEG Stream ======
#include "esp_http_server.h"
httpd_handle_t stream_httpd = NULL;

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

    res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    if (res == ESP_OK) {
      char header[128];
      int len = snprintf(header, 128, _STREAM_PART, "image/jpeg", fb->len);
      res = httpd_resp_send_chunk(req, header, len);
    }

    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    }

    esp_camera_fb_return(fb);
    if (res != ESP_OK) break;
    delay(50);
  }

  return res;
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  httpd_start(&stream_httpd, &config);
  httpd_uri_t stream_uri = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };
  httpd_register_uri_handler(stream_httpd, &stream_uri);
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
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;
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

  server.on("/", handleRoot);
  server.on("/control", handleControl);
  server.on("/mode", handleMode);
  server.begin();
  startCameraServer();

  Serial.println("\nReady. IP address: " + WiFi.localIP().toString());
}

void loop() {
  server.handleClient();

  if (autoMode) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return;

    int left = 0, right = 0;
    for (int i = 0; i < fb->len; i += 2) {
      if (fb->buf[i] < 60) {
        if (i % fb->width < fb->width / 2) left++;
        else right++;
      }
    }
    esp_camera_fb_return(fb);

    if (left > 300 && right > 300) {
      stopMotors(); delay(200);
      turnRight(); delay(300);
    } else if (left > right + 100) turnRight();
    else if (right > left + 100) turnLeft();
    else moveForward();

    delay(100);
  }
}
