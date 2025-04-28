#include"WifiManagerEsp32.hpp"
#include<stdio.h>
#include <cstring>
#include<esp_log.h>
#include<memory>
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/ip_addr.h"
#include"dnsRedirector.h"
#include"customEvents.hpp"
#include"spiffsControler.hpp"
#include"jsonDecoder.hpp"
#include<utility>


static const char *TAG = "WIFI Mgr";

void set_wifi_ap_ip(esp_netif_t *esp_netif_ap)
{

    esp_netif_ip_info_t IP_settings_ap;

    IP_settings_ap.ip.addr=ipaddr_addr("4.3.2.1");
    IP_settings_ap.gw.addr=ipaddr_addr("4.3.2.1");
    IP_settings_ap.netmask.addr=ipaddr_addr("255.255.255.0");
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(esp_netif_ap));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(esp_netif_ap, &IP_settings_ap));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(esp_netif_ap));

}

static void wifiEventHandler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    auto wifiManagerIdf = static_cast<WifiManagerEsp32*>(arg);

    if(!wifiManagerIdf)
    {
        ESP_LOGE(TAG,"[DEBUG]casting is not ok");
    }
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG,"WIFI_EVENT_AP_STACONNECTED");

    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG,"WIFI_EVENT_AP_STADISCONNECTED");

    }
    else if (event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG,"WIFI_EVENT_STA_START");
        ESP_LOGI(TAG,"Attempting to connect to WiFi...");
        if (wifiManagerIdf->credentials_opt.has_value()) {
            ESP_LOGI(TAG,"Using credentials: SSID=%s", wifiManagerIdf->credentials_opt.value()["ssid"].c_str());
        }
        esp_wifi_connect();
    }
    else if (event_id == WIFI_EVENT_STA_STOP) {
        ESP_LOGI(TAG,"WIFI_EVENT_STA_STOP");
        // esp_wifi_connect();

    }
    else if (event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG,"WIFI_EVENT_AP_START");

    }
    else if (event_id == WIFI_EVENT_AP_STOP) {
        ESP_LOGI(TAG,"WIFI_EVENT_AP_STOP");

    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG,"WIFI_EVENT_STA_CONNECTED");
        if(!wifiManagerIdf->staStarted_opt.has_value())
        {
            wifiManagerIdf->staStarted_opt = true; // ToDo consider removing it
        }

    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data; //TODO null check

        ESP_LOGI(TAG,"WIFI_EVENT_STA_DISCONNECTED reasonId %d",event->reason);

        // Print detailed error message based on reason code
        switch(event->reason) {
            case 1: ESP_LOGE(TAG, "Unspecified reason"); break;
            case 2: ESP_LOGE(TAG, "Auth expire"); break;
            case 3: ESP_LOGE(TAG, "Auth leave"); break;
            case 4: ESP_LOGE(TAG, "Assoc expire"); break;
            case 5: ESP_LOGE(TAG, "Assoc too many"); break;
            case 6: ESP_LOGE(TAG, "Not authed"); break;
            case 7: ESP_LOGE(TAG, "Not assoced"); break;
            case 8: ESP_LOGE(TAG, "Assoc leave"); break;
            case 9: ESP_LOGE(TAG, "Assoc not authed"); break;
            case 10: ESP_LOGE(TAG, "Disassoc pwrcap bad"); break;
            case 11: ESP_LOGE(TAG, "Disassoc supchan bad"); break;
            case 12: ESP_LOGE(TAG, "IE invalid"); break;
            case 13: ESP_LOGE(TAG, "MIC failure"); break;
            case 14: ESP_LOGE(TAG, "4way handshake timeout"); break;
            case 15: ESP_LOGE(TAG, "Group key handshake timeout"); break;
            case 16: ESP_LOGE(TAG, "IE in 4way differs"); break;
            case 17: ESP_LOGE(TAG, "Group cipher invalid"); break;
            case 18: ESP_LOGE(TAG, "Pairwise cipher invalid"); break;
            case 19: ESP_LOGE(TAG, "AKMP invalid"); break;
            case 20: ESP_LOGE(TAG, "Unsupp RSN IE version"); break;
            case 21: ESP_LOGE(TAG, "Invalid RSN IE cap"); break;
            case 22: ESP_LOGE(TAG, "802.1x auth failed"); break;
            case 23: ESP_LOGE(TAG, "Cipher suite rejected"); break;
            case 200: ESP_LOGE(TAG, "Beacon timeout"); break;
            case 201: ESP_LOGE(TAG, "No AP found with specified SSID"); break;
            case 202: ESP_LOGE(TAG, "Authentication failed - wrong password"); break;
            case 203: ESP_LOGE(TAG, "No AP found with specified BSSID"); break;
            case 204: ESP_LOGE(TAG, "Handshake timeout"); break;
            case 205: ESP_LOGE(TAG, "Connection failed"); break;
            default: ESP_LOGE(TAG, "Unknown reason: %d", event->reason); break;
        }

        if(!wifiManagerIdf->staStarted_opt.has_value()) wifiManagerIdf->staStarted_opt = false;

        // Try to reconnect for certain error types
        if(event->reason == 201 or event->reason == 202 or event->reason == 15) // wrong ssid or password or handshake timeout
        {
            ESP_LOGI(TAG, "Reconnecting to WiFi with AP mode...");
            wifiManagerIdf->setupWiFi(true, true);
            wifiManagerIdf->setupServerAndDns();
        } else {
            // For other errors, try to reconnect without AP mode
            ESP_LOGI(TAG, "Attempting to reconnect to WiFi...");
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to reconnect to WiFi: %d", err);
            }
        }
    }
    else if(event_id == WIFI_EVENT_SCAN_DONE)
    {
        ESP_LOGI(TAG,"WIFI_EVENT_SCAN_DONE");
        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);
        wifi_ap_record_t* accessPoints =  new wifi_ap_record_t[ap_count];
        esp_wifi_scan_get_ap_records(&ap_count,accessPoints);

        wifiManagerIdf->foundedAPs = std::vector<wifi_ap_record_t>(ap_count); //TODO  this is unnecessary since we have sending response immediately

        for(auto i = 0; i<ap_count;++i)
        {
            if(accessPoints[i].rssi == 0) continue; //skiping self
            wifiManagerIdf->foundedAPs.push_back(accessPoints[i]);
            ESP_LOGI(TAG,"SSID: %s RSSI %d",(const char*)accessPoints[i].ssid,accessPoints[i].rssi);
        }
        wifiManagerIdf->sendScannedAP();
        delete[] accessPoints;
    }
    else
    {
        ESP_LOGI(TAG,"Other event. ID:%d",int(event_id));
    }
}

