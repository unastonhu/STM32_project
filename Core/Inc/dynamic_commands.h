#ifndef DYNAMIC_COMMANDS_H
#define DYNAMIC_COMMANDS_H

#include <stdbool.h>

/*
 * 解析第五步新增的样本/原型/导出命令。
 * 返回 true 表示命令已被本模块处理，旧 USB 解析器不应继续匹配。
 */
bool DynamicCommands_Handle(const char *json);

#endif /* DYNAMIC_COMMANDS_H */
