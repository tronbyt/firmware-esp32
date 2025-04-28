#include"spiffsControler.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include"jsonDecoder.hpp"
#include <cstdio>

constexpr char* TAG = "SPIFFS";

SPIFFSControler::SPIFFSControler()
{
    ESP_LOGI(TAG, "Initializing SPIFFS");
    conf =  {
        .base_path = SPIFFS_BASE_PATH,
        .partition_label = NULL,
        .max_files = 1,  // Maximum number of open files
        .format_if_mount_failed = true
    };
    initializedSuccesfull = init();
}

bool SPIFFSControler::init()
{

    // // esp_err_t err = esp_spiffs_format(SPIFFS_BASE_PATH);
    // if (err != ESP_OK) {
    //  ESP_LOGE(TAG, "Error esp_spiffs_format(1024)");
    // }
     esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return false;
    }

    ESP_LOGI(TAG, "Performing SPIFFS_check().");
    ret = esp_spiffs_check(conf.partition_label);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
        return false;;
    } else {
        ESP_LOGI(TAG, "SPIFFS_check() successful");
    }

    return true;
}

SPIFFSControler::~SPIFFSControler()
{
    auto ret = esp_vfs_spiffs_unregister(conf.partition_label);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG,"Failed during esp_vfs_spiffs_unregister");
    }
    ESP_LOGI(TAG, "SPIFFS unmounted");
}

bool SPIFFSControler::writeCredentials(std::map<std::string, std:: string> credantialsMap_opt)
{
    if(!initializedSuccesfull)
    {
        ESP_LOGE(TAG, "Failed to init spiffs");
        return false;
    }
        
    std::string endodedJsonString = JsonDecoder::encodeJson(credantialsMap_opt);
    ESP_LOGE(TAG, "Cred in writeCredentials %s",endodedJsonString.c_str());

    ESP_LOGI(TAG, "Opening file");
    FILE* f = fopen("/spiffs/credentials.json", "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return false;
    }
    fwrite(&endodedJsonString[0], sizeof(char), endodedJsonString.size(),f);
    fclose(f);
    ESP_LOGI(TAG, "File written");
    return true;
}

std::optional<std::map<std::string, std::string>> SPIFFSControler::readCredentials()
{
    if (!initializedSuccesfull)
    {
        ESP_LOGE(TAG, "Failed to init spiffs");
        return std::nullopt;
    }

    std::map<std::string, std::string> credentialsMap_tmp;

    FILE* f = fopen("/spiffs/credentials.json", "r"); // w- neets to be to create a file when it's not present 
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return std::nullopt;
    }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    rewind(f);

    std::string jsonData;
    jsonData.resize(fileSize);
    fread(&jsonData[0], sizeof(char), fileSize, f);
    fclose(f);
    ESP_LOGI(TAG, "Credentials: %s",jsonData.c_str());

    // Parse the JSON data and populate the credentials map
    auto decodedJson = JsonDecoder::decodeJsonCredentials(jsonData);
    if(decodedJson.has_value())
    {
        credentialsMap_tmp = decodedJson.value();
    }
    else
    {
        return std::nullopt;
    }

    return credentialsMap_tmp;
}