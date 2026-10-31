#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_http_server.h>

static const char *TAG = "WEBSERVER";
static httpd_handle_t server = NULL;

// صفحة الويب المدمجة (HTML)
const char *html_page = 
"<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
"<title>ESP32 Pentest Tool</title>"
"<style>body{font-family:sans-serif;background:#222;color:#fff;text-align:center;padding:20px;}"
"h1{color:#00ffcc;} .btn{background:#ff4444;color:white;padding:15px 32px;text-align:center;"
"text-decoration:none;display:inline-block;font-size:16px;margin:4px 2px;cursor:pointer;border-radius:8px;}"
".btn:hover{background:#cc0000;} input{padding:10px;border-radius:5px;border:none;margin:10px;}"
".status{color:#00ff00; margin-top:20px;}</style></head>"
"<body><h1>ESP32 Evil Twin</h1>"
"<p>اختر شبكة الهدف وابدأ الهجوم:</p>"
"<form action='/start' method='get'>"
"<input type='text' name='ssid' placeholder='اسم الشبكة' required>"
"<input type='submit' class='btn' value='بدء الهجوم (Start Attack)'>"
"</form>"
"<div class='status'>النظام جاهز...</div>"
"</body></html>";

// دالة عرض الصفحة الرئيسية
static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_page, strlen(html_page));
    return ESP_OK;
}

// دالة بدء الهجوم
static esp_err_t start_handler(httpd_req_t *req) {
    char ssid[32];
    if (httpd_req_get_url_query_str(req, ssid, sizeof(ssid)) == ESP_OK) {
        ESP_LOGI(TAG, "Starting attack on: %s", ssid);
        // هنا نضع كود بدء الهجوم (Evil Twin)
        httpd_resp_send(req, "Attack Started!", HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_send(req, "No SSID provided", HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}

void http_server_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL
    };
    httpd_uri_t start_uri = {
        .uri = "/start",
        .method = HTTP_GET,
        .handler = start_handler,
        .user_ctx = NULL
    };

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &start_uri);
        ESP_LOGI(TAG, "Web server started on port 80");
    } else {
        ESP_LOGE(TAG, "Failed to start web server");
    }
}

void http_server_stop(void) {
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
}
