#ifndef MODEL_CALLER_H
#define MODEL_CALLER_H

#include "global.h"
#include "markdown_processor.h"

// 定义抽象类ModelCaller作为模型调用接口
class ModelCaller {
public:
    virtual ~ModelCaller() {}
    virtual bool initialize(const json& configJson) = 0;
    virtual void startInputOutputLoop() = 0;
    virtual void processResponse(const std::string& response) = 0;
    virtual void executePendingCodeBlocks() = 0;
};

// 前置声明
size_t StreamCallback(char* ptr, size_t size, size_t nmemb, void* userdata);
void TimerThread();

class LocalModelCaller : public ModelCaller {
private:
    PROCESS_INFORMATION pi;
    HANDLE hChildStdOutRead;
    SECURITY_ATTRIBUTES saAttr;
    std::string systemEndMarker; 
public:
    LocalModelCaller();
    ~LocalModelCaller();

    bool initialize(const json& configJson) override;
    void startInputOutputLoop() override;
    void processResponse(const std::string& response) override;
    void executePendingCodeBlocks() override;
};

class APIModelCaller : public ModelCaller {
public:
    std::vector<json> messages;
    APIModelCaller();
    ~APIModelCaller();

    bool initialize(const json& configJson) override;
    void startInputOutputLoop() override;
    void processResponse(const std::string& response) override;
    void executePendingCodeBlocks() override;

    void doOneRequest();

private:
    std::string api_base;
    std::string api_key;
    std::string model_name;
    std::string system_role;
    json parameters;
    bool isNewResponse;
    CodeBlockElement codeBlockElement;
};

#endif
