#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <unordered_map>
#include <optional>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <locale>
#include <locale.h>
#include <libintl.h>

#define _(String) gettext(String)
#define L(String) std::string(gettext(String))

#define DEBUG

// ПРОВЕРИТЬ, что символьные ссылки правильно обойдены
// TODO: !!!

namespace fs = std::filesystem;
using std::endl;
using std::cout;

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
            if (access(path.c_str(), R_OK | X_OK) == 0)
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
        
        cout << e.what() << endl;
        return DirCheckResult::OTHER_ERROR;
    }
    catch (const std::exception& e)
    {
        cout << e.what() << endl;
        return DirCheckResult::OTHER_ERROR;
    }
}

bool isDirectory(const std::string& path)
{
    #ifdef DEBUG
    cout << "checkIfDirectory " << int(checkIfDirectory(path)) << endl;
    #endif

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
    std::string fullName;
    fs::path    path;
    uintmax_t   size;
    uintmax_t   block_size;
    bool        is_symlink;
};

FileInfo getFileInfo(const fs::path& path)
{
    FileInfo info;
    struct stat st;

    auto pc = fs::absolute(path).lexically_normal();
    
    info.name         = pc.filename();
    info.path         = pc;
    info.fullName     = pc.string();
    info.is_symlink   = fs::is_symlink(pc);

    if (stat(pc.c_str(), &st) == 0)
    {
        info.size       = st.st_size;
        info.block_size = st.st_blksize; // размер блока ФС
    }

    return info;
}

// Структура для описания директории
struct DirInfo
{
    std::string           name;
    std::vector<FileInfo> files;
    std::vector<DirInfo>  subdirs;
    size_t                file_count;
    uintmax_t             total_size;
    bool                  is_symlink;
};

// Класс для парсинга аргументов
class ArgumentParser
{
private:
    std::vector<std::string>   settings;
    std::vector<std::string>   files_and_dirs;
    std::optional<std::string> temp_pattern;
    std::vector<std::string>   tempd_dirs;
    
public:
    fs::path sdel_exe_dir_path;

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
        sdel_exe_dir_path = fs::canonical(fs::absolute(argv[0])).parent_path();

