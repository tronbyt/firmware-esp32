#pragma once
#include<string>
#include<map>
#include <optional>

class JsonDecoder
{
public:
    static std::optional<std::map<std::string, std:: string>>  decodeJsonCredentials(const std::string& jsonContent); // TODO delete  and use decodeJson
    static std::optional<std::map<std::string, std:: string>> decodeJson(const std::string& jsonContent);
    static std::string encodeJson(const std::map<std::string, std:: string>& valuesToEncede);
};