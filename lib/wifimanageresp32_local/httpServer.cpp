#include"httpServer.hpp"
#include"indexHtml.h"
#include"style.h"
#include"script.h"
#include "esp_spiffs.h"
#include"jsonDecoder.hpp"
#include"customEvents.hpp"

#include<esp_log.h>
#include<esp_event.h>
#include<map>
#include<memory>



static const char *TAG = "HTTPServer";


static esp_err_t get_html_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG,"GET html");
    const char* resp_str = html_page;
    
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    // ESP_ERROR_CHECK(esp_event_post(CUSTOM_EVENTS,SCAN_AVAILABLE_APS,nullptr,0,portMAX_DELAY));
    return ESP_OK;
}

static esp_err_t get_js_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG,"GET js");
    const char* resp_str = script;
    
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    // ESP_ERROR_CHECK(esp_event_post(CUSTOM_EVENTS,SCAN_AVAILABLE_APS,nullptr,0,portMAX_DELAY));
    return ESP_OK;
}

static esp_err_t get_css_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG,"GET css");
    const char* resp_str = style;
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    // ESP_ERROR_CHECK(esp_event_post(CUSTOM_EVENTS,SCAN_AVAILABLE_APS,nullptr,0,portMAX_DELAY));
    return ESP_OK;
}

static esp_err_t post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG,"POST");
    constexpr auto BUFFER_LENGTH = 80;
    std::unique_ptr<char[]> charArray = std::make_unique<char[]>(BUFFER_LENGTH + 1);  // Allocate space for BUFFER_LENGTH characters + null terminator
    httpd_req_recv(req,charArray.get(),BUFFER_LENGTH);
    ESP_LOGI(TAG,"%s",charArray.get());
    ESP_ERROR_CHECK(esp_event_post(CUSTOM_EVENTS, CREDENTIALS_AQUIRED, charArray.get(),req->content_len+1,portMAX_DELAY));
    httpd_resp_send(req,"",HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t cors_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG,"OPTIONS cors handler");
    httpd_resp_set_hdr(req,"Access-Control-Allow-Headers","*");
    httpd_resp_set_hdr(req,"Access-Control-Allow-Origin","*");
    httpd_resp_set_hdr(req,"Access-Control-Allow-Methods","*");
    httpd_resp_send(req,"",HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}


esp_err_t ws_aps_handler(httpd_req_t *req) // TODO code duplication with other handlers
{
    auto httpServerWraper_ptr = static_cast<HttpServer*>(req->user_ctx);

    if (req->method == HTTP_GET) {
    ESP_LOGI(TAG, "Handshake done for APS, the new connection was opened");
    return ESP_OK;
    }
    httpd_ws_frame_t ws_pkt = {};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT; // necessary?
    auto ret = httpd_ws_recv_frame(req,&ws_pkt,0);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    auto buff = std::make_unique<u_int8_t[]>(ws_pkt.len);
    ESP_LOGE(TAG,"Package length = %d",ws_pkt.len);
    if (ws_pkt.len)
    {

        ws_pkt.payload = buff.get();
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            return ret;
        }
        ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
    }

    httpServerWraper_ptr->APsSocketDescriptor = httpd_req_to_sockfd(req);
    ESP_ERROR_CHECK(esp_event_post(CUSTOM_EVENTS,SCAN_AVAILABLE_APS,nullptr,0,portMAX_DELAY));

    return ESP_OK;
}
esp_err_t ws_custmo_params_handler(httpd_req_t *req)
{
    auto httpServerWraper_ptr = static_cast<HttpServer*>(req->user_ctx);

    if (req->method == HTTP_GET) {
    ESP_LOGI(TAG, "Handshake done for CP, the new connection was opened");
    return ESP_OK;
    }
    httpd_ws_frame_t ws_pkt = {};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT; // TODO necessary?
    auto ret = httpd_ws_recv_frame(req,&ws_pkt,0);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    auto buff = std::make_unique<u_int8_t[]>(ws_pkt.len);
    ESP_LOGE(TAG,"Package length = %d",ws_pkt.len);
    if (ws_pkt.len)
    {

        ws_pkt.payload = buff.get();
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            return ret;
        }
        ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
    }
    

    httpServerWraper_ptr->customParamsSocketDescriptor = httpd_req_to_sockfd(req);
    std::string respPayload((char*) ws_pkt.payload,ws_pkt.len);
    if(respPayload == "ping")
    {
        ESP_ERROR_CHECK(esp_event_post(CUSTOM_EVENTS,SEND_DEFAULT_PARAMETERS,nullptr,0,portMAX_DELAY));
    }
    else
    {   
        ESP_ERROR_CHECK(esp_event_post(CUSTOM_EVENTS,CUSTOM_PARAMETERS_RECEIVED,const_cast<char*>(respPayload.c_str()),respPayload.length(),portMAX_DELAY));
    }
    
    return ESP_OK;
}

