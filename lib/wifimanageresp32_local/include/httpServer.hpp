#pragma once
#include "esp_http_server.h"
#include<optional>
#include<map>
#include<string>
#include <functional>
#include<vector>
#include"esp_wifi_types.h" // for wifi_ap_record_t

class HttpServer
{
    httpd_handle_t server;
    httpd_uri_t uri_get_html;
    httpd_uri_t uri_get_js;
    httpd_uri_t uri_get_css;
    httpd_uri_t uri_post;
    httpd_uri_t androidCptv;
    httpd_uri_t microsiftCptv;
    httpd_uri_t uri_patch;
    httpd_uri_t uri_postCredentials;
    httpd_uri_t ws_APs_uri_handler_options;
    httpd_uri_t ws_custom_params_uri_handler_options;
    httpd_uri_t ws_logger_uri_handler_options;

    std::string APsMessagePayload; // JSON encoded
    std::string customParamsMessagePayload; // JSON encoded
    std::string logMessagePayload;


    // std::function<esp_err_t(httpd_req_t*)> handler;


public:
    
    int APsSocketDescriptor=1;
    int loggerSocketDescriptor=-1;
    int customParamsSocketDescriptor=-1;

    HttpServer();
    void sendScanedAPs(const std::vector<wifi_ap_record_t>&);
    void sendCustomParams(const std::map<std::string,std::string>&);
    ~HttpServer();
    bool startServer(bool enable_custom_params_socket,bool enable_logger_socket); 
    void stopServer(); // TODO consider moving it to private scope
    httpd_handle_t& getServerHandle(){return server;};
    std::string& getAPsMessagePayload(){return APsMessagePayload;};
    std::string& getCustomParametersMessagePayload(){return customParamsMessagePayload;};
    std::string& getLogMessagePayload(){return logMessagePayload;};
    void sendLog(std::string log);
    
};