static void customEventsHandler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{

    auto wifiMgrPtr = static_cast<WifiManagerEsp32*>(arg);


    if (event_id == CREDENTIALS_AQUIRED)
    {
        ESP_LOGI(TAG,"CREDENTIALS_AQUIRED event received");
        if(wifiMgrPtr)
        {
            auto ptr= static_cast<char*>(event_data); //TODO do not pass associative container through esp_event_post due to memcpy
            if(ptr == nullptr)
            {
                ESP_LOGE(TAG,"Bad casting");
                return;
            }
            auto credentials_temp = JsonDecoder::decodeJsonCredentials(ptr);
            wifiMgrPtr->credentials_opt = credentials_temp;
            if(wifiMgrPtr->managerConfig.shouldKeepAP)
            {
                ESP_LOGI(TAG,"Starting AP_STA");
                wifiMgrPtr->setupWiFi(wifiMgrPtr->managerConfig.shouldKeepAP,true);
            }
            else{
                ESP_LOGI(TAG,"Starting only STA");
                 wifiMgrPtr->setupWiFi(wifiMgrPtr->managerConfig.shouldKeepAP,true); //TODO duplication
            }

            if(wifiMgrPtr->credentials_opt.has_value())
            {
                SPIFFSControler spiffsControler;
                spiffsControler.writeCredentials(wifiMgrPtr->credentials_opt.value());
            }

        }
    }
    else if (event_id == SCAN_AVAILABLE_APS)
    {
        ESP_LOGI(TAG,"SCAN_AVAILABLE_APS event received");
        wifiMgrPtr->scanAvailableWifiNetworks();
    }
    else if (event_id == SEND_DEFAULT_PARAMETERS)
    {
        ESP_LOGI(TAG,"SEND_DEFAULT_PARAMETERS event received");
        wifiMgrPtr->sendCustomParameters();
    }
    else if(event_id == CUSTOM_PARAMETERS_RECEIVED)
    {
        ESP_LOGI(TAG,"CUSTOM_PARAMETERS_RECEIVED event received");
        auto customParams_ptr = static_cast<char*>(event_data);
        auto decodedJson = JsonDecoder::decodeJson(customParams_ptr);

        if(decodedJson.has_value())
        {
            wifiMgrPtr->managerConfig.customParametersMap = decodedJson.value();
        }
        else{
            ESP_LOGE(TAG,"Custom params decoding error");
        }
        if(wifiMgrPtr->managerConfig.customParametersReceivedCallback) // TODO maybe wrap it into some helper function
        {
            wifiMgrPtr->invokeCustomParamsCallbackFunction();
        }
    }
    else{
        ESP_LOGE(TAG,"Undefined custom event received. Id %d",int(event_id));
    }
}

