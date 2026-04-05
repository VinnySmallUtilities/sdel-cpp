#include <functional>
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
#include <chrono>
#include <mutex>

#define _(String) gettext(String)
#define L(String) std::string(gettext(String))

// #define DEBUG

// ПРОВЕРИТЬ, что символьные ссылки правильно обойдены
// TODO: !!!

namespace fs = std::filesystem;
using std::endl;
using std::cout;

// Тип для массива байтов
using ByteArray = std::vector<uint8_t>;
// Тип для списка массивов байтов
using ByteArrayList = std::vector<ByteArray>;


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
    info.size         = 0;
    info.block_size   = 0;

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
    bool                  is_service_dir;
};


// Вспомогательная функция для удаления пробелов и запятых из строки
std::string removeSeparators(const std::string& input)
{
    std::string result;
    std::copy_if
    (
        input.begin(), input.end(), std::back_inserter(result),
        [](char c) { return c != ' ' && c != ','; }
    );

    return result;
}

uint8_t hexToByte(const char* hex)
{
    uint8_t value = 0;
    for (int i = 0; i < 2; ++i)
    {
        value = value << 4;
        char c = hex[i];
        if (c >= '0' && c <= '9')
        {
            value += c - '0';
        }
        else if (c >= 'A' && c <= 'F')
        {
            value += c - 'A' + 10;
        }
        else if (c >= 'a' && c <= 'f')
        {
            value += c - 'a' + 10;
        }
    }

    return value;
}


ByteArrayList convertHexStringsToBytes(const std::vector<std::string>& temp_pattern)
{
    ByteArrayList result;

    for (const auto& str : temp_pattern)
    {
        ByteArray byteArray;
        // Работа со случайно сгенерированными данными: оставляем массив байтов не инициализированным.
        if (str == "TT" || str == "T")
        {
            result.push_back(byteArray);
            continue;
        }

        // Удаляем пробелы и запятые
        std::string cleaned = removeSeparators(str);

        // Проверяем, что длина строки чётная (каждому байту нужны 2 шестнадцатеричных символа)
        if (cleaned.length() % 2 != 0)
        {
            // В случае нечётной длины можно либо выбросить исключение, либо дополнить строку нулём
            cleaned += '0'; // Дополняем нулём справа
            cout << _("Warning") << ": " << _("The number of digits in the pattern is odd") << "." << endl;
        }

        
        // Преобразуем каждую пару символов в байт
        for (size_t i = 0; i < cleaned.length(); i += 2)
        {
            byteArray.push_back(hexToByte(cleaned.c_str() + i));
        }

        result.push_back(std::move(byteArray));
    }

    return result;
}

// Класс для парсинга аргументов
class ArgumentParser
{
private:
    std::vector<std::string>   settings;
    std::vector<std::string>   files_and_dirs;

public:
    std::vector<std::string>   temp_pattern;
    ByteArrayList              temp_bytes;
    std::vector<std::string>   tempd_dirs;

    fs::path sdel_exe_dir_path;
    DirInfo  tempd_files;
    DirInfo  serviceDir;

    bool verbose           = false;
    bool very_verbose      = false;
    bool show_progress     = false;
    bool disk_pause        = false;
    bool create_large_file = false;
    bool create_subdirs    = false;
    bool no_overwrite      = false;
    bool no_delete_dirs    = false;
    bool no_delete_files   = false;
    bool byone             = false;
    bool dry               = false;

    bool isCreationMode()
    {
        return create_large_file || create_subdirs;
    }
    
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
            else if (arg == "byone")
            {
                byone = true;
            }
            else if (arg == "dry")
            {
                dry = true;
            }
            else if (arg == "temp")
            {
                if (i + 1 < argc)
                {
                    temp_pattern.push_back(argv[++i]);
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
                temp_pattern.push_back("00");
            }
            else if (arg == "z1")
            {
                temp_pattern.push_back("55AA");
                temp_pattern.push_back("00");
            }
            else if (arg == "z2")
            {
                temp_pattern.push_back("CC");
                temp_pattern.push_back("66");
                temp_pattern.push_back("00");
            }
            else if (arg == "z3")
            {
                temp_pattern.push_back("AA");
                temp_pattern.push_back("CC");
                temp_pattern.push_back("66");
                temp_pattern.push_back("00");
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
    const std::vector<std::string>& getTempDDirs()     const { return tempd_dirs; }
};

FileInfo AddFileToDir(DirInfo& dir, const FileInfo& file, bool AvoidSymLinks)
{
    #ifdef DEBUG
    cout << "regular" << endl;
    cout << file.fullName << endl;
    #endif

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

FileInfo AddFileToDir(DirInfo& dir, fs::path path, bool AvoidSymLinks)
{
    FileInfo file = getFileInfo(path);

    return AddFileToDir(dir, file, AvoidSymLinks);
}

DirInfo collectFileSystemInfo(const std::string& path, bool AvoidSymLinks);

void AddDirToDir(DirInfo& dir, fs::path path, bool AvoidSymLinks)
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
    dir.is_service_dir = false;

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
            std::cerr << _("For") << " " << entry.path() << std:endl;
            std::cerr << L("Warning") + ": " << ex.what() << std::endl << std::endl;
        }
    }

    return dir;
}

