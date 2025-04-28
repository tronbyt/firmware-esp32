#pragma once
#include<optional>
#include<map>
#include<string>
#include<optional>
#include "esp_spiffs.h"

class SPIFFSControler
{

esp_vfs_spiffs_conf_t conf;
static constexpr char* SPIFFS_BASE_PATH = "/spiffs";
bool initializedSuccesfull = false;

    bool init();

public:
    SPIFFSControler();
    ~SPIFFSControler();
    bool writeCredentials(std::map<std::string, std:: string> credantialsMap_opt);
    std::optional<std::map<std::string, std:: string>> readCredentials();
};