        if (argc < 3)
        {
            std::cerr << L("Error: Not enough arguments") + ". " + _("Usage") + ": " << endl << fs::canonical(fs::absolute(argv[0])) << " [settings] -- files/dirs" << endl;
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
                    incorrect = true;
                }
            }
            else if (arg == "tempd")
            {
                if (i + 1 < argc)
                {
                    std::string dirPath = fs::canonical(fs::absolute(argv[++i])).string();
                    if (!isDirectory(dirPath))
                    {
                        std::cerr << "Error: tempd argument requires a directory (not an existing dir with right persmissions). " << dirPath << std::endl;
                        incorrect = true;
                    }
                    else
                    {
                        tempd_dirs.push_back(dirPath);
                    }
                }
                else
                {
                    std::cerr << "Error: tempd argument requires a directory (not enough params)" << std::endl;
                    incorrect = true;
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
            {
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
        if (i >= argc)
        {
            std::cerr << "Error: No files or directories specified after '--'" << std::endl;
            return false;
        }

        // Собираем файлы и директории
        while (i < argc)
        {
            files_and_dirs.push_back(argv[i]);
            ++i;
        }

        // Проверка для cr, d, nz
        if ((create_large_file || create_subdirs || no_overwrite) && files_and_dirs.size() != 1)
        {
            std::cerr << "Error: When using cr, d or nz, exactly one directory must be specified" << std::endl;
            incorrect = true;
        }

        if (no_overwrite)
        if (!create_large_file)
        {
            std::cerr << "Error: When using nz, cr must be specified" << std::endl;
            incorrect = true;
        }

        return !incorrect;
    }

    // Геттеры для доступа к распарсенным данным
    const std::vector<std::string>& getSettings()      const { return settings; }
    const std::vector<std::string>& getFilesAndDirs()  const { return files_and_dirs; }
    const std::optional<std::string>& getTempPattern() const { return temp_pattern; }
    const std::vector<std::string>& getTempDDirs()     const { return tempd_dirs; }
};


FileInfo AddFileToDir(DirInfo dir, fs::path path, bool AvoidSymLinks)
{
    #ifdef DEBUG
    cout << "regular" << endl;
    cout << path.string() << endl;
    #endif

    FileInfo file = getFileInfo(path);
    if (AvoidSymLinks && file.is_symlink)
        return file;

    dir.files.push_back(file);
    dir.file_count++;

    if (!file.is_symlink)
    dir.total_size += file.size;

    #ifdef DEBUG
    cout << "added" << " " << file.size << endl;
    #endif
    
    return file;
}

DirInfo collectFileSystemInfo(const std::string& path, bool AvoidSymLinks);

void AddDirToDir(DirInfo dir, fs::path path, bool AvoidSymLinks)
{
    #ifdef DEBUG
    cout << "dir" << endl;
    cout << path.string() << endl;
    cout << (fs::is_symlink(path) ? "link" : "notlink") << endl;
    // cout << fs::absolute(path).lexically_normal().string() << endl;
    #endif

    DirInfo subdir = collectFileSystemInfo(path, AvoidSymLinks);
    if (AvoidSymLinks && subdir.is_symlink)
    {
        return;
    }

    dir.subdirs.push_back(subdir);
    dir.file_count += subdir.file_count;
    dir.total_size += subdir.total_size;
}

// Функция сбора информации о файловой системе
DirInfo collectFileSystemInfo(const std::string& path, bool AvoidSymLinks)
{
    DirInfo dir;
    dir.name       = fs::absolute(path).lexically_normal();
    dir.file_count = 0;
    dir.total_size = 0;
    dir.is_symlink = fs::is_symlink(dir.name);

    if (dir.is_symlink)
    {
        if (AvoidSymLinks)
        {
            // Игнорируем symlink на папку
            return dir;
        }
        else
        {
            // Разрешаем symlink в реальный путь
            dir.name = fs::canonical(fs::absolute(path));

            #ifdef DEBUG
            cout << "to " << dir.name << endl;
            #endif
        }
    }
    
    for (auto& entry : fs::directory_iterator(dir.name))
    {
        try
        {
            // Если это файл, в т.ч. символическая ссылка
            if (fs::is_regular_file(entry.status()))
            {
                AddFileToDir(dir, entry.path(), AvoidSymLinks);
            }
            // Если это директория, в т.ч. символическая ссылка
            else if (fs::is_directory(entry.status()))
            {
                AddDirToDir(dir, entry.path(), AvoidSymLinks);
            }
            else
            {
                #ifdef DEBUG
                cout << "non regular" << endl;
                cout << entry.path().string() << endl;
                #endif

                // Пропускаем всё, кроме symlink
                if (!fs::is_symlink(entry.path()))
                {
                    cout << "skipped" << endl;
                    continue;
                }

                // Добавляем файл symlink
                auto file = AddFileToDir(dir, entry.path(), false);

                auto status = fs::status(file.fullName);
                if (status.type() == fs::file_type::not_found)
                {
                    if (!AvoidSymLinks)
                    {
                        std::cerr << L("Warning") + ": " << L("File not found for link") + " " + file.fullName << std::endl;
                    }
                    continue;
                }
                
                if (AvoidSymLinks)
                    continue;
                
                fs::path real_path = fs::canonical(entry.path());
                if (fs::is_directory(real_path))
                    AddDirToDir(dir, real_path, AvoidSymLinks);

                if (fs::is_regular_file(real_path))
                    AddFileToDir(dir, real_path, AvoidSymLinks);
            }
        }
        catch (const fs::filesystem_error& ex)
        {
            std::cerr << L("Warning") + ": " << ex.what() << std::endl;
        }
    }

    return dir;
}

int main(int argc, char* argv[])
{
    // Устанавливаем локаль и получаем локаль
    setlocale(LC_ALL, "");
    std::string locale = setlocale(LC_MESSAGES, "");

    // /usr/share/locale/<locale>/LC_MESSAGES/vinny-sdel.mo
    if (fs::exists("./locale/" ))
        bindtextdomain("vinny-sdel", "./locale");
    else
        bindtextdomain("vinny-sdel", "/usr/share/locale");

    textdomain("vinny-sdel");
    
//    std::cout << locale << std::endl;
//    std::cout << _("LOCALE") << std::endl;

    ArgumentParser parser;
    if (!parser.parse(argc, argv))
    {
        if (parser.verbose)
            std::cout << _("Vinogradov S.V. vinny-sdel") << std::endl;

        return 1;
    }

    // Обработка verbose-режимов
    if (parser.verbose)
    {
        if (parser.very_verbose)
        {
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
            DirInfo dir_info = collectFileSystemInfo(path, true);
            main_tree.push_back(dir_info);
            if (parser.verbose) {
                std::cout << "Collected info for directory: " << path
                          << " (files: " << dir_info.file_count
                          << ", total size: " << dir_info.total_size << " bytes)" << std::endl;
            }
        } else if (fs::is_regular_file(path)) {
            FileInfo file_info = getFileInfo(fs::path(path).filename().string());

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
    #ifdef DEBUG
    cout << "tempd parsing" << endl;
    #endif

    std::vector<DirInfo> tempd_tree;
    for (const auto& tempd_path : parser.getTempDDirs()) {
        if (!fs::exists(tempd_path)) {
            std::cerr << "Warning: Temp directory does not exist: " << tempd_path << std::endl;
            continue;
        }

        DirInfo tempd_info = collectFileSystemInfo(tempd_path, false);
        tempd_tree.push_back(tempd_info);

        if (parser.verbose) {
            std::cout << "Collected tempd info for: " << tempd_path
                      << " (files: " << tempd_info.file_count
                      << ", total size: " << tempd_info.total_size << " bytes)" << std::endl;
        }
    }

    // Дополнительная обработка в зависимости от настроек
    if (parser.show_progress) {
        std::cout << "Progress calculation enabled" << std::endl;
        // Здесь будет логика расчёта прогресса
    }

    if (parser.getTempPattern().has_value()) {
        std::cout << "Using overwrite pattern: " << parser.getTempPattern().value() << std::endl;
        // Здесь будет логика применения шаблона перезатирания
    }

    // Вывод итоговой информации при very verbose
    if (parser.very_verbose) {
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