esp_err_t ws_logger_handler(httpd_req_t *req)
{
    auto httpServerWraper_ptr = static_cast<HttpServer*>(req->user_ctx);

    if (req->method == HTTP_GET) {
    ESP_LOGI(TAG, "Handshake done for logger, the new connection was opened");
    return ESP_OK;
    }
    httpd_ws_frame_t ws_pkt = {};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT; // TODO necessary?
    auto ret = httpd_ws_recv_frame(req,&ws_pkt,0);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    auto buff = std::make_unique<u_int8_t[]>(ws_pkt.len);
    ESP_LOGE(TAG,"Package length = %d",ws_pkt.len);
    if (ws_pkt.len)
    {

        ws_pkt.payload = buff.get();
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            return ret;
        }
        ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
    }
    
    httpServerWraper_ptr->loggerSocketDescriptor = httpd_req_to_sockfd(req);
    return ESP_OK;
}

void sendAPsCallback(void *arg)
{
    auto httpServer_ptr = static_cast<HttpServer*>(arg);
    static httpd_ws_frame_t ws_pkt;
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    // auto payload = httpServer_ptr->getAPsMessagePayload().c_str();
    ws_pkt.payload = (uint8_t*)httpServer_ptr->getAPsMessagePayload().c_str();
    ws_pkt.len = httpServer_ptr->getAPsMessagePayload().length();

    httpd_ws_client_info_t clientInfo = httpd_ws_get_fd_info(httpServer_ptr->getServerHandle(),httpServer_ptr->APsSocketDescriptor);
    if(clientInfo != HTTPD_WS_CLIENT_WEBSOCKET)
    {
        ESP_LOGE(TAG,"Invalid socket type. Response not sended %d", clientInfo);
        return;
    }

    auto ret = httpd_ws_send_frame_async(httpServer_ptr->getServerHandle(),httpServer_ptr->APsSocketDescriptor,&ws_pkt);
    if (ret!= ESP_OK) ESP_LOGE(TAG,"%s",esp_err_to_name(ret));

}

void sendCustomParamsCallback(void *arg) //TODO code duplication
{
    auto httpServer_ptr = static_cast<HttpServer*>(arg);
    static httpd_ws_frame_t ws_pkt;
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.payload = (uint8_t*)httpServer_ptr->getCustomParametersMessagePayload().c_str(); //TODO oduplication of func call
    ws_pkt.len = httpServer_ptr->getCustomParametersMessagePayload().length();

    httpd_ws_client_info_t clientInfo = httpd_ws_get_fd_info(httpServer_ptr->getServerHandle(),httpServer_ptr->customParamsSocketDescriptor);
    if(clientInfo != HTTPD_WS_CLIENT_WEBSOCKET)
    {
        ESP_LOGE(TAG,"Invalid socket type. Response not sended %d", clientInfo);
        return;
    }

    auto ret = httpd_ws_send_frame_async(httpServer_ptr->getServerHandle(),httpServer_ptr->customParamsSocketDescriptor,&ws_pkt);
    if (ret!= ESP_OK) ESP_LOGE(TAG,"%s",esp_err_to_name(ret));

}