WifiManagerEsp32::WifiManagerEsp32(const WifiManagerIdfConfig& p_managerConfig):
managerConfig(p_managerConfig)
{

    ap_config = {};
    strncpy((char*)ap_config.ap.ssid, managerConfig.ssid.c_str(), sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = managerConfig.ssid.length();
    ap_config.ap.max_connection = 1;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;

    sta_config = {};

    scan_config = {
    .ssid = 0,
    .bssid = 0,
    .channel = 0,
    .show_hidden = true
  };

    ESP_ERROR_CHECK(esp_netif_init());

    // Check if event loop already exists
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }

    reqisterCutomEvents();
    initWifi();
    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap(); //TODO deinit this in destructor or something
    set_wifi_ap_ip(esp_netif_ap);

    bool credFetched = tryFetchCredentialsFromSPIFFS();
    if(credFetched)
    {
        setupWiFi(managerConfig.shouldKeepAP,true);
        if(managerConfig.shouldKeepAP) setupServerAndDns();
    }
    else{
        setupWiFi(true,true);
        setupServerAndDns();
    }

}

void WifiManagerEsp32::invokeCustomParamsCallbackFunction()
{
    managerConfig.customParametersReceivedCallback(this);
}

void WifiManagerEsp32::setupServerAndDns()
{

    bool serverStarted = startHttpServer();
    if(serverStarted)
    {
        ESP_LOGI(TAG,"[DEBUG] Server started");
        start_dns_server();
    }
    else
    {
        ESP_LOGI(TAG,"[DEBUG] Server NOT started");
    }
}

void WifiManagerEsp32::initWifi()
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.ampdu_rx_enable = false; // no idea what i am doing here but this is for android captive portal
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                    ESP_EVENT_ANY_ID,
                                                    &wifiEventHandler,
                                                    this,
                                                    NULL));
}


void WifiManagerEsp32::reqisterCutomEvents()
{

    ESP_ERROR_CHECK(esp_event_handler_register(CUSTOM_EVENTS,
                                                    ESP_EVENT_ANY_ID,
                                                    &customEventsHandler,
                                                    this));
    // ESP_ERROR_CHECK(esp_event_post(CUSTOM_EVENTS, CREDENTIALS_AQUIRED, nullptr, 0,portMAX_DELAY));


}


