#pragma once

#include "BaseView.h"
#include "../Database.h"
#include "../domain/RegistryImport.h"
#include "../domain/XlsxReader.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Состояние фонового чтения справочника: большие файлы ЕСКЛП читаются в отдельном
// потоке, чтобы интерфейс оставался живым и показывал прогресс.
struct RegistryScanState {
    ScanProgress progress;
    std::atomic<bool> done{false};

    bool ok = false;
    std::string error;
    RegistryProfile profile;
    std::vector<XlsxSheet> sheets;
    std::string sheetName;
    int linked = 0;
    bool streamed = false;
};

class RegistryImportView : public BaseView {
public:
    RegistryImportView();
    ~RegistryImportView() override;

    void Render() override;
    void RefreshData() override;

private:
    void StartScan();
    void FinishScan();
    void RecomputeMatches();

    void StartImport();
    void StepImport();

    std::string path;
    std::string sheetName;
    std::string linkPath;
    int linked = 0;
    std::vector<XlsxSheet> sheets;
    RegistryProfile profile;
    std::vector<ControlFilter> filters;
    std::vector<std::vector<int>> matches;
    std::vector<RegistrySource> sources;
    std::vector<ConflictRow> conflicts;
    std::string asOfDate;
    std::string note;
    int previewLimit = 20;
    bool parsed = false;
    bool streamed = false;
    std::string lastError;
    std::string lastMessage;

    std::shared_ptr<RegistryScanState> scan;
    std::thread scanThread;
    std::chrono::steady_clock::time_point scanStart;

    bool importing = false;
    std::vector<size_t> importQueue;
    size_t importIndex = 0;
    int imported = 0;
    int totalAdded = 0;
    int totalConflicts = 0;
};
