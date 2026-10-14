#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

#ifndef INSTALL_DATA_DIR
#define INSTALL_DATA_DIR "data"
#endif

inline std::string ExecDir() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return {};
    buf[n] = '\0';
    std::string p(buf);
    auto pos = p.find_last_of('/');
    return pos == std::string::npos ? std::string() : p.substr(0, pos);
}

inline std::string AssetPath(const std::string& file) {
    const std::vector<std::string> candidates = {
        "data/" + file,                              // запуск из каталога проекта/сборки
        ExecDir() + "/data/" + file,                 // data рядом с исполняемым файлом
        ExecDir() + "/" + file,                      // файл прямо рядом с исполняемым файлом
        std::string(INSTALL_DATA_DIR) + "/" + file,  // стандартный каталог установки (Linux)
    };
    for (const auto& path : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) return path;
    }
    return {};
}

inline std::string HomeDir() {
    const char* home = getenv("HOME");
    return home ? std::string(home) : std::string(".");
}

inline std::string AppDataDir() {
    // Позволяет тестам и другим инструментам использовать отдельный каталог,
    // не трогая пользовательские данные (~/.audit105).
    const char* override = getenv("AUDIT105_DATA_DIR");
    if (override && *override) return override;
    return HomeDir() + "/.audit105";
}

inline std::string DefaultDbPath() {
    return AppDataDir() + "/audit105.db";
}