bool WifiManagerEsp32::setupWiFi(bool keepAP, bool andRun)
{
    if(credentials_opt.has_value())
    {
        auto ssid = credentials_opt.value()["ssid"].c_str();
        auto password = credentials_opt.value()["password"].c_str();

        // Clear the config structure first
        memset(&sta_config, 0, sizeof(sta_config));

        // Set the SSID and password
        strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
        strncpy((char*)sta_config.sta.password, password, sizeof(sta_config.sta.password) - 1);

        // Set authentication mode to auto-detect
        sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

        // Set scan method to all channels
        sta_config.sta.scan_method = WIFI_FAST_SCAN;

        // Set connection timeout
        sta_config.sta.threshold.rssi = -127;

        // Print debug info
        ESP_LOGI(TAG, "Setting up WiFi with SSID: %s", ssid);

        // Set WiFi mode
        ESP_ERROR_CHECK(esp_wifi_set_mode(keepAP ? WIFI_MODE_APSTA : WIFI_MODE_STA));

        // Set STA config
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

        // Set AP config if needed
        if(keepAP) {
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
        }

        // Start WiFi if needed
        if(andRun) {
            ESP_ERROR_CHECK(esp_wifi_stop());
            ESP_ERROR_CHECK(esp_wifi_start());
        }
    }
    else
    {
        ESP_LOGE(TAG, "Credentials for STA mode haven't been set");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        if(keepAP) ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
        if(andRun) {
            ESP_ERROR_CHECK(esp_wifi_stop());
            ESP_ERROR_CHECK(esp_wifi_start());
        }
        return false;
    }
    return true;
}


bool WifiManagerEsp32::startHttpServer()
{
    if(httpServer_ptr)
    {
        ESP_LOGD(TAG,"HttpServer already started");
        return false;
    }

    httpServer_ptr = std::make_unique<HttpServer>();
    return httpServer_ptr->startServer(!managerConfig.customParametersMap.empty(),managerConfig.enableLogger);
}

void WifiManagerEsp32::stopHttpServer()
{
    if(httpServer_ptr == nullptr)
    {
        ESP_LOGE(TAG,"HttpServer already stopped");
    }
    httpServer_ptr.reset(); // it calls HttpServer destructor

}

bool WifiManagerEsp32::tryFetchCredentialsFromSPIFFS()
{
    SPIFFSControler spiffsControler;


    credentials_opt = spiffsControler.readCredentials();

    if(credentials_opt.has_value() && credentials_opt.value().find("ssid") != credentials_opt.value().end() && !credentials_opt.value()["ssid"].empty())
    {

        ESP_LOGD(TAG, "Credentials readed");
        return true;
    }
    else{

        ESP_LOGE(TAG, "Credentials NOT readed");

    }

    return false;
}

void WifiManagerEsp32::scanAvailableWifiNetworks() // wifi has to be in sta mode
{

  ESP_LOGE(TAG, "Scanning available networks");
  ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
}

void WifiManagerEsp32::sendScannedAP()
{
    httpServer_ptr->sendScanedAPs(foundedAPs);
}

void WifiManagerEsp32::sendCustomParameters()
{
    httpServer_ptr->sendCustomParams(managerConfig.customParametersMap);
}

void WifiManagerEsp32::sendLog(std::string log) // TODO check if server is available
{
    if(httpServer_ptr == nullptr)
    {
        ESP_LOGE(TAG,"Log can not be send due to lack of server setup");
        return;
    }
    if(httpServer_ptr->loggerSocketDescriptor == -1)
    {
        ESP_LOGE(TAG, "Web socket for logging is not available yet");
        return;
    }
    httpServer_ptr->sendLog(log);
}

WifiManagerEsp32::~WifiManagerEsp32()
{
    stopHttpServer();
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_deinit();
}