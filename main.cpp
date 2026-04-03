#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <unordered_map>
#include <optional>
#include <locale>
#include <fstream>
#include <sstream>

// ПРОВЕРИТЬ, что символьные ссылки правильно обойдены
// TODO: !!!

namespace fs = std::filesystem;

enum class DirCheckResult
{
    IS_DIRECTORY,
    NOT_DIRECTORY,
    PATH_DOES_NOT_EXIST,
    ACCESS_DENIED,
    OTHER_ERROR
};

DirCheckResult checkIfDirectory(const std::string& path)
{
    if (path.empty())
    {
        return DirCheckResult::OTHER_ERROR;
    }

    try
    {
        fs::path p(path);

        if (!fs::exists(p))
        {
            return DirCheckResult::PATH_DOES_NOT_EXIST;
        }

        if (fs::is_directory(p))
        {
            if (access(path.c_str(), R_OK | X_OK) != 0)
                return DirCheckResult::IS_DIRECTORY;
            else
                return DirCheckResult::ACCESS_DENIED;
        }
        else
        {
            return DirCheckResult::NOT_DIRECTORY;
        }
    }
    catch (const fs::filesystem_error& e)
    {
        if (e.code() == std::errc::permission_denied)
        {
            return DirCheckResult::ACCESS_DENIED;
        }
        return DirCheckResult::OTHER_ERROR;
    }
    catch (...)
    {
        return DirCheckResult::OTHER_ERROR;
    }
}

bool isDirectory(const std::string& path)
{
    return checkIfDirectory(path) == DirCheckResult::IS_DIRECTORY;
}

bool isDirectoryOrNotExists(const std::string& path)
{
    return checkIfDirectory(path) == DirCheckResult::IS_DIRECTORY || checkIfDirectory(path) == DirCheckResult::PATH_DOES_NOT_EXIST;
}

// Структура для описания файла
struct FileInfo
{
    std::string name;
    uintmax_t   size;
    uintmax_t   block_size;
    bool        is_directory;
};

// Структура для описания директории
struct DirInfo
{
    std::string           name;
    std::vector<FileInfo> files;
    std::vector<DirInfo>  subdirs;
    size_t                file_count;
    uintmax_t             total_size;
};

// Класс для парсинга аргументов
class ArgumentParser
{
private:
    std::vector<std::string>   settings;
    std::vector<std::string>   files_and_dirs;
    std::optional<std::string> temp_pattern;
    std::vector<std::string>   tempd_dirs;
    bool verbose           = false;
    bool very_verbose      = false;
    bool show_progress     = false;
    bool zero_pass         = false;
    bool one_pass          = false;
    bool two_pass          = false;
    bool three_pass        = false;
    bool disk_pause        = false;
    bool create_large_file = false;
    bool create_subdirs    = false;
    bool no_overwrite      = false;
    bool no_delete_dirs    = false;
    bool no_delete_files   = false;

public:
    bool parse(int argc, char* argv[])
    {
        if (argc < 3) {
            std::cerr << "Error: Not enough arguments. Usage: " << argv[0] << " [settings] -- files/dirs" << std::endl;
            return false;
        }

        int  i = 1;
        bool incorrect = false;
        // Парсим аргументы-настройки до разделителя
        while (i < argc && std::string(argv[i]) != "--")
        {
            std::string arg = argv[i];
            if (arg == "v")
            {
                verbose = true;
            }
            else if (arg == "vv")
            {
                very_verbose = true;
                verbose = true; // vv включает v
            }
            else if (arg == "pr")
            {
                show_progress = true;
            }
            else if (arg == "temp")
            {
                if (i + 1 < argc)
                {
                    temp_pattern = argv[++i];
                }
                else
                {
                    std::cerr << "Error: temp argument requires a hex pattern" << std::endl;
                    return false;
                }
            }
            else if (arg == "tempd")
            {
                if (i + 1 < argc)
                {
                    std::string dirPath = argv[++i];
                    if (!isDirectory(dirPath))
                    {
                        std::cerr << "Error: tempd argument requires a directory (not an existing dir with right persmissions)" << std::endl;
                        return false;
                    }

                    tempd_dirs.push_back(dirPath);
                    ++i; // TODO: !!! Это точно верно????
                }
                else
                {
                    std::cerr << "Error: tempd argument requires a directory (not enough params)" << std::endl;
                    return false;
                }
            }
            else if (arg == "z0")
            {
                zero_pass = true;
            }
            else if (arg == "z1")
            {
                one_pass = true;
            }
            else if (arg == "z2")
            {
                two_pass = true;
            }
            else if (arg == "z3")
            {
                three_pass = true;
            }
            else if (arg == "sl")
            {
                disk_pause = true;
            }
            else if (arg == "cr")
            {
                create_large_file = true;
            }
            else if (arg == "d")
            {
                create_subdirs = true;
            }
            else if (arg == "nz")
            {// TODO: !!! Добавить проверку в конце, т.к. no_overwrite имеет смысл только при cr или d
                no_overwrite = true;
            }
            else if (arg == "nd")
            {
                no_delete_dirs = true;
            }
            else if (arg == "nf")
            {
                no_delete_files = true;
            }
            else
            {
                std::cerr << "ERROR: Unknown setting: " << arg << std::endl;
                incorrect = true;
            }
            
            ++i;
        }   // end while for arguments

        if (i >= argc || std::string(argv[i]) != "--")
        {
            std::cerr << "Error: Missing obligatory separator '--'" << std::endl;
            return false;
        }
        ++i; // пропускаем разделитель

        // Проверяем, что есть хотя бы один файл/директория
        if (i >= argc) {
            std::cerr << "Error: No files or directories specified after '--'" << std::endl;
            return false;
        }

        // Собираем файлы и директории
        while (i < argc) {
            files_and_dirs.push_back(argv[i]);
            ++i;
        }

        // Проверка для cr, d, nz
        if ((create_large_file || create_subdirs || no_overwrite) && files_and_dirs.size() != 1) {
            std::cerr << "Error: When using cr, d or nz, exactly one directory must be specified" << std::endl;
            return false;
        }

        return !incorrect;
    }

