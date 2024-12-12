#include "timer_thread.h"
#include "global.h"
#include "model_caller.h"
#include "code_executor.h"

void TimerThread() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::chrono::steady_clock::time_point lastTime;
        {
            std::lock_guard<std::mutex> lock(lastChunkMutex);
            lastTime = lastChunkTime;
        }
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime).count();

        if (duration >= 1000) {
            if (modelLoaded && !isExecuting) {
                std::vector<CodeBlock> blocksToExecute;
                {
                    std::lock_guard<std::mutex> lock(codeBlocksMutex);
                    if (pendingCodeBlocks.empty()) {
                        continue;
                    }
                    isExecuting = true;
                    blocksToExecute.swap(pendingCodeBlocks);
                }

                ModelCaller* caller = nullptr;
                if (dynamic_cast<APIModelCaller*>(modelCaller.get())) {
                    caller = modelCaller.get();
                }

                std::thread execThread([blocksToExecute, caller]() {
                    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
                    std::string lastResultMessage;
                    bool lastSuccess = false;

                    for (auto& blk : blocksToExecute) {
                        std::string resultMessage;
                        bool success = ExecuteCode(blk, resultMessage);

                        if (success) {
                            SetConsoleTextAttribute(hConsole, GREEN_COLOR);
                        } else {
                            SetConsoleTextAttribute(hConsole, RED_COLOR);
                        }
                        std::cout << resultMessage;
                        SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);

                        lastResultMessage = resultMessage;
                        lastSuccess = success;
                    }

                    if (!lastResultMessage.empty()) {
                        if (caller) {
                            APIModelCaller* apiCaller = static_cast<APIModelCaller*>(caller);
                            apiCaller->messages.push_back({
                                {"role", "user"},
                                {"content", lastResultMessage}
                            });
                            apiCaller->doOneRequest();
                            std::cout << "> " << std::flush;
                        } else {
                            std::string messageToModel = lastResultMessage;
                            WriteFile(hChildStdInWrite, messageToModel.c_str(), 
                                      (DWORD)messageToModel.length(), NULL, NULL);
                        }
                    }

                    isExecuting = false;
                });
                execThread.detach();
            }
        }
    }
}
