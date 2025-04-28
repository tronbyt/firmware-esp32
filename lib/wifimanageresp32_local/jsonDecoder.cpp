#include"jsonDecoder.hpp"
#include <cJSON.h>
#include<esp_log.h>

const char* TAG = "JSONdecoder";


std::optional<std::map<std::string, std:: string>> JsonDecoder::decodeJsonCredentials(const std::string& jsonContent)
{

    std::map<std::string, std::string> results;
    
        // Parse JSON response
    cJSON* root = cJSON_Parse(jsonContent.c_str());
    if (root == NULL) {
        const char* error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "Error before: %s\n", error_ptr);
        }
        cJSON_Delete(root);  // Cleanup cJSON
        // Handle parsing error
        return std::nullopt;
    }

    // Extract data from JSON
    cJSON* ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    if (cJSON_IsString(ssid) && (ssid->valuestring != NULL)) {
        ESP_LOGE(TAG, "ssid: %s\n", ssid->valuestring);
        results.insert(std::make_pair<std::string,std::string>("ssid",ssid->valuestring));
    }

    cJSON* password = cJSON_GetObjectItemCaseSensitive(root, "password");
    if (cJSON_IsString(password)) {
        ESP_LOGE(TAG, "password: %s\n", password->valuestring);
        results.insert(std::make_pair<std::string,std::string>("password",password->valuestring));
    }

    cJSON_Delete(root);  // Cleanup cJSON
    return results;
}

std::string JsonDecoder::encodeJson(const std::map<std::string, std:: string>& valuesToEncode)
{
    cJSON* root = cJSON_CreateObject();


    for(const auto& pair : valuesToEncode)
    {
        cJSON_AddStringToObject(root, pair.first.c_str(), pair.second.c_str());
    }

    char* jsonString = cJSON_Print(root);
    std::string encodedJson(jsonString);

    cJSON_free(jsonString);
    cJSON_Delete(root);

    return encodedJson;
}

std::optional<std::map<std::string, std:: string>> JsonDecoder::decodeJson(const std::string& jsonContent)
{
    cJSON* root = cJSON_Parse(jsonContent.c_str());
    if(root == NULL) {
        const char* error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            printf("Error before: %s\n", error_ptr);
        }
        cJSON_Delete(root);
        return std::nullopt;
    }
    std::map<std::string,std::string> customParametersMap;
    cJSON *current_item = root->child;
    while(current_item != nullptr)
    {
        customParametersMap[current_item->string] = current_item->valuestring;
        current_item = current_item->next;
    }
    

    cJSON_Delete(root);
    return customParametersMap;
}