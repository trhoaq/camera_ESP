#include <stdio.h>
#include "esp_http_server.h"
#include "esp_camera.h"
#include "esp_log.h"

static const char *TAG = "camera_httpd";

// HTML page for streaming
static const char* INDEX_HTML = 
"<html>"
"<head>"
"<title>ESP32-S3 Camera Stream</title>"
"<style>"
"body { font-family: Arial; text-align: center; margin: 20px; }"
".container { max-width: 800px; margin: 0 auto; }"
"img { max-width: 100%; height: auto; border: 2px solid #ddd; border-radius: 4px; }"
"button { padding: 10px 20px; margin: 10px; font-size: 16px; }"
"</style>"
"</head>"
"<body>"
"<div class='container'>"
"<h1>ESP32-S3 OV2640 Camera Stream</h1>"
"<img src='/stream' alt='Live Stream'>"
"<br>"
"<button onclick='window.location.reload()'>Refresh</button>"
"<button onclick='window.open(\"/jpg\")'>Capture Still Image</button>"
"</div>"
"</body>"
"</html>";

// Handler for main page
static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
}

// Handler for JPEG image
static esp_err_t jpg_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    
    fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    
    return res;
}

// Handler for MJPEG stream
static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    
    res = httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
    if (res != ESP_OK) {
        return res;
    }
    
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "60");
    
    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
            break;
        }
        
        if (fb->format != PIXFORMAT_JPEG) {
            bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
            if (!jpeg_converted) {
                ESP_LOGE(TAG, "JPEG compression failed");
                esp_camera_fb_return(fb);
                res = ESP_FAIL;
                break;
            }
        } else {
            _jpg_buf_len = fb->len;
            _jpg_buf = fb->buf;
        }
        
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, "--frame\r\n", strlen("--frame\r\n"));
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, "Content-Type: image/jpeg\r\n\r\n", 
                                     strlen("Content-Type: image/jpeg\r\n\r\n"));
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, "\r\n", strlen("\r\n"));
        }
        
        if (fb->format != PIXFORMAT_JPEG) {
            free(_jpg_buf);
            _jpg_buf = NULL;
        }
        esp_camera_fb_return(fb);
        fb = NULL;
        
        // Check if client disconnected
        if (res != ESP_OK) {
            ESP_LOGI(TAG, "Client disconnected or error occurred");
            break;
        }
    }
    
    // Cleanup
    if (fb) {
        esp_camera_fb_return(fb);
    }
    if (_jpg_buf) {
        free(_jpg_buf);
    }
    
    return res;
}

// URI handlers
static const httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t jpg_uri = {
    .uri       = "/jpg",
    .method    = HTTP_GET,
    .handler   = jpg_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t stream_uri = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
};

void start_camera_server(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.server_port = 80;
    
    ESP_LOGI(TAG, "Starting web server on port: '%d'", config.server_port);
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &jpg_uri);
        httpd_register_uri_handler(server, &stream_uri);
        ESP_LOGI(TAG, "Web server started successfully");
    } else {
        ESP_LOGE(TAG, "Error starting web server!");
    }
}