    // Геттеры для доступа к распарсенным данным
    const std::vector<std::string>& getSettings() const { return settings; }
    const std::vector<std::string>& getFilesAndDirs() const { return files_and_dirs; }
    const std::optional<std::string>& getTempPattern() const { return temp_pattern; }
    const std::vector<std::string>& getTempDDirs() const { return tempd_dirs; }
    bool isVerbose() const { return verbose; }
    bool isVeryVerbose() const { return very_verbose; }
    bool shouldShowProgress() const { return show_progress; }
    // ... другие геттеры
};

// Класс локализации
class Localization {
private:
    std::unordered_map<std::string, std::string> messages;

public:
    bool load(const std::string& locale) {
        std::string filename = "messages_" + locale + ".txt";
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Warning: Could not load localization file: " << filename << std::endl;
            return false;
        }

        std::string key, value;
        while (std::getline(file, key, '=') && std::getline(file, value)) {
            messages[key] = value;
        }
        return true;
    }

    std::string getMessage(const std::string& key) const {
        auto it = messages.find(key);
        if (it != messages.end()) {
            return it->second;
        }
        return "Unknown message: " + key;
    }
};

// Функция сбора информации о файловой системе
DirInfo collectFileSystemInfo(const std::string& path) {
    DirInfo dir;
    dir.name = path;
    dir.file_count = 0;
    dir.total_size = 0;

    for (const auto& entry : fs::directory_iterator(path)) {
        if (fs::is_regular_file(entry.status())) {
            FileInfo file;
            file.name = entry.path().filename().string();
            file.size = fs::file_size(entry);
            file.block_size = fs::stat_status(entry.symlink_status()).st_blksize;
            file.is_directory = false;
            dir.files.push_back(file);
            dir.file_count++;
            dir.total_size += file.size;
        } else if (fs::is_directory(entry.status())) {
            DirInfo subdir = collectFileSystemInfo(entry.path().string());
            dir.subdirs.push_back(subdir);
            dir.file_count += subdir.file_count;
            dir.total_size += subdir.total_size;
        }
    }
    return dir;
}

