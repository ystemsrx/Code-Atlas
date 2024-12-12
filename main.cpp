#include "global.h"
#include "model_caller.h"
#include <windows.h>
#include <iostream>
#include <fstream>

int main() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::ifstream configFile("config.json");
    if (!configFile.is_open()) {
        std::cout << "无法打开 config.json 文件" << std::endl;
        return 1;
    }

    nlohmann::json configJson;
    try {
        configFile >> configJson;
    } catch (const nlohmann::json::parse_error& e) {
        std::cout << "解析 config.json 文件失败: " << e.what() << std::endl;
        return 1;
    }

    bool useAPI = false;
    if (configJson[3]["API"] == "True") {
        useAPI = true;
    }

    if (useAPI) {
        modelCaller.reset(new APIModelCaller());
    } else {
        modelCaller.reset(new LocalModelCaller());
    }

    if (!modelCaller->initialize(configJson)) {
        return 1;
    }

    modelCaller->startInputOutputLoop();

    SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);

    return 0;
}
