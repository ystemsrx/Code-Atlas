#include "global.h"
#include "code_executor.h"
#include "model_caller.h"

// 全局变量定义
std::unique_ptr<ModelCaller> modelCaller;
HANDLE hChildStdInWrite = NULL;

std::atomic<bool> modelLoaded(false);
std::vector<CodeBlock> pendingCodeBlocks;
std::atomic<bool> isExecuting(false);
std::mutex codeBlocksMutex;
std::chrono::steady_clock::time_point lastChunkTime;
std::mutex lastChunkMutex;
MarkdownLineProcessor* gLineProcessor = nullptr;

// 控制台颜色常量定义
const WORD DEFAULT_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;  
const WORD CODE_COLOR = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;  
const WORD YELLOW_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;  
const WORD HEADING1_COLOR = FOREGROUND_RED | FOREGROUND_INTENSITY;  
const WORD HEADING2_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;  
const WORD HEADING3_COLOR = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;  
const WORD HEADING4_COLOR = FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;  

const WORD GREEN_COLOR = FOREGROUND_GREEN | FOREGROUND_INTENSITY;  
const WORD RED_COLOR = FOREGROUND_RED | FOREGROUND_INTENSITY;  
const WORD ITALIC_COLOR = FOREGROUND_BLUE | FOREGROUND_INTENSITY;  
const WORD BOLD_COLOR = FOREGROUND_RED | FOREGROUND_INTENSITY;  
const WORD HEADING_COLORS[4] = {HEADING1_COLOR, HEADING2_COLOR, HEADING3_COLOR, HEADING4_COLOR};

// 源代码语言到颜色的映射表
std::unordered_map<std::string, WORD> languageColorMap = {
    {"python", CODE_COLOR},
    {"py", CODE_COLOR},
    {"batch", CODE_COLOR},
    {"bat", CODE_COLOR},
    {"sh", CODE_COLOR},
    {"shell", CODE_COLOR},
    {"bash", CODE_COLOR},
    {"cmd", CODE_COLOR},
    {"powershell", CODE_COLOR},
    {"ps1", CODE_COLOR},
};
