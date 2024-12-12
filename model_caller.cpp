#include "model_caller.h"
#include "code_executor.h"

extern HANDLE hChildStdInWrite;

LocalModelCaller::LocalModelCaller() {
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
    ZeroMemory(&saAttr, sizeof(SECURITY_ATTRIBUTES));
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;
}

LocalModelCaller::~LocalModelCaller() {
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hChildStdInWrite);
    CloseHandle(hChildStdOutRead);
}

bool LocalModelCaller::initialize(const json& configJson) {
    std::string systemPrompt = configJson[0]["system"];
    std::string modelName = configJson[1]["model_name"];
    json parameters = configJson[2]["parameters"];

    std::istringstream iss(systemPrompt);
    std::vector<std::string> words;
    std::string word;
    while (iss >> word) {
        words.push_back(word);
    }

    systemEndMarker = "";
    int startIdx = (std::max)(0, (int)words.size() - 5);
    for (int i = startIdx; i < (int)words.size(); i++) {
        systemEndMarker += words[i];
        if (i < (int)words.size() - 1) {
            systemEndMarker += " ";
        }
    }

    std::string TOP_P = std::to_string(parameters["TOP_P"].get<double>());
    std::string TOP_K = std::to_string(parameters["TOP_K"].get<int>());
    std::string MAX_LENGTH = std::to_string(parameters["MAX_LENGTH"].get<int>());
    std::string TEMPERATURE = std::to_string(parameters["TEMPERATURE"].get<double>());
    std::string CONTEXT_WINDOW = std::to_string(parameters["CONTEXT_WINDOW"].get<int>());

    std::string cmdline = "llama-cli -m " + modelName + " -p \"" + systemPrompt + "\"";
    cmdline += " -n " + MAX_LENGTH;
    cmdline += " -c " + CONTEXT_WINDOW;
    cmdline += " --temp " + TEMPERATURE;
    cmdline += " --top-k " + TOP_K;
    cmdline += " --top-p " + TOP_P;
    cmdline += " -cnv";

    std::vector<char> cmdlineVec(cmdline.begin(), cmdline.end());
    cmdlineVec.push_back('\0');
    char* cmdlineCStr = cmdlineVec.data();

    HANDLE hChildStdInRead, hChildStdInWriteLocal;
    HANDLE hChildStdOutWrite;

    if (!CreatePipe(&hChildStdInRead, &hChildStdInWriteLocal, &saAttr, 0) ||
        !CreatePipe(&hChildStdOutRead, &hChildStdOutWrite, &saAttr, 0)) {
        std::cout << "创建管道失败" << std::endl;
        return false;
    }

    hChildStdInWrite = hChildStdInWriteLocal;

    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    si.hStdError = hChildStdOutWrite;
    si.hStdOutput = hChildStdOutWrite;
    si.hStdInput = hChildStdInRead;
    si.dwFlags |= STARTF_USESTDHANDLES;

    if (!CreateProcessA(NULL, cmdlineCStr, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        std::cout << "启动进程失败: " << GetLastError() << std::endl;
        return false;
    }

    CloseHandle(hChildStdOutWrite);
    CloseHandle(hChildStdInRead);

    {
        std::lock_guard<std::mutex> lock(lastChunkMutex);
        lastChunkTime = std::chrono::steady_clock::now();
    }

    std::thread outputThread([this]() {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);

        CodeBlockElement codeBlockElement;
        WORD currentColor = DEFAULT_COLOR;

        MarkdownLineProcessor lineProcessor(hConsole);
        gLineProcessor = &lineProcessor;

        char buffer[4096];
        DWORD bytesRead;

        while (true) {
            if (!ReadFile(this->hChildStdOutRead, buffer, sizeof(buffer), &bytesRead, NULL) || bytesRead == 0) {
                break;
            }
            {
                std::lock_guard<std::mutex> lock(lastChunkMutex);
                lastChunkTime = std::chrono::steady_clock::now();
            }

            std::string chunk(buffer, bytesRead);

            if (!modelLoaded && chunk.find(systemEndMarker) != std::string::npos) {
                modelLoaded = true;
                SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);
                std::cout << "\n\n模型加载完成!\n";
                SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);
                codeBlockElement.reset();
                lineProcessor.resetLineState();
                continue;
            }

            size_t pos = 0;
            while (pos < chunk.size()) {
                char ch = chunk[pos];
                if (codeBlockElement.isActive()) {
                    codeBlockElement.process(ch, pos, chunk, hConsole, currentColor);
                } else {
                    size_t oldPos = pos;
                    if (codeBlockElement.process(ch, pos, chunk, hConsole, currentColor)) {
                        continue;
                    } else {
                        lineProcessor.processChar(ch);
                        pos++;
                    }
                }
            }
        }

        lineProcessor.processChar('\n');
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), DEFAULT_COLOR);
    });
    outputThread.detach();

    std::thread timerThread(TimerThread);
    timerThread.detach();

    std::cout << "正在加载模型，请稍候...\n";
    int timeout = 0;
    while (!modelLoaded && timeout < 300) {
        Sleep(100);
        timeout++;
    }

    if (!modelLoaded) {
        std::cout << "模型加载超时!\n";
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hChildStdInWrite);
        CloseHandle(hChildStdOutRead);
        return false;
    }

    return true;
}