int main(int argc, char* argv[]) {
    ArgumentParser parser;
    if (!parser.parse(argc, argv)) {
        return 1;
    }

    Localization loc;
    // Получаем локаль ОС (упрощённо)    // Получаем локаль ОС (упрощённо)
    std::string locale = setlocale(LC_ALL, nullptr);
    if (locale.empty()) {
        locale = "en_US"; // дефолтная локаль
    } else {
        // Оставляем только часть до точки (например, ru_RU.UTF-8 -> ru_RU)
        size_t dot_pos = locale.find('.');
        if (dot_pos != std::string::npos) {
            locale = locale.substr(0, dot_pos);
        }
    }

    loc.load(locale);

    // Обработка verbose-режимов
    if (parser.isVerbose()) {
        std::cout << "=== Program Information ===" << std::endl;
        if (parser.isVeryVerbose()) {
            std::cout << loc.getMessage("verbose_description") << std::endl;
            std::cout << "Temp pattern: "
                      << (parser.getTempPattern().has_value() ? parser.getTempPattern().value() : "not set")
                      << std::endl;
            std::cout << "Temp directories: ";
            for (const auto& dir : parser.getTempDDirs()) {
                std::cout << dir << " ";
            }
            std::cout << std::endl;
        }
        std::cout << "Files and directories to process: ";
        for (const auto& item : parser.getFilesAndDirs()) {
            std::cout << item << " ";
        }
        std::cout << std::endl;
    }

    // Сбор информации о файловой системе для основных файлов/директорий
    std::vector<DirInfo> main_tree;
    for (const auto& path : parser.getFilesAndDirs()) {
        if (!fs::exists(path)) {
            std::cerr << "Warning: Path does not exist: " << path << std::endl;
            continue;
        }

        if (fs::is_directory(path)) {
            DirInfo dir_info = collectFileSystemInfo(path);
            main_tree.push_back(dir_info);
            if (parser.isVerbose()) {
                std::cout << "Collected info for directory: " << path
                          << " (files: " << dir_info.file_count
                          << ", total size: " << dir_info.total_size << " bytes)" << std::endl;
            }
        } else if (fs::is_regular_file(path)) {
            FileInfo file_info;
            file_info.name = fs::path(path).filename().string();
            file_info.size = fs::file_size(path);
            file_info.block_size = fs::status(path).st_blksize;
            file_info.is_directory = false;

            // Создаём временную директорию для одиночного файла
            DirInfo temp_dir;
            temp_dir.name = fs::path(path).parent_path().string();
            temp_dir.files.push_back(file_info);
            temp_dir.file_count = 1;
            temp_dir.total_size = file_info.size;
            main_tree.push_back(temp_dir);
        }
    }

    // Сбор информации для tempd директорий (с обходом символических ссылок)
    std::vector<DirInfo> tempd_tree;
    for (const auto& tempd_path : parser.getTempDDirs()) {
        if (!fs::exists(tempd_path)) {
            std::cerr << "Warning: Temp directory does not exist: " << tempd_path << std::endl;
            continue;
        }

        DirInfo tempd_info = collectFileSystemInfo(tempd_path);
        tempd_tree.push_back(tempd_info);

        if (parser.isVerbose()) {
            std::cout << "Collected tempd info for: " << tempd_path
                      << " (files: " << tempd_info.file_count
                      << ", total size: " << tempd_info.total_size << " bytes)" << std::endl;
        }
    }

    // Дополнительная обработка в зависимости от настроек
    if (parser.shouldShowProgress()) {
        std::cout << "Progress calculation enabled" << std::endl;
        // Здесь будет логика расчёта прогресса
    }

    if (parser.getTempPattern().has_value()) {
        std::cout << "Using overwrite pattern: " << parser.getTempPattern().value() << std::endl;
        // Здесь будет логика применения шаблона перезатирания
    }

    // Вывод итоговой информации при very verbose
    if (parser.isVeryVerbose()) {
        std::cout << "\n=== Detailed Summary ===" << std::endl;
        std::cout << "Main tree contains " << main_tree.size() << " root items" << std::endl;
        std::cout << "Tempd tree contains " << tempd_tree.size() << " items" << std::endl;

        // Выводим структуру основной директории (если есть)
        if (!main_tree.empty()) {
            const auto& root = main_tree[0];
            std::cout << "Root: " << root.name << std::endl;
            std::cout << "  Files: " << root.file_count << std::endl;
            std::cout << "  Total size: " << root.total_size << " bytes" << std::endl;

            if (!root.files.empty()) {
                std::cout << "  First few files:" << std::endl;
                for (size_t i = 0; i < std::min(root.files.size(), size_t(5)); ++i) {
                    const auto& file = root.files[i];
                    std::cout << "    " << file.name << " (" << file.size << " bytes, "
                      << file.block_size << " block size)" << std::endl;
                }
            }
        }
    }

    std::cout << "Program completed successfully" << std::endl;
    return 0;
}
