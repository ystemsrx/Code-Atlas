#ifndef GLOBAL_H
#define GLOBAL_H

#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <algorithm>
#include <unordered_map>
#include <fstream>
#include <vector>
#include <mutex>
#include <chrono>
#include <sstream>
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <random>
#include <memory>

// 使用nlohmann::json进行JSON操作
using json = nlohmann::json;

// 前置声明
class ModelCaller;
class MarkdownLineProcessor;
struct CodeBlock;

// 全局变量声明
extern std::unique_ptr<ModelCaller> modelCaller;

extern HANDLE hChildStdInWrite;

extern std::atomic<bool> modelLoaded;
extern std::vector<CodeBlock> pendingCodeBlocks;
extern std::atomic<bool> isExecuting;
extern std::mutex codeBlocksMutex;
extern std::chrono::steady_clock::time_point lastChunkTime;
extern std::mutex lastChunkMutex;
extern MarkdownLineProcessor* gLineProcessor;

// 控制台颜色常量定义
extern const WORD DEFAULT_COLOR;  
extern const WORD CODE_COLOR;  
extern const WORD YELLOW_COLOR;  
extern const WORD HEADING1_COLOR;  
extern const WORD HEADING2_COLOR;  
extern const WORD HEADING3_COLOR;  
extern const WORD HEADING4_COLOR;  

extern const WORD GREEN_COLOR;  
extern const WORD RED_COLOR;  
extern const WORD ITALIC_COLOR;  
extern const WORD BOLD_COLOR;  
extern const WORD HEADING_COLORS[4];

// 源代码语言到颜色的映射表
extern std::unordered_map<std::string, WORD> languageColorMap;

#endif