void LocalModelCaller::startInputOutputLoop() {
    std::string input;
    DWORD bytesWritten;
    char ch;
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    while (true) {
        std::cout.flush();
        HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode;
        GetConsoleMode(hStdin, &mode);
        SetConsoleMode(hStdin, mode & ~ENABLE_LINE_INPUT);

        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        int cursorStartX = 2;
        input.clear();
        while (true) {
            DWORD readCount;
            if (ReadConsoleA(hStdin, &ch, 1, &readCount, NULL)) {
                if (ch == '\r') {
                    std::cout << '\n' << '\n';
                    break;
                } else if (ch == '\b') {
                    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
                    if (!input.empty() && csbi.dwCursorPosition.X > cursorStartX) {
                        if ((unsigned char)input.back() >= 0x80) {
                            if (csbi.dwCursorPosition.X >= cursorStartX + 2) {
                                input.pop_back();
                                if (!input.empty()) input.pop_back();
                                std::cout << "\b\b  \b\b";
                            }
                        } else {
                            input.pop_back();
                            std::cout << "\b \b";
                        }
                    }
                    continue;
                }
                input += ch;
                std::cout << ch;
            }
        }

        SetConsoleMode(hStdin, mode);

        if (input == "quit") break;

        input += "\n";
        if (!WriteFile(hChildStdInWrite, input.c_str(), (DWORD)input.length(), &bytesWritten, NULL)) {
            std::cout << "写入失败" << std::endl;
            break;
        }
        FlushFileBuffers(hChildStdInWrite);
    }
}

void LocalModelCaller::processResponse(const std::string& response) {
    // 本地调用中未使用
}

void LocalModelCaller::executePendingCodeBlocks() {
    // 本地调用在TimerThread中执行
}


APIModelCaller::APIModelCaller() : isNewResponse(true) {}
APIModelCaller::~APIModelCaller() {}

bool APIModelCaller::initialize(const json& configJson) {
    try {
        api_base = configJson[3]["API_BASE"];
        api_key = configJson[3]["API_KEY"];
        model_name = configJson[1]["model_name"];
        system_role = configJson[0]["system"];
        parameters = configJson[2]["parameters"];

        messages.push_back({
            {"role", "system"},
            {"content", system_role}
        });
    } catch (json::exception& e) {
        std::cerr << "配置文件解析错误: " << e.what() << std::endl;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(lastChunkMutex);
        lastChunkTime = std::chrono::steady_clock::now();
    }
    std::thread timerThread(TimerThread);
    timerThread.detach();

    return true;
}