void sendLogCallback(void *arg) //TODO code duplication
{
    auto httpServer_ptr = static_cast<HttpServer*>(arg);
    static httpd_ws_frame_t ws_pkt;
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.payload = (uint8_t*)httpServer_ptr->getLogMessagePayload().c_str(); //TODO oduplication of func call
    ws_pkt.len = httpServer_ptr->getLogMessagePayload().length();

    httpd_ws_client_info_t clientInfo = httpd_ws_get_fd_info(httpServer_ptr->getServerHandle(),httpServer_ptr->loggerSocketDescriptor);
    if(clientInfo != HTTPD_WS_CLIENT_WEBSOCKET)
    {
        ESP_LOGE(TAG,"Invalid socket type. Response not sended %d", clientInfo);
        return;
    }

    auto ret = httpd_ws_send_frame_async(httpServer_ptr->getServerHandle(),httpServer_ptr->loggerSocketDescriptor,&ws_pkt);
    if (ret!= ESP_OK) ESP_LOGE(TAG,"%s",esp_err_to_name(ret));

}
HttpServer::HttpServer()
{
    uri_get_html = {};
    uri_get_html.uri = "/";
    uri_get_html.method = HTTP_GET;
    uri_get_html.user_ctx = nullptr;
    uri_get_html.handler = get_html_handler;
    
    uri_get_js = {};
    uri_get_js.uri = "/myScript.js";
    uri_get_js.method = HTTP_GET;
    uri_get_js.user_ctx = nullptr;
    uri_get_js.handler = get_js_handler;

    uri_get_css = {};
    uri_get_css.uri = "/styles.css";
    uri_get_css.method = HTTP_GET;
    uri_get_css.user_ctx = nullptr;
    uri_get_css.handler = get_css_handler;

    androidCptv = {};
    androidCptv.uri = "/generate_204";
    androidCptv.method = HTTP_GET;
    androidCptv.user_ctx = nullptr;
    androidCptv.handler = get_html_handler;

    microsiftCptv = {};
    microsiftCptv.uri = "/redirect";
    microsiftCptv.method = HTTP_GET;
    microsiftCptv.user_ctx = nullptr;
    microsiftCptv.handler = get_html_handler;
    

    uri_postCredentials = {};
    uri_postCredentials.uri = "/postCredentials";
    uri_postCredentials.method = HTTP_POST;
    uri_postCredentials.user_ctx = nullptr;
    uri_postCredentials.handler = post_handler;


    uri_post = {};
    uri_post.uri = "/postCredentials";
    uri_post.method = HTTP_OPTIONS;
    uri_post.user_ctx = nullptr;
    uri_post.handler = cors_handler;

    uri_patch= {};
    uri_patch.uri = "/postCredentials";
    uri_patch.method = HTTP_PATCH;
    uri_patch.user_ctx = nullptr;
    uri_patch.handler = cors_handler;

  
    ws_APs_uri_handler_options= {};
    ws_APs_uri_handler_options.uri = "/ws";
    ws_APs_uri_handler_options.method = HTTP_GET;
    ws_APs_uri_handler_options.user_ctx = this;
    ws_APs_uri_handler_options.handler =  ws_aps_handler;
    ws_APs_uri_handler_options.is_websocket = true;

    ws_custom_params_uri_handler_options = {};
    ws_custom_params_uri_handler_options.uri = "/cp"; //cp -custom parameters
    ws_custom_params_uri_handler_options.method = HTTP_GET;
    ws_custom_params_uri_handler_options.user_ctx = this;
    ws_custom_params_uri_handler_options.handler =  ws_custmo_params_handler;
    ws_custom_params_uri_handler_options.is_websocket = true;

    ws_logger_uri_handler_options = {};
    ws_logger_uri_handler_options.uri = "/log"; //cp -custom parameters
    ws_logger_uri_handler_options.method = HTTP_GET;
    ws_logger_uri_handler_options.user_ctx = this;
    ws_logger_uri_handler_options.handler =  ws_logger_handler;
    ws_logger_uri_handler_options.is_websocket = true;

}

void HttpServer::sendScanedAPs(const std::vector<wifi_ap_record_t>& ap)
{
    std::map<std::string,std::string> valMap;

    for (const auto& item : ap)
    {
        valMap.emplace(std::make_pair<std::string,std::string>((char *)item.ssid,std::to_string(item.rssi)));
    }
    APsMessagePayload = JsonDecoder::encodeJson(valMap); //TODO free string memory after send

    httpd_queue_work(server,sendAPsCallback,this);

}

void HttpServer::sendCustomParams(const std::map<std::string,std::string>& params)
{
    customParamsMessagePayload = JsonDecoder::encodeJson(params);//TODO free string memory after send
    ESP_LOGE(TAG,"PARRAMS: %s",customParamsMessagePayload.c_str());
    httpd_queue_work(server,sendCustomParamsCallback,this);
}

void HttpServer::sendLog(std::string log)
{
    logMessagePayload = log;
    httpd_queue_work(server,sendLogCallback,this);
}

HttpServer::~HttpServer()
{
    stopServer();
}

bool HttpServer::startServer(bool enable_custom_params_socket,bool enable_logger_socket)
{

    /* Generate default configuration */
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 9;

    /* Empty handle to esp_http_server */
    server = nullptr;

    /* Start the httpd server */
    auto err = httpd_start(&server, &config);
    if (err == ESP_OK) {
        /* Register URI handlers */
        httpd_register_uri_handler(server, &uri_get_html);
        httpd_register_uri_handler(server, &uri_get_js);
        httpd_register_uri_handler(server, &uri_get_css);
        httpd_register_uri_handler(server, &uri_post);
        httpd_register_uri_handler(server, &uri_postCredentials);
        httpd_register_uri_handler(server, &androidCptv);
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ws_APs_uri_handler_options));
        if(enable_custom_params_socket) ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ws_custom_params_uri_handler_options));
        if(enable_logger_socket) ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ws_logger_uri_handler_options));

        return true;
    }
    else{
        /* If server failed to start, handle will be NULL */
        ESP_LOGE(TAG, "Failed to start a server (%s)", esp_err_to_name(err));
        return false;
    }

}

void HttpServer::stopServer()
{
    if (server)
        {
            httpd_stop(server);
        }
}