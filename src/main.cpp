#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <DHTesp.h>

const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
WebServer webServer(80);

DHTesp dht;
int interval = 2000;
unsigned long lastDHTReadMillis = 0;
float humidity = 0;
float temperature = 0;

// ConfigPortal에서 설정할 변수들
String ssid = "SK_0A48_2.4G";
String password = "AAX04@3427";
String influxAddress = "192.168.45.172";
String token = "3ZXJ4jjxJkcukYJmuGzVT7TMre01RyuBhdDpn-PZ5PUAW9O6ZYge4S90S9wxCLWF6kaRwKKb7BXxqa6N19CbDQ==";
String bucket = "bucket01";
int reportInterval = 5000;

// DHT22 데이터 읽기
void readDHT22() {
    unsigned long currentMillis = millis();
    if(currentMillis - lastDHTReadMillis >= interval) {
        lastDHTReadMillis = currentMillis;
        humidity = dht.getHumidity();
        temperature = dht.getTemperature();
    }
}

// ConfigPortal 페이지 생성 및 정보 저장
void handleRoot() {
    String page = "<!DOCTYPE html><html><head><title>Device Setup Page</title></head><body><h2>Device Setup Page</h2>"
                  "<form action=\"/save\" method=\"POST\">"
                  "SSID: <input type=\"text\" name=\"ssid\" value=\"" + ssid + "\"><br>"
                  "Password: <input type=\"text\" name=\"password\" value=\"" + password + "\"><br>"
                  "InfluxDB Address: <input type=\"text\" name=\"influxAddress\" value=\"" + influxAddress + "\"><br>"
                  "InfluxDB Token: <input type=\"text\" name=\"token\" value=\"" + token + "\"><br>"
                  "InfluxDB Bucket: <input type=\"text\" name=\"bucket\" value=\"" + bucket + "\"><br>"
                  "Report Interval (ms): <input type=\"number\" name=\"interval\" value=\"" + String(reportInterval) + "\"><br><br>"
                  "<input type=\"submit\" value=\"Save\">"
                  "</form></body></html>";
    webServer.send(200, "text/html", page);
}

// 입력된 정보를 저장하고 재부팅 메시지 출력
void handleSave() {
    ssid = webServer.arg("ssid");
    password = webServer.arg("password");
    influxAddress = webServer.arg("influxAddress");
    token = webServer.arg("token");
    bucket = webServer.arg("bucket");
    reportInterval = webServer.arg("interval").toInt();

    String response = "Settings Saved! Please reboot the device.<br><br>"
                      "SSID: " + ssid + "<br>"
                      "Password: " + password + "<br>"
                      "InfluxDB Address: " + influxAddress + "<br>"
                      "InfluxDB Token: " + token + "<br>"
                      "InfluxDB Bucket: " + bucket + "<br>"
                      "Report Interval: " + String(reportInterval) + " ms<br>";

    webServer.send(200, "text/html", response);
}

void setup() {
    Serial.begin(115200);
    dht.setup(15, DHTesp::DHT22);

    // Captive Portal 설정
    WiFi.softAP("jihoonweb");
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    dnsServer.start(DNS_PORT, "*", apIP);

    webServer.on("/", handleRoot);
    webServer.on("/save", HTTP_POST, handleSave);
    webServer.onNotFound([]() {
        webServer.send(404, "text/html", "<h1>Page not found</h1>");
    });

    webServer.begin();
    Serial.println("Captive Portal started");
}

WiFiClient client;

void loop() {
    dnsServer.processNextRequest();
    webServer.handleClient();

    // Wi-Fi에 연결된 후 InfluxDB로 데이터 전송
    if (WiFi.status() != WL_CONNECTED && !ssid.isEmpty() && !password.isEmpty()) {
        Serial.printf("Connecting to WiFi: %s\n", ssid.c_str());
        WiFi.begin(ssid.c_str(), password.c_str());
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
        }
        Serial.println("\nConnected to WiFi!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
    }

    if (WiFi.status() == WL_CONNECTED && !influxAddress.isEmpty()) {
        HTTPClient http;
        char urlbuf[200];
        char data[200];

        readDHT22();
        sprintf(urlbuf, "http://%s:8086/write?db=%s", influxAddress.c_str(), bucket.c_str());
        sprintf(data, "ambient,location=room02 temperature=%.1f,humidity=%.1f", temperature, humidity);

        if (http.begin(client, urlbuf)) {
            http.addHeader("Authorization", "Token " + token);
            int httpCode = http.POST(String(data));

            if (httpCode > 0) {
                Serial.printf("[HTTP] POST... code: %d\n", httpCode);
            } else {
                Serial.printf("[HTTP] POST... failed, HTTP Code: %d, error: %s\n", httpCode, http.errorToString(httpCode).c_str());
            }
            http.end();
        } else {
            Serial.printf("[HTTP] Unable to connect\n");
        }

        delay(reportInterval);
    }
}