void APIModelCaller::startInputOutputLoop() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    MarkdownLineProcessor lineProcessor(hConsole);
    gLineProcessor = &lineProcessor;

    while (true) {
        std::string user_input;
        std::cout << "> ";
        std::getline(std::cin, user_input);

        if (user_input.empty()) {
            std::cout << "输入为空，退出程序。" << std::endl;
            break;
        }

        messages.push_back({
            {"role", "user"},
            {"content", user_input}
        });

        doOneRequest();
    }
}

void APIModelCaller::processResponse(const std::string& chunk) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    MarkdownLineProcessor& lineProcessor = *gLineProcessor;

    std::istringstream stream(chunk);
    std::string line;
    while (std::getline(stream, line)) {
        if (line == "data: [DONE]") {
            lineProcessor.processChar('\n');
            std::cout << "\n\n";
            SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);
            isNewResponse = true;
            return;
        }
        if (line.find("data: ") == 0) {
            std::string jsonData = line.substr(6);
            try {
                json response = json::parse(jsonData);
                if (response["choices"][0].contains("delta") && response["choices"][0]["delta"].contains("content")) {
                    std::string messageContent = response["choices"][0]["delta"]["content"];

                    {
                        std::lock_guard<std::mutex> lock(lastChunkMutex);
                        lastChunkTime = std::chrono::steady_clock::now();
                    }

                    if (!modelLoaded && !messageContent.empty()) {
                        modelLoaded = true;
                    }

                    if (isNewResponse) {
                        std::cout << "\n";
                        isNewResponse = false;
                    }

                    size_t pos = 0;
                    while (pos < messageContent.size()) {
                        char ch = messageContent[pos];
                        if (codeBlockElement.isActive()) {
                            codeBlockElement.process(ch, pos, messageContent, hConsole, *(new WORD(DEFAULT_COLOR)));
                        } else {
                            size_t oldPos = pos;
                            if (codeBlockElement.process(ch, pos, messageContent, hConsole, *(new WORD(DEFAULT_COLOR)))) {
                                continue;
                            } else {
                                lineProcessor.processChar(ch);
                                pos++;
                            }
                        }
                    }
                }
            } catch (json::exception&) {
            }
        }
    }
}

void APIModelCaller::executePendingCodeBlocks() {
    if (pendingCodeBlocks.empty()) {
        return;
    }

    std::vector<CodeBlock> blocksToExecute;
    {
        std::lock_guard<std::mutex> lock(codeBlocksMutex);
        blocksToExecute.swap(pendingCodeBlocks);
    }

    std::string lastResultMessage;
    bool lastSuccess = false;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    for (auto& blk : blocksToExecute) {
        std::string resultMessage;
        bool success = ExecuteCode(blk, resultMessage);

        if (success) {
            SetConsoleTextAttribute(hConsole, GREEN_COLOR);
        } else {
            SetConsoleTextAttribute(hConsole, RED_COLOR);
        }
        std::cout << resultMessage << std::endl;
        SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);

        lastResultMessage = resultMessage;
        lastSuccess = success;
    }

    if (!lastResultMessage.empty()) {
        messages.push_back({
            {"role", "user"},
            {"content", lastResultMessage}
        });
        doOneRequest();
    }
}

void APIModelCaller::doOneRequest() {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "无法初始化CURL" << std::endl;
        return;
    }

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    struct curl_slist* headers = NULL;
    std::string auth_header = "Authorization: Bearer " + api_key;
    headers = curl_slist_append(headers, auth_header.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    json request_body = {
        {"model", model_name},
        {"messages", messages},
        {"stream", true}
    };

    for (auto& el : parameters.items()) {
        request_body[el.key()] = el.value();
    }

    std::string postData = request_body.dump();
    curl_easy_setopt(curl, CURLOPT_URL, api_base.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        std::cerr << "请求失败: " << curl_easy_strerror(res) << std::endl;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}

size_t StreamCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    APIModelCaller* caller = reinterpret_cast<APIModelCaller*>(userdata);
    size_t totalSize = size * nmemb;
    std::string chunk(ptr, totalSize);
    caller->processResponse(chunk);
    return totalSize;
}
