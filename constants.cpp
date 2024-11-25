#include "constants.h"

// 语言到颜色的映射
std::unordered_map<std::string, WORD> languageColorMap = {
    // Python相关
    {"python", CODE_COLOR},
    {"py", CODE_COLOR},

    // 命令行/脚本相关
    {"batch", CODE_COLOR},
    {"bat", CODE_COLOR},
    {"sh", CODE_COLOR},
    {"shell", CODE_COLOR},
    {"bash", CODE_COLOR},
    {"cmd", CODE_COLOR},
    {"powershell", CODE_COLOR},
    {"ps1", CODE_COLOR},
};