DirInfo extractAllFiles(const std::vector<DirInfo>& dirs)
{
    DirInfo result;
    result.total_size = 0;
    result.file_count = 0;

    std::function<void(const DirInfo&)> traverse = [&](const DirInfo& dir)
    {
        // Добавляем все файлы текущей директории
        for (const auto& file : dir.files)
        {
            AddFileToDir(result, file, false);
        }

        // Рекурсивно обходим поддиректории
        for (const auto& subdir : dir.subdirs)
        {
            traverse(subdir);
        }
    };

    // Запускаем обход для каждой корневой директории
    for (const auto& dir : dirs)
    {
        traverse(dir);
    }

    return result;
}

std::mutex localtime_mutex;
std::string getTimeString(std::time_t& now_c)
{
    std::lock_guard<std::mutex> lock(localtime_mutex);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_c), "%H:%M:%S");
    return ss.str();
}

std::string getTimeNowString()
{
    std::time_t now_c = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    return getTimeString(now_c);
}

int main(int argc, char* argv[])
{
    auto start = std::chrono::steady_clock::now();

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

    if (parser.verbose)
    {
        std::cout << _("Vinogradov S.V. vinny-sdel") << std::endl;
        cout << getTimeNowString() << endl;
    }

    if (parser.verbose)
    {
        if (parser.very_verbose)
        {
            if (!parser.isCreationMode())
            {
                cout << _("Begin to crawl the directories to collect information about the files being deleted") << "." << endl;
            }
        }

        std::cout << std::endl;
    }

    // Вспомогательная директория для того, чтобы добавлять в неё файлы
    parser.serviceDir.name       = "/nonexistent";
    parser.serviceDir.file_count = 0;
    parser.serviceDir.total_size = 0;
    parser.serviceDir.is_symlink = false;
    parser.serviceDir.is_service_dir = true;

    for (const auto& path : parser.getFilesAndDirs())
    {
        try
        {
            if (fs::exists(path))
            {
                if (parser.isCreationMode())
                {
                    if (parser.very_verbose)
                    {
                        std::cerr << _("Warning") << ": " << _("The path exist");
                        std::cerr << ": " << path << std::endl;
                        continue;
                    }
                }
            }
            else
            {
                if (!parser.isCreationMode())
                {
                    std::cerr << _("Warning") << ": " << _("Path does not exist");
                    std::cerr << ": " << path << std::endl;
                    continue;
                }                
            }

            auto Path = fs::absolute(path).lexically_normal();
            if (fs::is_symlink(Path))
            {
                AddFileToDir(parser.serviceDir, Path, false);
            }

            Path = fs::canonical(Path);
            // Если это файл, в т.ч. символическая ссылка
            if (fs::is_regular_file(Path))
            {
                AddFileToDir(parser.serviceDir, Path, true);
            }
            else
            // Если это директория, в т.ч. символическая ссылка
            if (fs::is_directory(Path))
            {
                AddDirToDir(parser.serviceDir, Path, true);
            }
        }
        catch (const fs::filesystem_error& ex)
        {
            std::cerr << L("Warning") + ": " << ex.what() << std::endl;
        }
    }

    // Сбор информации для tempd директорий (с обходом символических ссылок)
    #ifdef DEBUG
    cout << "tempd parsing" << endl;
    #endif
    

    if (!parser.getTempDDirs().empty())
    if (parser.verbose)
    {
        if (parser.very_verbose)
        {
            if (!parser.isCreationMode())
            {
                cout << _("Begin to crawl the directories to collect information about the files for tempd template") << "." << endl;
            }
        }

        std::cout << std::endl;
    }

    // Обработка каталогов для tempd
    std::vector<DirInfo> tempd_tree;
    for (const auto& tempd_path : parser.getTempDDirs())
    {
        if (!fs::exists(tempd_path))
        {
            std::cerr << "Warning: Temp directory does not exist: " << tempd_path << std::endl;
            continue;
        }

        DirInfo tempd_info = collectFileSystemInfo(tempd_path, false);
        tempd_tree.push_back(tempd_info);

        if (parser.very_verbose)
        {
            std::cout << "Collected tempd info for: " << tempd_path
                      << " (files: " << tempd_info.file_count
                      << ", total size: " << tempd_info.total_size << " bytes)" << std::endl;
        }
    }

    parser.tempd_files = extractAllFiles(tempd_tree);
    if (parser.verbose)
    {
        if (parser.serviceDir.total_size > 0 || parser.serviceDir.file_count > 0)
        {
            cout << _("Files prepared for deletion") << ":" << parser.tempd_files.file_count << ", size " << parser.tempd_files.total_size << endl;
        }

        if (parser.tempd_files.total_size > 0)
        {
            cout << _("Files prepared for tempd") << ":" << parser.tempd_files.file_count << ", size " << parser.tempd_files.total_size << endl;
        }
    }

    if (parser.temp_pattern.empty())
    {
        parser.temp_pattern.push_back("00");
    }

    parser.temp_bytes = convertHexStringsToBytes(parser.temp_pattern);


    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> duration = end - start;

    if (parser.verbose)
    {
        std::cout << getTimeNowString() << endl;
        std::cout << "The program is completed in" << " " << duration.count() << " s" << std::endl;
    }

    return 0;
}

