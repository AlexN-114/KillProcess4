// Show and kill processes
// aN / 2024

#include <windows.h>
#include <windowsx.h>
#include <winbase.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <wow64apiset.h>
#include <stdio.h>
#include <stdlib.h>

#include "KillProcess.h"
#include "Resource.h"

char *pCmdLine;
char *argv[100];
int argc = 0;
SYSTEM_INFO si;
char *szExe = ".exe";
char versStr[50];
char trenner[5] = "|";
TASK_LIST tlist[1024];
TASK_LIST slist[1024];
SHOW show = {7, 0, 25, 40, 30, 5, 8, 5, 3, 5, 9, 4};
int iTopItem = 0;
int iCaretItem = 0;
int iStatusWidths[] = {100, 150, 275, 350, 425, -1};
HWND hwnd_main = NULL;
HWND hwnd_base = NULL;
HWND hwnd_header = NULL;
HWND hwnd_client = NULL;
HWND hwnd_StatusBar = NULL;
HWND hwnd_filt = NULL;
HWND hwnd_show = NULL;
HWND hwnd_disp = NULL;
HWND hwnd_sedit = NULL;
RECT rMain = {0, 0, 0, 0};
UINT hHeader = 0;
HINSTANCE g_hInst;

HMENU hMain     = NULL;
HMENU hContext  = NULL;

int numTasks    = 0;
int listTasks   = 0;
int selected    = 0;
int numSelected = 0;
BOOL ForceKill  = TRUE;
BOOL TopMost    = FALSE;

char filt_name [MAX_PATH];
char filt_pid  [MAX_PATH];
char filt_title[MAX_PATH];

char *sPipe = "\\\\.\\pipe\\KillProcess4";

//---------------------------------------------------------------------------//

char* GetVersionString(char *szModul, char *szVersion)
{
    int vis;
    void *vi;

    WORD *ll,*lh,*hl,*hh;
    unsigned long long x;
    HANDLE hModule;
    char szName[MAX_PATH];
    char *pModule = szModul;

    if (szModul == NULL)
    {
        hModule = GetModuleHandle(NULL);
        GetModuleFileName(hModule, szName, MAX_PATH);
        pModule = szName;
        CloseHandle(hModule);
    }
    vis = GetFileVersionInfoSize (pModule, NULL);
    if (vis)
    {
        vi = malloc (vis);
        if (NULL != vi)
        {
            GetFileVersionInfo (pModule, 0, vis, vi);
            x = (unsigned long long)vi;
            ll = (WORD*)(x + 0x32);
            lh = (WORD*)(x + 0x30);
            hl = (WORD*)(x + 0x36);
            hh = (WORD*)(x + 0x34);
            sprintf (szVersion, "%d.%d.%d.%d", *ll, *lh, *hl, *hh);
            free(vi);
        }
    }
    else
    {
        szVersion[0] = 0;
    }
    return szVersion;
}


HWND GetHScroll(HWND hwnd)
{
    HWND hHScroll = GetWindow(hwnd, GW_CHILD);

    while (hHScroll)
    {
        if (GetWindowLong(hHScroll, GWL_STYLE) &  WS_HSCROLL)
        {
            break;
        }
    }
    return hHScroll;
}

//----------------------------------------------------------------------------------------------------------------
// cpuusage(void)
// ==============
// Return a int value in the range 0 - 100 representing actual CPU usage in percent.
//----------------------------------------------------------------------------------------------------------------

int cpuusage(void)
{
    static SYSTEM_INFO *si = NULL;

    if (NULL == si)
    {
        si = malloc(sizeof(SYSTEM_INFO));
        GetNativeSystemInfo(si);
    }

    FILETIME                ft_sys_idle;
    FILETIME                ft_sys_kernel;
    FILETIME                ft_sys_user;

    FILETIME                ft_fun_time;

    ULARGE_INTEGER          ul_sys_idle;
    static ULARGE_INTEGER   ul_sys_idleold;
    ULARGE_INTEGER          ul_sys_idle_diff;

    ULARGE_INTEGER	     	ul_fun_time;
    static ULARGE_INTEGER   ul_fun_timeold;
    ULARGE_INTEGER          ul_fun_time_diff;

    GetSystemTimes(&ft_sys_idle, &ft_sys_kernel, &ft_sys_user); // This API was introduced in WinXP SP1
    GetSystemTimeAsFileTime(&ft_fun_time);

    ul_sys_idle.LowPart  = ft_sys_idle.dwLowDateTime;
    ul_sys_idle.HighPart = ft_sys_idle.dwHighDateTime;

    ul_fun_time.LowPart  = ft_fun_time.dwLowDateTime;
    ul_fun_time.HighPart = ft_fun_time.dwHighDateTime;

    ul_sys_idle_diff.QuadPart = ul_sys_idle.QuadPart - ul_sys_idleold.QuadPart;
    ul_fun_time_diff.QuadPart = ul_fun_time.QuadPart - ul_fun_timeold.QuadPart;

    ul_sys_idleold.QuadPart   = ul_sys_idle.QuadPart;
    ul_fun_timeold.QuadPart   = ul_fun_time.QuadPart;

    if (!ul_fun_time_diff.QuadPart)
        return 0;

    return 1000 - (((ul_sys_idle_diff.QuadPart * 1000) / si->dwNumberOfProcessors) / ul_fun_time_diff.QuadPart);
}

/* Routine Description:
   Changes the process's privilege so that kill works properly.
   Arguments:
   Return Value:
   TRUE             - success
   FALSE            - failure
*/
BOOL EnableDebugPrivNT( VOID )
{
    HANDLE hToken;
    LUID DebugValue;
    TOKEN_PRIVILEGES tkp;


    //
    // Retrieve a handle of the access token
    //
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                          &hToken))
    {
        fprintf(stderr, "OpenProcessToken failed with %lu\n", GetLastError());
        CloseHandle(hToken);
        return FALSE;
    }

    //
    // Enable the SE_DEBUG_NAME privilege
    //
    if (!LookupPrivilegeValue((LPSTR) NULL,
                              SE_DEBUG_NAME,
                              &DebugValue))
    {
        fprintf(stderr, "LookupPrivilegeValue failed with %lu\n", GetLastError());
        CloseHandle(hToken);
        return FALSE;
    }

    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Luid = DebugValue;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    AdjustTokenPrivileges(hToken,
                          FALSE,
                          &tkp,
                          sizeof(TOKEN_PRIVILEGES),
                          (PTOKEN_PRIVILEGES) NULL,
                          (PDWORD) NULL);

    //
    // The return value of AdjustTokenPrivileges can't be tested
    //
    if (GetLastError() != ERROR_SUCCESS)
    {
        CloseHandle(hToken);
        printf("AdjustTokenPrivileges failed with %lu\n", GetLastError());
        return FALSE;
    }
    else
    {
        char hStr[100];
        GetWindowText(hwnd_main, hStr, sizeof(hStr));
        if (hStr[strlen(hStr) - 1] != '*')
        {
            //TODO
            strcat_s(hStr, sizeof(hStr), "*");
            EnableMenuItem(hMain, CM_FILE_RESTART, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
        }
        SetWindowText(hwnd_main, hStr);
    }
    CloseHandle(hToken);

    return TRUE;
}

void printError( TCHAR const* msg )
{
    DWORD eNum;
    TCHAR sysMsg[256];
    TCHAR* p;
    TCHAR hStr[512];

    eNum = GetLastError( );
    FormatMessage( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, eNum,
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
                   sysMsg, 256, NULL );

    // Trim the end of the line and terminate it with a null
    p = sysMsg;
    while ( ( *p > 31 ) || ( *p == 9 ) )
        ++p;
    do
    {
        *p-- = 0;
    }
    while (( p >= sysMsg) &&
           ( ( *p == '.') || ( *p < 33)));

    // Display the message
// _tprintf( TEXT("\n  WARNING: %s failed with error %d (%s)"), msg, eNum, sysMsg );
    sprintf_s(hStr, sizeof(hStr), TEXT("\n  WARNING: %s failed with error %d (%s)"), msg, eNum, sysMsg );
    MessageBox(NULL, hStr, "Error", MB_ICONERROR);
}

char *strcasestr(const char *haystack, const char *needle)
{
    size_t needle_len = strlen(needle);
    size_t haystack_len = strlen(haystack);
    size_t i;

    if (needle_len > haystack_len)
    {
        return NULL;
    }

    for (i = 0; i <= haystack_len - needle_len; i++)
    {
        if (strncasecmp(haystack + i, needle, needle_len) == 0)
        {
            return (char *) haystack + i;
        }
    }

    return NULL;
}

BOOL is_main_window(HWND handle)
{
    return GetWindow(handle, GW_OWNER) == (HWND)0 && IsWindowVisible(handle);
}

BOOL CALLBACK enum_windows_callback(HWND handle, LPARAM lParam)
{
    handle_data *data = (handle_data*)lParam;
    unsigned long process_id = 0;
    GetWindowThreadProcessId(handle, &process_id);
    if (data->process_id != process_id || !is_main_window(handle))
        return TRUE;
    data->window_handle = handle;
    return FALSE;
}

HWND find_main_window(unsigned long process_id)
{
    handle_data data;
    data.process_id = process_id;
    data.window_handle = 0;
    EnumWindows(enum_windows_callback, (LPARAM)&data);
    return data.window_handle;
}


char *GetFirstModulePathEnum (DWORD dwPID, char *path)
{
    HMODULE hMods[1024] = {0};
    HANDLE hProcess;
    DWORD cbNeeded;

    path[0] = 0;

    hProcess = OpenProcess( PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, dwPID );

    if (NULL == hProcess)
    {
        return path;
    }

    if ( EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded))
    {
        GetModuleFileNameEx( hProcess, hMods[0], path, MAX_PATH);
    }

    // Release the handle to the process.

    CloseHandle( hProcess );

    return path;
}

int PrintModules( DWORD processID )
{
    HMODULE hMods[1024];
    HANDLE hProcess;
    DWORD cbNeeded;
    int i;
    // Print the process identifier.

    printf( "\nProcess ID: %u\n", (UINT)processID );

    // Get a handle to the process.
    hProcess = OpenProcess( PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processID );

    // Get a list of all the modules in this process.

    if ( EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded))
    {
        for ( i = 0; i < (cbNeeded / sizeof(HMODULE)); i++ )
        {
            TCHAR szModName[MAX_PATH];

            // Get the full path to the module's file.

            if ( GetModuleFileNameEx( hProcess, hMods[i], szModName,
                                      sizeof(szModName) / sizeof(TCHAR)))
            {
                // Print the module name and handle value.

                fprintf(stderr, TEXT("\t%s (0x%08llX)\n"), szModName, (size_t)hMods[i] );
            }
        }
    }

    // Release the handle to the process.

    CloseHandle( hProcess );

    return 0;
}

char *GetFirstModulePath (DWORD dwPID, char *path)
{
    HANDLE hModuleSnap = INVALID_HANDLE_VALUE;
    MODULEENTRY32 me32;

    path[0] = 0;

    if (0 == dwPID)
    {
        //TODO
        return path;
    }

    // Take a snapshot of all modules in the specified process.
    hModuleSnap = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE /* | TH32CS_SNAPMODULE32 */, dwPID );
    if ( hModuleSnap != INVALID_HANDLE_VALUE )
    {
        me32.dwSize = sizeof( MODULEENTRY32 );
        if ( Module32First( hModuleSnap, &me32 ) )
        {
            strcpy(path, me32.szExePath);
        }
        GetModuleFileName(hModuleSnap, path, MAX_PATH);
        CloseHandle( hModuleSnap );
    }
    return path;
}

BOOL ListProcessModules( DWORD dwPID, HWND hwnd)
{
    char hStr[200];
    HANDLE hModuleSnap = INVALID_HANDLE_VALUE;
    MODULEENTRY32 me32;

    // Take a snapshot of all modules in the specified process.
    hModuleSnap = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE, dwPID );
    if ( hModuleSnap == INVALID_HANDLE_VALUE )
    {
        printError( TEXT("CreateToolhelp32Snapshot (of modules)") );
        return ( FALSE );
    }

    // Set the size of the structure before using it.
    me32.dwSize = sizeof( MODULEENTRY32 );

    // Retrieve information about the first module,
    // and exit if unsuccessful
    if ( !Module32First( hModuleSnap, &me32 ) )
    {
        printError( TEXT("Module32First") ); // show cause of failure
        CloseHandle( hModuleSnap ); // clean the snapshot object
        return ( FALSE );
    }

    // Now walk the module list of the process,
    // and display information about each module
    do
    {
        sprintf_s(hStr, sizeof(hStr), TEXT("     MODULE NAME:     %s"),           me32.szModule );
        SendMessage(hwnd, LB_ADDSTRING, 0, (LPARAM)hStr);
        sprintf_s(hStr, sizeof(hStr), TEXT("     Executable     = %s"),             me32.szExePath );
        SendMessage(hwnd, LB_ADDSTRING, 0, (LPARAM)hStr);
        sprintf_s(hStr, sizeof(hStr), TEXT("     Process ID     = 0x%08X"),         me32.th32ProcessID );
        SendMessage(hwnd, LB_ADDSTRING, 0, (LPARAM)hStr);
        sprintf_s(hStr, sizeof(hStr), TEXT("     Ref count (g)  = 0x%04X"),         me32.GlblcntUsage );
        SendMessage(hwnd, LB_ADDSTRING, 0, (LPARAM)hStr);
        sprintf_s(hStr, sizeof(hStr), TEXT("     Ref count (p)  = 0x%04X"),         me32.ProccntUsage );
        SendMessage(hwnd, LB_ADDSTRING, 0, (LPARAM)hStr);
        sprintf_s(hStr, sizeof(hStr), TEXT("     Base address   = 0x%08llX"), (DWORD) me32.modBaseAddr );
        SendMessage(hwnd, LB_ADDSTRING, 0, (LPARAM)hStr);
        sprintf_s(hStr, sizeof(hStr), TEXT("     Base size      = %d"),             me32.modBaseSize );
        SendMessage(hwnd, LB_ADDSTRING, 0, (LPARAM)hStr);

    }
    while ( Module32Next( hModuleSnap, &me32 ) );

    CloseHandle( hModuleSnap );
    return ( TRUE );
}

BOOL ListProcessThreads( DWORD dwOwnerPID, HWND hwnd)
{
    HANDLE hThreadSnap = INVALID_HANDLE_VALUE;
    THREADENTRY32 te32;
    char hStr[200];

    // Take a snapshot of all running threads
    hThreadSnap = CreateToolhelp32Snapshot( TH32CS_SNAPTHREAD, 0 );
    if ( hThreadSnap == INVALID_HANDLE_VALUE )
        return ( FALSE );

    // Fill in the size of the structure before using it.
    te32.dwSize = sizeof(THREADENTRY32);

    // Retrieve information about the first thread,
    // and exit if unsuccessful
    if ( !Thread32First( hThreadSnap, &te32 ) )
    {
        printError( TEXT("Thread32First") ); // show cause of failure
        CloseHandle( hThreadSnap ); // clean the snapshot object
        return ( FALSE );
    }

    // Now walk the thread list of the system,
    // and display information about each thread
    // associated with the specified process
    do
    {
        if ( te32.th32OwnerProcessID == dwOwnerPID )
        {
            sprintf_s(hStr, sizeof(hStr), TEXT("     THREAD ID      = 0x%08X"), te32.th32ThreadID );
            SendMessage(hwnd, LB_ADDSTRING, 0, (LPARAM)hStr);
            sprintf_s(hStr, sizeof(hStr), TEXT("     Base priority  = %d"), te32.tpBasePri );
            SendMessage(hwnd, LB_ADDSTRING, 0, (LPARAM)hStr);
            sprintf_s(hStr, sizeof(hStr), TEXT("     Delta priority = %d"), te32.tpDeltaPri );
            SendMessage(hwnd, LB_ADDSTRING, 0, (LPARAM)hStr);
            sprintf_s(hStr, sizeof(hStr), TEXT(""));
            SendMessage(hwnd, LB_ADDSTRING, 0, (LPARAM)hStr);
        }
    }
    while ( Thread32Next(hThreadSnap, &te32 ) );

    CloseHandle( hThreadSnap );
    return ( TRUE );
}

BOOL FilterItem(TASK_LIST task)
{
    BOOL takeIt = TRUE;
    char hStr[MAX_PATH];

    // ProcessName
    if (strcasestr(task.ProcessName, filt_name) == NULL)
    {
        takeIt = FALSE;
    }

    // PID
    sprintf_s(hStr, sizeof(hStr), "%d", task.dwProcessId);
    if ((strlen(filt_pid) > 0) && (strstr(hStr, filt_pid) == NULL))
    {
        takeIt = FALSE;
    }

    // Window-Title
    if (strcasestr(task.WindowTitle, filt_title) == NULL)
    {
        takeIt = FALSE;
    }

    return takeIt;
}

int CreateLine(TASK_LIST t, char *Line, int lng)
{
    int bis = 0;
    char fStr[10] = "% d";

    memset(Line, ' ', lng);
    Line[lng - 1] = 0;

    if ((show.pid > 0) && ((bis + show.pid) < lng))
    {
        //TODO
        sprintf(fStr, "%%%dd", show.pid);
        sprintf(&Line[bis], fStr, t.dwProcessId);
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    if ((show.parent_pid > 0) && ((bis + show.parent_pid) < lng))
    {
        //TODO
        sprintf(fStr, "%%%dd", show.parent_pid);
        sprintf(&Line[bis], fStr, t.dwParentProcessId);
        Line[bis + show.parent_pid] = 0;
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    if ((show.is64 > 0) && ((bis + show.is64) < lng))
    {
        //TODO
        //if (0x14c==t.is64){t.is64=0x386;}
        sprintf(fStr, "%%%dX", show.is64);
        sprintf(&Line[bis], fStr, t.is64 ? 0x32 : 0x64);
        Line[bis + show.is64] = 0;
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    if ((show.usage > 0) && ((bis + show.usage) < lng))
    {
        //TODO
        sprintf(fStr, "%%-%ds%%", show.usage);
        sprintf(&Line[bis], fStr, t.cpuusage);
        Line[bis + show.usage] = 0;
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    if ((show.cputime > 0) && ((bis + show.cputime) < lng))
    {
        //TODO
        sprintf(fStr, "%%-%ds", show.cputime);
        sprintf(&Line[bis], fStr, t.cputime);
        Line[bis + show.cputime] = 0;
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;

    }

    if ((show.name > 0) && ((bis + show.name) < lng))
    {
        //TODO
        sprintf(fStr, "%%-%ds", show.name);
        sprintf(&Line[bis], fStr, t.ProcessName);
        Line[bis + show.name] = 0;
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    if ((show.path > 0) && ((bis + show.path) < lng))
    {
        //TODO
        sprintf(fStr, "%%-%ds", show.path);
        sprintf(&Line[bis], fStr, t.ModulePath);
        Line[bis + show.path] = 0;
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    if ((show.title > 0) && ((bis + show.title) < lng))
    {
        //TODO
        sprintf(fStr, "%%-%ds", show.title);
        sprintf(&Line[bis], fStr, t.WindowTitle);
        Line[bis + show.title] = 0;
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    if ((show.cntThreads > 0) && ((bis + show.cntThreads) < lng))
    {
        //TODO
        sprintf(fStr, "%%%dd", show.cntThreads);
        sprintf(&Line[bis], fStr, t.cntThreads);
        Line[bis + show.cntThreads] = 0;
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    if ((show.handle > 0) && ((bis + show.handle) < lng))
    {
        //TODO
        sprintf(fStr, "%%%dd", show.handle);
        sprintf(&Line[bis], fStr, t.handle);
        Line[bis + show.handle] = 0;
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    if ((show.hwnd > 0) && ((bis + show.hwnd) < lng))
    {
        //TODO
        sprintf(fStr, "%%%dX", show.hwnd);
        sprintf(&Line[bis], fStr, t.hwnd);
        Line[bis + show.title] = 0;
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    if ((show.prio > 0) && ((bis + show.prio) < lng))
    {
        //TODO
        sprintf(fStr, "%%%dd", show.prio);
        sprintf(&Line[bis], fStr, t.pcPriClassBase);
        Line[bis + show.prio] = 0;
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    Line[bis] = 0;

    return bis;
}

int CreateHead(char *Line, int lng)
{
    int bis = 0;
    char fStr[10] = "% d";

    memset(Line, ' ', lng);
    Line[lng - 1] = 0;

    if ((show.pid > 0) && ((bis + show.pid) < lng))
    {
        //TODO
        sprintf(fStr, "%%%ds", show.pid);
        sprintf(&Line[bis], fStr, "PID");
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    if ((show.parent_pid > 0) && ((bis + show.parent_pid) < lng))
    {
        //TODO
        sprintf(fStr, "%%%ds", show.parent_pid);
        sprintf(&Line[bis], fStr, "PIDp");
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    if ((show.is64 > 0) && ((bis + show.is64) < lng))
    {
        //TODO
        sprintf(fStr, "%%%ds", show.is64);
        sprintf(&Line[bis], fStr, "Bit");
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    if ((show.usage > 0) && ((bis + show.usage) < lng))
    {
        //TODO
        sprintf(fStr, "%%%ds", show.usage);
        sprintf(&Line[bis], fStr, "CPU%");
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    if ((show.cputime > 0) && ((bis + show.cputime) < lng))
    {
        //TODO
        sprintf(fStr, "%%%ds", show.cputime);
        sprintf(&Line[bis], fStr, "CPU-Time");
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    if ((show.name > 0) && ((bis + show.name) < lng))
    {
        //TODO
        sprintf(fStr, "%%-%ds", show.name);
        sprintf(&Line[bis], fStr, "Name");
        Line[bis + show.name] = 0;
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    if ((show.path > 0) && ((bis + show.path) < lng))
    {
        //TODO
        sprintf(fStr, "%%-%ds", show.path);
        sprintf(&Line[bis], fStr, "Path");
        Line[bis + show.path] = 0;
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    if ((show.title > 0) && ((bis + show.title) < lng))
    {
        //TODO
        sprintf(fStr, "%%-%ds", show.title);
        sprintf(&Line[bis], fStr, "Title");
        Line[bis + show.title] = 0;
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    if ((show.cntThreads > 0) && ((bis + show.cntThreads) < lng))
    {
        //TODO
        sprintf(fStr, "%%%ds", show.cntThreads);
        sprintf(&Line[bis], fStr, "Thrds");
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    if ((show.handle > 0) && ((bis + show.handle) < lng))
    {
        //TODO
        sprintf(fStr, "%%%ds", show.handle);
        sprintf(&Line[bis], fStr, "Hndl");
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    if ((show.hwnd > 0) && ((bis + show.hwnd) < lng))
    {
        //TODO
        sprintf(fStr, "%%%ds", show.hwnd);
        sprintf(&Line[bis], fStr, "HWND");
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    if ((show.prio > 0) && ((bis + show.prio) < lng))
    {
        //TODO
        sprintf(fStr, "%%%ds", show.prio);
        sprintf(&Line[bis], fStr, "Prio");
        bis = strlen(Line);
        Line[bis] = trenner[0];
        bis++;
    }

    Line[bis] = 0;

    return bis;
}

int CreateLineInfo(TASK_LIST t, char *Line, int lng)
{
    int bis = 0;
    char fStr[10] = "% d";
    char hStr[100];

    memset(Line, ' ', lng);
    Line[0] = 0;

    if (show.pid > 0)
    {
        //TODO
        sprintf_s(fStr, sizeof(fStr), "%%-%dd", show.pid);
        sprintf_s(hStr, sizeof(fStr), fStr, t.dwProcessId);

        strcat_s(Line, lng - strlen(Line), "PID       : ");
        strcat_s(Line, lng - strlen(Line), hStr);
        strcat_s(Line, lng - strlen(Line), "\r\n");
    }

    if (show.parent_pid > 0)
    {
        //TODO
        sprintf_s(fStr, sizeof(fStr), "%%-%dd", show.parent_pid);
        sprintf_s(hStr, sizeof(fStr), fStr, t.dwParentProcessId);

        strcat_s(Line, lng - strlen(Line), "PID-parent: ");
        strcat_s(Line, lng - strlen(Line), hStr);
        strcat_s(Line, lng - strlen(Line), "\r\n");
    }

    if (show.name > 0)
    {
        //TODO
        strcat_s(Line, lng - strlen(Line), "Name      : ");
        strcat_s(Line, lng - strlen(Line), t.ProcessName);
        strcat_s(Line, lng - strlen(Line), "\r\n");
    }

    if (show.path > 0)
    {
        //TODO
        strcat_s(Line, lng - strlen(Line), "Path      : ");
        strcat_s(Line, lng - strlen(Line), t.ModulePath);
        strcat_s(Line, lng - strlen(Line), "\r\n");
    }

    if (show.title > 0)
    {
        //TODO
        strcat_s(Line, lng - strlen(Line), "Title     : ");
        strcat_s(Line, lng - strlen(Line), t.WindowTitle);
        strcat_s(Line, lng - strlen(Line), "\r\n");
    }

    if (show.cntThreads > 0)
    {
        //TODO
        sprintf_s(fStr, sizeof(fStr), "%%-%dd", show.cntThreads);
        sprintf_s(hStr, sizeof(fStr), fStr, t.cntThreads);

        strcat_s(Line, lng - strlen(Line), "Threads   : ");
        strcat_s(Line, lng - strlen(Line), hStr);
        strcat_s(Line, lng - strlen(Line), "\r\n");
    }

    if (show.handle > 0)
    {
        //TODO
        sprintf_s(fStr, sizeof(fStr), "%%-%dd", show.handle);
        sprintf_s(hStr, sizeof(fStr), fStr, t.handle);

        strcat_s(Line, lng - strlen(Line), "Handles   : ");
        strcat_s(Line, lng - strlen(Line), hStr);
        strcat_s(Line, lng - strlen(Line), "\r\n");
    }

    if (show.hwnd > 0)
    {
        //TODO
        sprintf_s(fStr, sizeof(fStr), "%%-%dX", show.hwnd);
        sprintf_s(hStr, sizeof(fStr), fStr, t.hwnd);

        strcat_s(Line, lng - strlen(Line), "HandleWND : ");
        strcat_s(Line, lng - strlen(Line), hStr);
        strcat_s(Line, lng - strlen(Line), "\r\n");
    }

    if (show.prio > 0)
    {
        //TODO
        sprintf_s(fStr, sizeof(fStr), "%%-%dd", show.prio);
        sprintf_s(hStr, sizeof(fStr), fStr, t.pcPriClassBase);

        strcat_s(Line, lng - strlen(Line), "BasePrio  : ");
        strcat_s(Line, lng - strlen(Line), hStr);
        strcat_s(Line, lng - strlen(Line), "\r\n");
    }

    {
        int us, ti;
        int std, min, sec;

        GetProcessPidPerf(t.dwProcessId, &us, &ti);
        std = ti / 3600;
        min = (ti - std * 3600) / 60;
        sec = ti % 60;
        sprintf_s(hStr, sizeof(hStr), "CPU Usage : %2d.%d%%\r\nTime      : %2d:%02d:%02d\r\n", us / 10, us % 10, std, min, sec);
        strcat_s(Line, lng - strlen(Line), hStr);
    }

    return strlen(Line);
}

void GetProcessPidPerf(DWORD dwPid, int *usage, int *time)
{
    HANDLE hProcess;

    hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, dwPid);

    if (NULL != hProcess)
    {
        GetProcessHandlePerf(hProcess, usage, time);
        CloseHandle(hProcess);
    }
    else
    {
        *usage = 0;
        *time  = 0;
    }
}


void GetProcessHandlePerf(HANDLE hProcess, int *usage, int *time)
{
    static INT64 oldSysTime = 0;
    static INT64 SysTime;
    static ULARGE_INTEGER ui[4];
    static ULARGE_INTEGER old[4];
    static INT64 deltaS, deltaP;

    GetProcessTimes(hProcess, (LPFILETIME)&ui[__creation], (LPFILETIME)&ui[__exit], (LPFILETIME)&ui[__kernel], (LPFILETIME)&ui[__user]);
    SysTime = (ui[__kernel].QuadPart + ui[__user].QuadPart); // / si.dwNumberOfProcessors;
    GetSystemTimeAsFileTime((LPFILETIME)&ui[__exit]);
    deltaP = SysTime - oldSysTime;
    deltaS = ui[__exit].QuadPart - old[__exit].QuadPart;
    *usage = ((deltaP) * 1000) / (deltaS);
    *time  = (SysTime) / 100000000;

    memcpy(old, ui, sizeof(old));
    oldSysTime = SysTime;
}

BOOL GetProcessList(HWND hWnd, BOOL force)
{
    HANDLE hProcessSnap;
    HANDLE hProcess;
    PROCESSENTRY32 pe32;
    TASK_LIST *t;
    HANDLE hTList = hwnd_client;
    char hStr[500];
    int oldCnt = 0;
    int oldListTasks = listTasks;
    static BOOL safe = TRUE;
    DWORD count;

    force = TRUE;

    if (! safe)
    {
        return FALSE;
    }
    else
    {
        safe = FALSE;
    }

    // GetProcessHandleCount(GetCurrentProcess(), &count);
    listTasks = 0;
    numTasks = 0;
    trenner[0] = '|';

    // SendMessage(hTList, LB_RESETCONTENT, 0, 0);

    oldCnt = SendMessage(hTList, LB_GETCOUNT, 0, 0);
    oldListTasks = (oldCnt > oldListTasks) ? oldCnt : oldListTasks;

    if (0 == iTopItem)
    {
        // if zero get current value
        iTopItem = SendMessage(hTList, LB_GETTOPINDEX, 0, 0);
        iCaretItem = SendMessage(hTList, LB_GETCARETINDEX, 0, 0);
    }

    EnableDebugPrivNT();

    //GetProcessHandleCount(GetCurrentProcess(), &count);

    // Take a snapshot of all processes in the system.
    hProcessSnap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
    if ( hProcessSnap == INVALID_HANDLE_VALUE )
    {
        MessageBox(hwnd_main, "CreateToolhelp32Snapshot (of processes)", "Error", MB_ICONERROR);
        CloseHandle(hTList);
        safe = TRUE;
        return ( FALSE );
    }

    // Set the size of the structure before using it.
    pe32.dwSize = sizeof( PROCESSENTRY32 );

    // Retrieve information about the first process,
    // and exit if unsuccessful
    if ( !Process32First( hProcessSnap, &pe32 ) )
    {
        // printError( TEXT("Process32First") ); // show cause of failure
        CloseHandle(hProcessSnap);    // clean the snapshot object
        CloseHandle(hTList);
        safe = TRUE;
        return ( FALSE );
    }

    // Now walk the snapshot of processes, and
    // display information about each process in turn
    do
    {
        t = &tlist[numTasks];

        hProcess = OpenProcess( PROCESS_ALL_ACCESS, FALSE, pe32.th32ProcessID );


        if ( hProcess == NULL )
        {
            // printError( TEXT("OpenProcess") );
        }
        else
        {
            IsWow64Process(hProcess, (PBOOL)&t->is64);

            GetProcessHandleCount(hProcess, &t->handle);
            GetProcessTimes(hProcess,
                            (PFILETIME)&t->Times[__creation],
                            (PFILETIME)&t->Times[__exit],
                            (PFILETIME)&t->Times[__kernel],
                            (PFILETIME)&t->Times[__user]);
            GetSystemTimeAsFileTime((PFILETIME)&t->Times[__exit]);

            ULONG64 tt = (t->Times[__exit].QuadPart - t->oldTimes[__exit].QuadPart);
            ULONG64 ct = (t->Times[__kernel].QuadPart + t->Times[__user].QuadPart);
            ULONG64 ot = (t->oldTimes[__kernel].QuadPart + t->oldTimes[__user].QuadPart);
            int h, m, s;
            ULONG64 us = ((ct - ot) * 1000) / tt;
            sprintf_s(t->cpuusage, sizeof(t->cpuusage), "%3d.%d", us / 10, us % 10);
            ct /= 10000000;
            h = ct / 3600;
            m = (ct - h * 3600) / 60;
            s = ct % 60;
            sprintf_s(t->cputime, sizeof(t->cputime), "%3d:%02d:%02d", h, m, s);

            memcpy(&t->oldTimes, &t->Times, sizeof(t->oldTimes));

            CloseHandle( hProcess );
        }


        if (force || (pe32.th32ProcessID != t->dwProcessId))
        {
            // Retrieve the priority class.
            // add to list
            t->dwProcessId = pe32.th32ProcessID;
            t->dwParentProcessId = pe32.th32ParentProcessID;
            strcpy_s(t->ProcessName, sizeof(t->ProcessName), pe32.szExeFile);
            t->flags = pe32.dwFlags;
            t->cntThreads = pe32.cntThreads;
            t->dwModuleId = pe32.th32ModuleID;
            t->pcPriClassBase = pe32.pcPriClassBase;
            GetFirstModulePath(pe32.th32ProcessID, t->ModulePath);
            //QueryFullProcessImageName(hProcess,0,t->ModulePath,sizeof(t->ModulePath));
            //GetProcessImageFileName(hProcess, t->ModulePath,sizeof(t->ModulePath));
            //GetModuleBaseName(hProcess, NULL, t->ModulePath,sizeof(t->ModulePath));
            t->hwnd = find_main_window(pe32.th32ProcessID);
            GetWindowText(t->hwnd, t->WindowTitle, sizeof(t->WindowTitle));

            if (FilterItem(tlist[numTasks]) && (hTList != NULL))
            {
                char xStr[sizeof(hStr)] = "";

                slist[listTasks] = tlist[numTasks];
                // sprintf_s(hStr, sizeof(hStr), "%7d %7d %s", pe32.th32ProcessID, pe32.th32ParentProcessID, pe32.szExeFile);

                SendMessage(hTList, LB_GETTEXT, listTasks, (LPARAM)xStr);
                CreateLine(slist[listTasks], hStr, sizeof(hStr));
                // SendMessage(hTList, LB_ADDSTRING, 0, (LPARAM)hStr);
                if (oldListTasks > listTasks)
                {
                    if (strcmp(hStr, xStr) != 0)
                    {
                        SendMessage(hTList, LB_INSERTSTRING, listTasks, (LPARAM)hStr);
                        SendMessage(hTList, LB_DELETESTRING, listTasks + 1, 0);
                    }
                }
                else
                {
                    SendMessage(hTList, LB_ADDSTRING, 0, (LPARAM)hStr);
                }

                listTasks++;
            }
            else
            {
                t->dwProcessId = 0;
            }
        }

        if (hProcess != NULL)
        {
            CloseHandle(hProcess);
            hProcess = NULL;
        }
        //GetProcessInfo(&tlist[numTasks]);
        numTasks++;
    }
    while (Process32Next(hProcessSnap, &pe32));

    for (int i = listTasks; i < oldListTasks; i++)
    {
        //TODO
        SendMessage(hTList, LB_DELETESTRING, listTasks, 0);
    }

    // sprintf_s(hStr, sizeof(hStr), "Kill Process 4  (%d/%d)", listTasks, numTasks);
    // SetWindowText(hWnd, hStr);
    sprintf_s(hStr, sizeof(hStr), "%d/%d/%d ", numSelected, listTasks, numTasks);
    SendMessage(hwnd_StatusBar, SB_SETTEXT, 0, (LPARAM)hStr);

    char xStr[sizeof(hStr)];
    CreateHead(hStr, sizeof(hStr));
    SendMessage(hwnd_header, WM_GETTEXT, sizeof(xStr), (LPARAM)xStr);

    if (strcmp(hStr, xStr) != 0)
    {
        SendMessage(hwnd_header, WM_SETTEXT, 0, (LPARAM)hStr);
    }

    if (0 < iTopItem)
    {
        SendMessage(hTList, LB_SETTOPINDEX, iTopItem, 0);
        SendMessage(hTList, LB_SETCARETINDEX, iCaretItem, 0);
        iTopItem = 0;
        iCaretItem = 0;
    }
    safe = TRUE;

    CloseHandle(hProcessSnap);
    CloseHandle(hTList);

    GetProcessHandleCount(GetCurrentProcess(), &count);

    return ( TRUE );
}


BOOL GetProcessInfo( PTASK_LIST tlist )
{
    HANDLE hProcess = 0;

    hProcess = OpenProcess( PROCESS_ALL_ACCESS, FALSE, tlist->dwProcessId );
    if (hProcess)
    {
        // USHORT c, d;
        BOOL b;

        IsWow64Process(hProcess, (PBOOL)&b);
        // IsWow64Process2(hProcess, &c, &d);
        CloseHandle( hProcess );
        tlist->is64 = b;
        return TRUE;
    }
    else
    {
        return FALSE;
    }

    return TRUE;
}

BOOL KillProcess( PTASK_LIST tlist, BOOL fForce )
{
    HANDLE hProcess;

    if (fForce || !tlist->hwnd)
    {
        hProcess = OpenProcess( PROCESS_ALL_ACCESS, FALSE, tlist->dwProcessId );
        if (hProcess)
        {
            if (!TerminateProcess( hProcess, 1 ))
            {
                CloseHandle( hProcess );
                return FALSE;
            }

            CloseHandle( hProcess );
            return TRUE;
        }
        else
        {
            return FALSE;
        }
    }

    //
    // kill the process
    //
    PostMessage( tlist->hwnd, WM_CLOSE, 0, 0 );

    return TRUE;
}

int Kill(HWND hDlg)
{
    // find which item has been selected from the list box
    int i = SendMessage(GetDlgItem(hDlg, IDC_MAIN_TEXT), LB_GETCURSEL, 0, 0);
    if (i < 0)
    {
        MessageBox(GetActiveWindow(), "Nothing selected!", "Message", MB_OK);
        return 0;
    }

    // items in a list box are '0' based, the same as our array tlist[]
    // so just pass 'i'
    if (!KillProcess(&slist[i], ForceKill))
        MessageBox(GetActiveWindow(), "Could not Kill Process!", "Message", MB_OK);

    // wait for 1 second before refreshing the list box
    Sleep(1000);
    // GetProcessList(hDlg);
    return 1;
}

int KillMulti(HWND hDlg)
{
    // find which item has been selected from the list box
    HWND hListBox = GetDlgItem(hDlg, IDC_MAIN_TEXT);
    int *pSelList;

    numSelected = SendMessage(hListBox, LB_GETSELCOUNT, 0, 0);

    if (numSelected > 0)
    {
        //TODO
        pSelList = malloc(numSelected*sizeof(int));
        SendMessage(hListBox, LB_GETSELITEMS, numSelected, (LPARAM)pSelList);
        for (int i = 0; i < numSelected; i++)
        {
            //TODO
            int idx = pSelList[i];
            if (!KillProcess(&slist[idx], ForceKill))
            {
                char hStr[100];
                sprintf_s(hStr, sizeof(hStr), "%s (%d) could not be killed!",
                          slist[idx].ProcessName,
                          slist[idx].dwProcessId);
                MessageBox(hDlg, hStr, "Error", MB_ICONERROR);
            }

        }
        CloseHandle(hListBox);
        Sleep(1000);
        GetProcessList(hDlg, FALSE);
    }

    return 1;
}

static LRESULT CALLBACK DlgDisplayL(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    char hStr[1000];
    RECT r;

    hwnd_disp = hwndDlg;

    switch (LOWORD(uMsg))
    {
        case WM_INITDIALOG:
        {
            int i = SendMessage(GetDlgItem(hwnd_main, IDC_MAIN_TEXT), LB_GETCURSEL, 0, 0);
            hwnd_disp = hwndDlg;
            MoveWindow(hwndDlg, rMain.left + 50, rMain.top + 50, rMain.right - rMain.left - 100, rMain.bottom - rMain.top - 100, TRUE);
            CreateLineInfo(slist[i], hStr, sizeof(hStr));
            SetDlgItemText(hwndDlg, IDD_DISP_TEXT, hStr);
            SetTimer(hwndDlg, IDT_REFRESH_LINE, 500, NULL);
            break;
        }

        case WM_SIZE:
        {
            GetClientRect(hwndDlg, &r);
            // MoveWindow(GetDlgItem(hwndDlg, IDD_DISP_TEXT), r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
            MoveWindow(GetDlgItem(hwndDlg, IDD_DISP_TEXT), 0, 0, r.right - r.left, r.bottom - r.top, TRUE);
            // InvalidateRect(GetDlgItem(hwndDlg, IDD_DISP_TEXT), &r, TRUE);
            // UpdateWindow(GetDlgItem(hwndDlg, IDD_DISP_TEXT));
            // UpdateWindow(hwndDlg);
            break;
        }

        case WM_TIMER:
        {
            int i = SendMessage(GetDlgItem(hwnd_main, IDC_MAIN_TEXT), LB_GETCURSEL, 0, 0);
            CreateLineInfo(slist[i], hStr, sizeof(hStr));
            SetDlgItemText(hwndDlg, IDD_DISP_TEXT, hStr);
            break;
        }

        case WM_LBUTTONDOWN:
        case WM_CLOSE:
        {
            hwnd_disp = NULL;
            KillTimer(hwndDlg, IDT_REFRESH_LINE);
            EndDialog(hwndDlg, 0);
            return TRUE;
        }

        default:
            return DefWindowProc(hwndDlg, uMsg, wParam, lParam);
    }
    return FALSE;
}


static LRESULT CALLBACK DlgDisplayM(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    char hStr[1000];
    RECT r;


    switch (LOWORD(uMsg))
    {
        case WM_INITDIALOG:
        {
            hwnd_disp = hwndDlg;

            int i = SendMessage(GetDlgItem(hwnd_main, IDC_MAIN_TEXT), LB_GETCURSEL, 0, 0);
            MoveWindow(hwndDlg, rMain.left + 50, rMain.top + 50, rMain.right - rMain.left - 100, rMain.bottom - rMain.top - 100, TRUE);
            ListProcessModules(slist[i].dwProcessId, GetDlgItem(hwndDlg, IDD_DISP_TEXT));
            //SetDlgItemText(hwndDlg, IDD_DISP_TEXT, hStr);
            break;
        }

        case WM_SIZE:
        {
            GetClientRect(hwndDlg, &r);
            MoveWindow(GetDlgItem(hwndDlg, IDD_DISP_TEXT), r.top, r.left, r.right - r.left, r.bottom - r.top, TRUE);
            //InvalidateRect(GetDlgItem(hwndDlg, IDD_DISP_TEXT), &r, TRUE);
            // UpdateWindow(GetDlgItem(hwndDlg, IDD_DISP_TEXT));
            // UpdateWindow(hwndDlg);
            break;
        }

        case WM_LBUTTONDOWN:
        case WM_CLOSE:
        {
            hwnd_disp = NULL;
            EndDialog(hwndDlg, 0);
            return TRUE;
        }

        default:
            return DefWindowProc(hwndDlg, uMsg, wParam, lParam);

    }
    return FALSE;
}


static LRESULT CALLBACK DlgDisplayT(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    char hStr[1000];
    RECT r;

    hwnd_disp = hwndDlg;
    trenner[0] = '\n';

    switch (LOWORD(uMsg))
    {
        case WM_INITDIALOG:
        {
            int i = SendMessage(GetDlgItem(hwnd_main, IDC_MAIN_TEXT), LB_GETCURSEL, 0, 0);
            MoveWindow(hwndDlg, rMain.left + 50, rMain.top + 50, rMain.right - rMain.left - 100, rMain.bottom - rMain.top - 100, TRUE);
            ListProcessThreads(slist[i].dwProcessId, GetDlgItem(hwndDlg, IDD_DISP_TEXT));
            break;
        }

        case WM_SIZE:
        {
            GetClientRect(hwndDlg, &r);
            MoveWindow(GetDlgItem(hwndDlg, IDD_DISP_TEXT), r.top, r.left, r.right - r.left, r.bottom - r.top, TRUE);
            //InvalidateRect(GetDlgItem(hwndDlg, IDD_DISP_TEXT), &r, TRUE);
            // UpdateWindow(GetDlgItem(hwndDlg, IDD_DISP_TEXT));
            // UpdateWindow(hwndDlg);
            break;
        }

        case WM_LBUTTONDOWN:
        case WM_CLOSE:
        {
            hwnd_disp = NULL;
            EndDialog(hwndDlg, 0);
            return TRUE;
        }
    }
    return FALSE;
}


static LRESULT CALLBACK DlgProcShow(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    char hStr[100];


    switch (uMsg)
    {
        case WM_INITDIALOG:
            if (NULL != hwnd_show)
            {
                SetWindowPos (hwnd_show, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                Sleep (1000);
                SetWindowPos (hwnd_show, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                EndDialog(hwndDlg, TRUE);
            }
            else
            {
                hwnd_show = hwndDlg;
                itoa(show.pid, hStr, 10);
                SetDlgItemText(hwndDlg, IDD_SHOW_PID, hStr);
                itoa(show.parent_pid, hStr, 10);
                SetDlgItemText(hwndDlg, IDD_SHOW_PPID, hStr);
                itoa(show.name, hStr, 10);
                SetDlgItemText(hwndDlg, IDD_SHOW_NAME, hStr);
                itoa(show.path, hStr, 10);
                SetDlgItemText(hwndDlg, IDD_SHOW_PATH, hStr);
                itoa(show.title, hStr, 10);
                SetDlgItemText(hwndDlg, IDD_SHOW_TITLE, hStr);
                itoa(show.cntThreads, hStr, 10);
                SetDlgItemText(hwndDlg, IDD_SHOW_THREADS, hStr);
                itoa(show.hwnd, hStr, 10);
                SetDlgItemText(hwndDlg, IDD_SHOW_HWND, hStr);
                itoa(show.prio, hStr, 10);
                SetDlgItemText(hwndDlg, IDD_SHOW_PRIO, hStr);
                itoa(show.is64, hStr, 10);
                SetDlgItemText(hwndDlg, IDD_SHOW_6432, hStr);
                itoa(show.usage, hStr, 10);
                SetDlgItemText(hwndDlg, IDD_SHOW_USAGE, hStr);
                itoa(show.cputime, hStr, 10);
                SetDlgItemText(hwndDlg, IDD_SHOW_CPUTIME, hStr);
                ShowWindow(GetDlgItem(hwndDlg, IDD_SHOW_RESERVE), SW_HIDE);
                ShowWindow(GetDlgItem(hwndDlg, _IDD_SHOW_RESERVE), SW_HIDE);
            }
            return TRUE;

        case WM_COMMAND:
            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
                case IDOK:
                    GetDlgItemText(hwndDlg, IDD_SHOW_PID, hStr, sizeof(hStr));
                    show.pid = atoi(hStr);
                    GetDlgItemText(hwndDlg, IDD_SHOW_PPID, hStr, sizeof(hStr));
                    show.parent_pid = atoi(hStr);
                    GetDlgItemText(hwndDlg, IDD_SHOW_NAME, hStr, sizeof(hStr));
                    show.name = atoi(hStr);
                    GetDlgItemText(hwndDlg, IDD_SHOW_PATH, hStr, sizeof(hStr));
                    show.path = atoi(hStr);
                    GetDlgItemText(hwndDlg, IDD_SHOW_TITLE, hStr, sizeof(hStr));
                    show.title = atoi(hStr);
                    GetDlgItemText(hwndDlg, IDD_SHOW_THREADS, hStr, sizeof(hStr));
                    show.cntThreads = atoi(hStr);
                    GetDlgItemText(hwndDlg, IDD_SHOW_HWND, hStr, sizeof(hStr));
                    show.hwnd = atoi(hStr);
                    GetDlgItemText(hwndDlg, IDD_SHOW_PRIO, hStr, sizeof(hStr));
                    show.prio = atoi(hStr);
                    GetDlgItemText(hwndDlg, IDD_SHOW_6432, hStr, sizeof(hStr));
                    show.is64 = atoi(hStr);
                    GetDlgItemText(hwndDlg, IDD_SHOW_USAGE, hStr, sizeof(hStr));
                    show.usage = atoi(hStr);
                    GetDlgItemText(hwndDlg, IDD_SHOW_CPUTIME, hStr, sizeof(hStr));
                    show.cputime = atoi(hStr);

                    GetProcessList(hwnd_main, FALSE);

                    hwnd_show = NULL;
                    EndDialog(hwndDlg, 1);
                    return TRUE;

                case CM_FILE_REFRESH:
                    GetDlgItemText(hwndDlg, IDD_SHOW_PID, hStr, sizeof(hStr));
                    show.pid = atoi(hStr);
                    GetDlgItemText(hwndDlg, IDD_SHOW_PPID, hStr, sizeof(hStr));
                    show.parent_pid = atoi(hStr);
                    GetDlgItemText(hwndDlg, IDD_SHOW_NAME, hStr, sizeof(hStr));
                    show.name = atoi(hStr);
                    GetDlgItemText(hwndDlg, IDD_SHOW_PATH, hStr, sizeof(hStr));
                    show.path = atoi(hStr);
                    GetDlgItemText(hwndDlg, IDD_SHOW_TITLE, hStr, sizeof(hStr));
                    show.title = atoi(hStr);
                    GetDlgItemText(hwndDlg, IDD_SHOW_THREADS, hStr, sizeof(hStr));
                    show.cntThreads = atoi(hStr);
                    GetDlgItemText(hwndDlg, IDD_SHOW_HWND, hStr, sizeof(hStr));
                    show.hwnd = atoi(hStr);
                    GetDlgItemText(hwndDlg, IDD_SHOW_PRIO, hStr, sizeof(hStr));
                    show.prio = atoi(hStr);
                    GetDlgItemText(hwndDlg, IDD_SHOW_6432, hStr, sizeof(hStr));
                    show.is64 = atoi(hStr);
                    GetDlgItemText(hwndDlg, IDD_SHOW_USAGE, hStr, sizeof(hStr));
                    show.usage = atoi(hStr);
                    GetDlgItemText(hwndDlg, IDD_SHOW_CPUTIME, hStr, sizeof(hStr));
                    show.cputime = atoi(hStr);

                    GetProcessList(hwnd_main, TRUE);

                    return TRUE;

                case IDCANCEL:
                    hwnd_show = NULL;
                    EndDialog(hwndDlg, 0);
                    return TRUE;

                default:
                    return DefWindowProc(hwndDlg, uMsg, wParam, lParam);
            }
    }
    return FALSE;
}

static LRESULT CALLBACK DlgProcFilter(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    char hStr[100];
    RECT hRect;
    int changed;


    switch (uMsg)
    {
        case WM_INITDIALOG:
            if (NULL != hwnd_filt)
            {
                SetWindowPos (hwnd_filt, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                Sleep (1000);
                SetWindowPos (hwnd_filt, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                EndDialog(hwndDlg, TRUE);
            }
            else
            {
                hwnd_filt = hwndDlg;
                memset(hStr, 0, sizeof(hStr));
                SetDlgItemText(hwndDlg, IDD_EDIT_NAME, filt_name  );
                SetDlgItemText(hwndDlg, IDD_EDIT_PID, filt_pid   );
                SetDlgItemText(hwndDlg, IDD_EDIT_TITLE, filt_title );
                GetWindowRect(hwndDlg, &hRect);
                MoveWindow(hwndDlg, rMain.left + 100, rMain.top + 100, hRect.right - hRect.left, hRect.bottom - hRect.top, TRUE);
                SetTimer(hwndDlg, IDT_CHANGED_FILT, 100, NULL);
                SetFocus(hwndDlg);
                ShowWindow(hwnd_sedit, SW_HIDE);
            }
            return TRUE;

        case WM_COMMAND:
            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
                case CM_FILE_KILL:
                    Kill(hwnd_main);
                    return TRUE;

                case CM_FILE_REFRESH:
                    GetProcessList(hwnd_main, TRUE);
                    return TRUE;

                case IDOK:
                    GetDlgItemText(hwndDlg, IDD_EDIT_NAME, filt_name, sizeof(filt_name  ));
                    GetDlgItemText(hwndDlg, IDD_EDIT_PID, filt_pid, sizeof(filt_pid   ));
                    GetDlgItemText(hwndDlg, IDD_EDIT_TITLE, filt_title, sizeof(filt_title ));
                    GetProcessList(hwnd_main, TRUE);
                    ShowWindow(hwnd_sedit, SW_SHOW);
                    hwnd_filt = NULL;
                    KillTimer(hwndDlg, IDT_CHANGED_FILT);
                    EndDialog(hwndDlg, TRUE);
                    return TRUE;

                case IDCANCEL:
                    filt_name[0]  = 0;
                    filt_pid[0]   = 0;
                    filt_title[0] = 0;
                    SendMessage(hwnd_StatusBar, SB_SETTEXT, 2, (LPARAM)filt_name);
                    SendMessage(hwnd_StatusBar, SB_SETTEXT, 3, (LPARAM)filt_pid);
                    SetWindowText(hwnd_sedit, "");
                    GetProcessList(hwnd_main, TRUE);
                    ShowWindow(hwnd_sedit, SW_SHOW);
                    hwnd_filt = NULL;
                    KillTimer(hwndDlg, IDT_CHANGED_FILT);
                    EndDialog(hwndDlg, FALSE);
                    return TRUE;
            }
            break;

        case WM_KEYUP:
        case WM_KEYDOWN:
        case WM_CHAR:
            sprintf_s(hStr, sizeof(hStr), "WM_... %d %d %ld", uMsg, wParam, lParam);
            MessageBox(hwndDlg, hStr, "diverse WM_...", MB_OK);
            break;

        case WM_TIMER:
            if (wParam == IDT_CHANGED_FILT)
            {
                //Timer-functions for Filter
                changed = 0;

                GetDlgItemText(hwndDlg, IDD_EDIT_NAME, hStr, sizeof(hStr));
                if (strcmp(hStr, filt_name) != 0)
                {
                    //Name changed
                    strcpy(filt_name, hStr);
                    SendMessage(hwnd_StatusBar, SB_SETTEXT, 2, (LPARAM)hStr);
                    SetWindowText(hwnd_sedit, hStr);
                    changed = 1;
                }

                GetDlgItemText(hwndDlg, IDD_EDIT_PID, hStr, sizeof(hStr));
                if (strcmp(hStr, filt_pid) != 0)
                {
                    //PID changed
                    for (int i = 0; i < strlen(hStr); i++)
                    {
                        //TODO
                        if (!isdigit(hStr[i]))
                        {
                            //TODO
                            hStr[i] = 0;
                            SetDlgItemText(hwndDlg, IDD_EDIT_PID, hStr);
                        }
                    }
                    strcpy(filt_pid, hStr);
                    SendMessage(hwnd_StatusBar, SB_SETTEXT, 3, (LPARAM)hStr);
                    changed = 1;
                }

                GetDlgItemText(hwndDlg, IDD_EDIT_TITLE, hStr, sizeof(hStr));
                if (strcmp(hStr, filt_title) != 0)
                {
                    //Title changed
                    strcpy(filt_title, hStr);
                    SendMessage(hwnd_StatusBar, SB_SETTEXT, 3, (LPARAM)hStr);
                    SetWindowText(hwnd_sedit, hStr);
                    changed = 1;
                }

                if (changed != 0)
                {
                    //TODO
                    GetProcessList(hwnd_main, FALSE);
                }
            }
            break;

        case WM_CLOSE:
            hwnd_filt = NULL;
            ShowWindow(hwnd_sedit, SW_SHOW);
            EndDialog(hwndDlg, 0);
            return TRUE;

        default:
            return DefWindowProc(hwndDlg, uMsg, wParam, lParam);
    }

    return FALSE;
}

void doCmdLine(void)
{
    if (argc > 0)
    {
        for (int i = 0; i < argc; i++)
        {
            char exe[strlen(argv[i]) + 5];
            strcpy(exe, argv[i]);
            long l = strlen(exe);
            if (l > 4)
            {
                if (stricmp(&exe[l - 4], szExe) != 0)
                {
                    strcat(exe, szExe);
                }
            }
            else
            {
                strcat(exe, szExe);
            }
            for (int ii = 0; ii < numTasks; ii++)
            {
                if (stricmp(tlist[ii].ProcessName, argv[i]) == 0)
                {
                    if (!KillProcess( &tlist[ii], ForceKill ))
                        MessageBox(GetActiveWindow(), "Could not Kill Process!", "Message", MB_OK);
                }
                if (stricmp(tlist[ii].ProcessName, exe) == 0)
                {
                    if (!KillProcess(&tlist[ii], ForceKill))
                        MessageBox(GetActiveWindow(), "Could not Kill Process!", "Message", MB_OK);
                }
            }
        }
        fprintf(stdout, "KillProcess4 ready!\n\n");
        exit(0);
    }
}

static int ScanCmdLine(const char *cmdline)
{
    int i, l;

    if ((l = strlen(cmdline)) > 0)
    {
        argv[argc] = malloc(l + 1);
        strcpy(argv[argc], cmdline);

        for (i = 0; i < l; i++)
        {
            while (isblank(cmdline[i]) == 0)
            {
                i++;
                if (l <= i)
                {
                    break;
                }
            }
            argv[0][i] = 0;
            argc++;
            while (isblank(cmdline[i]) != 0)
            {
                i++;
                if (l <= i)
                {
                    break;
                }
            }
            argv[argc] = &argv[0][i];
        }
    }
    else
    {
        argv[0] = NULL;
    }
    return argc;
}

LRESULT CALLBACK WndBaseProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static int xPos = 0; // Aktuelle horizontale Position
    int x;
    RECT r;
    HDC hdc;
    SIZE size;
    char hStr[500];

    switch (uMsg)
    {
        case WM_CREATE:
            GetClientRect(hwnd_main, &r);
            MoveWindow(hwnd_base, 0, 7, r.right - r.left, r.bottom - r.top - 20, TRUE);
            return 0;

        case WM_SIZE:
            GetClientRect(hwnd_main, &r);
            MoveWindow(hwnd_base, 0, 7, r.right - r.left, r.bottom - r.top - 20, TRUE);
            hdc = GetWindowDC(hwnd_header);
            GetWindowText(hwnd_header, hStr, sizeof(hStr));
            GetTextExtentPoint32(hdc, hStr, strlen(hStr), &size);
            SetScrollRange(hwnd_base, SB_HORZ, 0, size.cx, TRUE);
            break;

        case WM_HSCROLL:

        switch (LOWORD(wParam))
            {
                case SB_LINELEFT:
                    xPos -= 8; // Eine Einheit nach links scrollen
                    break;
                case SB_LINERIGHT:
                    xPos += 8; // Eine Einheit nach rechts scrollen
                    break;
                case SB_PAGELEFT:
                    xPos -= 80; // Eine Seite nach links scrollen
                    break;
                case SB_PAGERIGHT:
                    xPos += 80; // Eine Seite nach rechts scrollen
                    break;
                case SB_THUMBPOSITION:
                case SB_THUMBTRACK:
                    xPos = HIWORD(wParam); // Position des Bildlauffelds setzen
                    break;
                case SB_ENDSCROLL:
                    // Scrollen beenden
                    break;
            }
            // Position des Bildlauffelds aktualisieren
            SetScrollPos(hwnd, SB_HORZ, xPos, TRUE);
            InvalidateRect(hwnd, NULL, TRUE); // Fensterinhalt neu zeichnen
            return 0;
        case WM_VSCROLL:
            // Vertikales Scrollen behandeln (�hnlich wie horizontales Scrollen)
            return 0;
    }

}

LRESULT CALLBACK WndProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
    char mStr[200];
    RECT rBase, rClient, rStatus;
    RECT r;
    int x;
    DWORD h;
    UINT uStatusHeight;

    switch (Message)
    {
        case WM_CREATE:
        {
            hwnd_main = hwnd;

            // Move window when we got it from pipe
            if (rMain.top || rMain.left || rMain.bottom || rMain.right)
            {
                //TODO
                MoveWindow(hwnd, rMain.left, rMain.top, rMain.right - rMain.left, rMain.bottom - rMain.top, TRUE);
            }

            // Create Base
            hwnd_base = CreateWindow("Static", "", WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL | WS_OVERLAPPED,
                                     CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, (HMENU)IDD_STAT_BASE, GetModuleHandle(NULL), NULL);
            LONG_PTR oldProc = SetWindowLongPtr(hwnd_base, GWLP_WNDPROC, (LONG_PTR)WndBaseProc);
            ShowWindow(hwnd_base, TRUE);
            // Create client
            hwnd_client = CreateWindow("ListBox", "",  LBS_MULTIPLESEL | WS_TABSTOP | WS_CHILD | WS_VISIBLE | WS_VSCROLL,
                                       CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd_base, (HMENU)IDC_MAIN_TEXT, GetModuleHandle(NULL), NULL);
            x = SendDlgItemMessage(hwnd_base, IDC_MAIN_TEXT, WM_SETFONT, (WPARAM)GetStockObject(ANSI_FIXED_FONT), MAKELPARAM(TRUE, 0));
            SendMessage(hwnd_client, LB_INITSTORAGE, 1000, 500);
            sprintf_s(mStr, sizeof(mStr), "Kill Process: Version %s", versStr);

            h = SendMessage(hwnd_client, LB_GETITEMHEIGHT, 0, 0);
            hHeader = h + 2;

            // Header window
            hwnd_header = CreateWindow("Static", "",  WS_BORDER | WS_CHILD | WS_VISIBLE | WS_OVERLAPPED,
                                       CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd_base, (HMENU)IDC_MAIN_HEAD, GetModuleHandle(NULL), NULL);
            x = SendDlgItemMessage(hwnd_base, IDC_MAIN_HEAD, WM_SETFONT, (WPARAM)GetStockObject(ANSI_FIXED_FONT), MAKELPARAM(TRUE, 0));


            // Create Statusbar
            hwnd_StatusBar = CreateWindowEx(0, STATUSCLASSNAME, NULL, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hwnd, (HMENU)ID_STATUSBAR, g_hInst, NULL);
            SendMessage(hwnd_StatusBar, SB_SETPARTS, 6, (LPARAM)iStatusWidths);
            SendMessage(hwnd_StatusBar, SB_SETTEXT, 1, (LPARAM)"Name:");
            SendMessage(hwnd_StatusBar, SB_SETTEXT, 5, (LPARAM)mStr);
            SendMessage(hwnd_StatusBar, SB_SETTEXT, 4, (LPARAM)"CPU:  0%");

            GetWindowRect(hwnd_StatusBar, &r);
            hwnd_sedit = CreateWindow("Edit", "", WS_POPUP | WS_TABSTOP | WS_VISIBLE | WS_BORDER, r.left + iStatusWidths[1] + 2, r.top + 2, iStatusWidths[3] - iStatusWidths[2], r.bottom - r.top - 2, hwnd, NULL, GetModuleHandle(NULL), NULL);
            SendMessage(hwnd_sedit, WM_SETFONT, (WPARAM)GetStockObject(ANSI_FIXED_FONT), MAKELPARAM(TRUE, 0));
            SetWindowText(hwnd_sedit, filt_name);
            ShowWindow(hwnd_sedit, SW_HIDE);

            //GetWindowRect(hwnd_StatusBar, &r);
            //uStatusHeight = r.bottom - r.top;
            //MoveWindow(GetDlgItem(hwnd, IDC_MAIN_TEXT), 0, h, rClient.right, rClient.bottom - uStatusHeight-h, TRUE);

            SetTimer(hwnd, IDT_CHANGED_MAIN, 400, NULL);
            SetTimer(hwnd, IDT_ACTION, 200, NULL);
            SetTimer(hwnd, IDT_REFRESH, 2000, NULL);
            hMain    = GetMenu(hwnd);
            hContext = CreatePopupMenu();
            AppendMenu(hContext, MF_STRING, CM_FILE_KILL, "&Kill Task");
            AppendMenu(hContext, MF_STRING, CM_FILE_FILT, "&Filter Task");
            AppendMenu(hContext, MF_STRING, CM_CMENU_LINE, "Show &Line");
            AppendMenu(hContext, MF_STRING, CM_CMENU_MODULES, "Show &Modules");
            AppendMenu(hContext, MF_STRING, CM_CMENU_THREADS, "Show &Threads");
            GetProcessList(hwnd, FALSE);

//          SetFocus(hwnd_client);
//          SendMessage(hwnd, WM_PAINT, 0, 0);
            break;
        }
//    case WM_PAINT:
//    {
//        PAINTSTRUCT ps;
//        RECT r1, r2;
//        HDC hdc = BeginPaint(hwnd, &ps);
//        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(10, 10, 10));
//        SelectPen(hdc, hPen);
//        GetWindowRect(hwnd_client, &r1);
//        GetWindowRect(hwnd_StatusBar,&r2);
//        MoveToEx(hdc, 62, 0, NULL);
//        LineTo(hdc, 62, r2.top - r1.top);
//        DeleteObject(hPen);
//        EndPaint(hwnd, &ps);
//        break;
//    }
        case WM_SIZE:
        {
            SendMessage(hwnd_StatusBar, WM_SIZE, 0, 0);

            GetWindowRect(hwnd, &rMain);
            GetClientRect(hwnd, &rBase);
            GetClientRect(hwnd_base, &rClient);
            GetWindowRect(hwnd_StatusBar, &rStatus);
            uStatusHeight = rStatus.bottom - rStatus.top;

            if (wParam != SIZE_MINIMIZED)
            {
                MoveWindow(hwnd_base, 0, 0, rBase.right, rBase.bottom - uStatusHeight, TRUE);
                MoveWindow(hwnd_header, 1, 0, rBase.right, hHeader, TRUE);
                //GetWindowRect(hwnd_header, &r);
                //InvalidateRect(NULL, &r, TRUE);
                CreateHead(mStr, sizeof(mStr));
                SetWindowText(hwnd_header, mStr);

                MoveWindow(hwnd_client, 0, hHeader, rBase.right, rBase.bottom - uStatusHeight - hHeader, TRUE);
                MoveWindow(hwnd_sedit, rStatus.left + iStatusWidths[1] + 2, rStatus.top + 2, iStatusWidths[2] - iStatusWidths[1], rStatus.bottom - rStatus.top - 2, TRUE);
                ShowWindow(hwnd_sedit, SW_SHOW);

                x = SendMessage(hwnd_client, LB_GETTEXTLEN, 0, (LPARAM)&r);
                x *= 8;

                SetScrollRange(hwnd_base, SB_HORZ, 0, 100, TRUE);
                //SendMessage(hwnd_base, LB_SETHORIZONTALEXTENT, 1220, 0);
                //SendMessage(hwnd_client, LB_SETHORIZONTALEXTENT, x, 0);
                //SendMessage(hwnd_header, LB_SETHORIZONTALEXTENT, x, 0);
            }

            if (0 < iTopItem)
            {
                // select the old view
                SendMessage(hwnd_client, LB_SETTOPINDEX, iTopItem, 0);
                SendMessage(hwnd_client, LB_SETCARETINDEX, iCaretItem, 0);
                iTopItem = 0;
                iCaretItem = 0;
            }
            break;
        }
        case WM_MOVE:
            GetWindowRect(hwnd, &rMain);
            GetWindowRect(hwnd_StatusBar, &rStatus);
            MoveWindow(hwnd_sedit, rStatus.left + iStatusWidths[1] + 2, rStatus.top + 2, iStatusWidths[2] - iStatusWidths[1], rStatus.bottom - rStatus.top - 2, TRUE);
            break;

        case WM_SETFOCUS:
            // SetFocus(GetDlgItem(hwnd, IDC_MAIN_TEXT));
            break;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case CM_FILE_EXIT:
                    PostMessage(hwnd, WM_CLOSE, 0, 0);
                    break;

                case CM_FILE_REFRESH:
                    //SendMessage(GetDlgItem(hwnd, IDC_MAIN_TEXT), LB_ADDSTRING,0,(WPARAM)"Eins");
                    GetProcessList(hwnd, TRUE);

                    break;

                case CM_FILE_KILL:
                    Kill(hwnd);
                    GetProcessList(hwnd, FALSE);
                    break;

                case CM_FILE_KILLM:
                    KillMulti(hwnd);
                    GetProcessList(hwnd, FALSE);
                    break;

                case CM_FILE_FILT:
                    DialogBox(g_hInst, MAKEINTRESOURCE(DLG_FILTER), NULL, (DLGPROC)DlgProcFilter);
                    break;

                case CM_FILE_FILT_CLR:
                    filt_name[0]  = 0;
                    filt_pid[0]   = 0;
                    filt_title[0] = 0;
                    SendMessage(hwnd_StatusBar, SB_SETTEXT, 2, (LPARAM)filt_name);
                    SendMessage(hwnd_StatusBar, SB_SETTEXT, 3, (LPARAM)filt_pid);
                    SetWindowText(hwnd_sedit, filt_name);
                    GetProcessList(hwnd_main, FALSE);
                    break;

                case CM_OPT_SHOW:
                    DialogBox(g_hInst, MAKEINTRESOURCE(DLG_SHOW), NULL, (DLGPROC)DlgProcShow);
                    break;

                case CM_FILE_RESTART:
                    RestartAsAdmin();
                    break;

                case CM_SYSTEMINFO:
                {
                    SYSTEM_INFO si;
                    char *proc;

                    GetNativeSystemInfo(&si);

                    switch (si.wProcessorArchitecture)
                    {
                        case 12:
                            proc = "ARM64";
                            break;
                        case 9:
                            proc = "x64";
                            break;
                        case 6:
                            proc = "Itanium";
                            break;
                        case 5:
                            proc = "ARM";
                            break;
                        case 0:
                            proc = "x86";
                            break;
                        default:
                            proc = "???";
                            break;
                    }
                    sprintf_s(mStr, sizeof(mStr),
                              "Processor %s\r\nProcessor Mask %X\r\nAnzahl %d\r\nProcessor level %d",
                              proc,
                              si.dwActiveProcessorMask,
                              si.dwNumberOfProcessors,
                              si.wProcessorLevel
                             );
                    MessageBox (hwnd_main, mStr, "System info ...", MB_ICONINFORMATION);
                }
                break;

                case CM_ABOUT:
                {
                    int i;
                    char prg[MAX_PATH];

                    GetModuleFileName(GetModuleHandle(NULL), prg, sizeof(prg));
                    for (i = strlen(prg); (prg[i] != '\\') && (prg[i] != '/') && (i >= 0); i--);
                    sprintf_s(mStr, sizeof(mStr), "Name   : %s\r\nVersion: %s\r\nCreated using the Win32 API", &prg[i + 1], versStr);
                    MessageBox (hwnd_main, mStr, "About...", 0);
                }
                break;

                case CM_FILE_ALL:
                {
                    LPARAM l = 0 + ((listTasks - 1) << 16);
                    SendMessage(hwnd_client, LB_SELITEMRANGE, TRUE, l);
                    break;
                }

                case CM_FILE_NON:
                {
                    LPARAM l = 0 + ((listTasks - 1) << 16);
                    SendMessage(hwnd_client, LB_SELITEMRANGE, FALSE, l);
                    break;
                }

                case CM_CMENU_LINE:
                {
                    //HWND hLine = CreateWindow("Static", "\r\nLine", WS_CLIPCHILDREN | WS_HSCROLL | WS_VSCROLL | WS_SIZEBOX,
                    //rMain.left + 100, rMain.top + 100, 300, 300,
                    //hwnd_main, NULL, g_hInst, 0);
                    DialogBox(g_hInst, MAKEINTRESOURCE(DLG_DISPLAY_LINE), NULL, (DLGPROC)DlgDisplayL);
                    SetWindowText(hwnd_disp, "Info");            //SetDlgItemText(hDisplay,IDD_DISP_TEXT,"Hallo\r\nAlex!");
                    //ShowWindow(hDisplay, TRUE);
                    break;
                }

                case CM_CMENU_MODULES:
                {
                    //HWND hLine = CreateWindow("Static", "\r\nLine", WS_CLIPCHILDREN | WS_HSCROLL | WS_VSCROLL | WS_SIZEBOX,
                    //rMain.left + 100, rMain.top + 100, 300, 300,
                    //hwnd_main, NULL, g_hInst, 0);
                    DialogBox(g_hInst, MAKEINTRESOURCE(DLG_DISPLAY_LIST), NULL, (DLGPROC)DlgDisplayM);
                    SetWindowText(hwnd_disp, "Modula");
                    //SetDlgItemText(hDisplay,IDD_DISP_TEXT,"Hallo\r\nAlex!");
                    //ShowWindow(hDisplay, TRUE);
                    break;
                }

                case CM_CMENU_THREADS:
                {
                    //HWND hLine = CreateWindow("Static", "\r\nLine", WS_CLIPCHILDREN | WS_HSCROLL | WS_VSCROLL | WS_SIZEBOX,
                    //rMain.left + 100, rMain.top + 100, 300, 300,
                    //hwnd_main, NULL, g_hInst, 0);
                    DialogBox(g_hInst, MAKEINTRESOURCE(DLG_DISPLAY_LIST), NULL, (DLGPROC)DlgDisplayT);
                    SetWindowText(hwnd_disp, "Threads");
                    //SetDlgItemText(hDisplay,IDD_DISP_TEXT,"Hallo\r\nAlex!");
                    //ShowWindow(hDisplay, TRUE);
                    break;
                }

                case CM_CHANGE_FOKUS:
                {
                    HANDLE hwndF = GetFocus();
                    if (hwndF == hwnd_sedit)
                    {
                        SetFocus(hwnd_client);
                    }
                    else
                    {
                        SetFocus(hwnd_sedit);
                    }
                    CloseHandle(hwndF);
                    break;
                }

                case CM_CHANGE_TOP:
                {
                    if (!TopMost)
                    {
                        SetWindowPos (hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                    }
                    else
                    {
                        SetWindowPos (hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                    }
                    TopMost = !TopMost;
                    break;
                }

                case CM_TEST:
                {
                    POINT pt;
                    short x = GetAsyncKeyState(VK_LBUTTON);
                    short y = GetAsyncKeyState(VK_RBUTTON);
                    int i;
                    GetCursorPos(&pt);

                    LPARAM l = 0 + ((listTasks - 1) << 16);
                    SendMessage(hwnd_client, LB_SELITEMRANGE, FALSE, l);

                    i = SendMessage(hwnd_client, LB_GETCURSEL, 0, 0);
                    for (i = 1; i < numTasks; i++)
                    {
                        GetProcessInfo(&slist[i]);
                    }
                    if (PtInRect(&rMain, pt))
                    {
                        char hStr[200];
                        sprintf(hStr, "Pkt  %ld/%ld\r\nRect %ld/%ld %ld/%ld\r\nMB  L=%d R=%d\r\nWSW  %d",
                                pt.x, pt.y,
                                rMain.left, rMain.right,
                                rMain.top, rMain.bottom,
                                x, y,
                                slist[i].is64);
                        MessageBox(hwnd, hStr, "Test", MB_OK);
                    }
                }
                break;

            }
            break;

        case WM_CONTEXTMENU:
        {
            POINT pt;

            if (GetActiveWindow() != hwnd_main)
            {
                //TODO
                return FALSE;
            }
            GetCursorPos(&pt);
            // TrackPopupMenu(hContext,TPM_RIGHTALIGN,LOWORD(lParam),HIWORD(lParam),0,hwnd,NULL);
            TrackPopupMenu(hContext, TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
            return TRUE;
        }
        break;

        case WM_LBUTTONUP:
            //MessageBox(hwnd,"Button L up","msg",MB_OK);
            break;

        case WM_RBUTTONUP:
        {
            POINT pt;

            if (GetActiveWindow() != hwnd_main)
            {
                //TODO
                return FALSE;
            }
            GetCursorPos(&pt);
            // TrackPopupMenu(hContext,TPM_RIGHTALIGN,LOWORD(lParam),HIWORD(lParam),0,hwnd,NULL);
            TrackPopupMenu(hContext, TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
            return TRUE;
        }
        break;

        case WM_LBUTTONDOWN:
            //MessageBox(hwnd,"Button L down","msg",MB_OK);
            break;

        case WM_RBUTTONDOWN:
            //MessageBox(hwnd,"Button R down","msg",MB_OK);
            break;

        case WM_TIMER:
        {
            //TODO
            static char old[sizeof(filt_name)] = "";
            if (wParam == IDT_CHANGED_MAIN)
            {
                //TODO
                char hStr[50];
                //TODO[Error]  'hStr' undeclared (first use in this function)[Error]  'hStr' undeclared (first use in this function)[Error]  'hStr' undeclared (first use in this function)
                size_t x = SendMessage(GetDlgItem(hwnd, IDC_MAIN_TEXT), LB_GETSELCOUNT, 0, 0);
                if (x != numSelected)
                {
                    //TODO
                    numSelected = x;
                    sprintf_s(hStr, sizeof(hStr), "%d/%d/%d ", numSelected, listTasks, numTasks);
                    SendMessage(hwnd_StatusBar, SB_SETTEXT, 0, (LPARAM)hStr);
                    return TRUE;
                }

                GetWindowText(hwnd_sedit, filt_name, sizeof(filt_name));
                if (strcmp(filt_name, old) != 0)
                {
                    GetProcessList(hwnd_main, FALSE);
                }

            }
            else if (wParam == IDT_ACTION)
            {
                char hStr[50];
                POINT pt;
                lParamPt konv;
                short l = GetAsyncKeyState(VK_LBUTTON);
                short r = GetAsyncKeyState(VK_RBUTTON);
                static short ll = 0, rr = 0;

                GetCursorPos(&pt);

                konv.pt.x = pt.x;
                konv.pt.y = pt.y;

                if (!PtInRect(&rMain, pt)) break;

                if (l != ll)
                {
                    if (l == 0)
                    {
                        SendMessage(hwnd, WM_LBUTTONUP, 0, konv.lparam);
                    }
                    else
                    {
                        SendMessage(hwnd, WM_LBUTTONDOWN, 0, konv.lparam);
                    }
                    ll = l;
                }
                if (r != rr)
                {
                    if (r == 0)
                    {
                        SendMessage(hwnd, WM_RBUTTONUP, 0, konv.lparam);
                    }
                    else
                    {
                        SendMessage(hwnd, WM_RBUTTONDOWN, 0, konv.lparam);
                    }
                    rr = r;
                }
                // set HScroll  for head line
                if (0)
                {
                    int pos = 0;
                    static int opos = 0;
                    pos = GetScrollPos(hwnd_client, SB_HORZ);
                    if (opos != pos)
                    {
                        RECT r;
                        GetWindowRect(hwnd_header, &r);
                        ScrollWindow(hwnd_header, opos - pos, 0, NULL, NULL);
                        opos = pos;
                    }
                }

                // set HScroll  for head line
                static int opos = 0;
                int pos = 0;
                pos = GetScrollPos(hwnd_client, SB_HORZ);
                if (opos != pos)
                {
                    RECT r;
                    GetWindowRect(hwnd_header, &r);
                    ScrollWindow(hwnd_header, opos - pos, 0, NULL, NULL);
                    opos = pos;
                }

                //TrackPopupMenu()
                sprintf_s(hStr, sizeof(hStr), "%d/%d/%c/%c/%c ", konv.pt.x, konv.pt.y, (l) ? 'L' : ' ', (r) ? 'R' : ' ', (TopMost) ? 135 : 32);
                SendMessage(hwnd_StatusBar, SB_SETTEXT, 3, (LPARAM)hStr);

                return TRUE;
            }
            else if (wParam == IDT_REFRESH)
            {
                char hStr[50];
                int cu = cpuusage();

                GetProcessList(hwnd_main, FALSE);

                sprintf_s(hStr, sizeof(hStr), "CPU:%3d.%d%%", cu / 10, cu % 10);
                SendMessage(hwnd_StatusBar, SB_SETTEXT, 4, (LPARAM)hStr);
            }
            break;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            break;
        case WM_DESTROY:
            KillTimer(hwnd, IDT_CHANGED_MAIN);
            KillTimer(hwnd, IDT_ACTION);
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, Message, wParam, lParam);
    }
    return 0;
}

void RestartAsAdmin(void)
{
    SHELLEXECUTEINFO Shex;
    HANDLE mod;
    char name[MAX_PATH];
    DWORD tid = 0;


    memset(&Shex, 0, sizeof(Shex));

    mod = GetModuleHandle(NULL);
    GetModuleFileName(mod, name, sizeof(name));
    CloseHandle(mod);
    Shex.cbSize = sizeof(Shex);
    Shex.fMask = SEE_MASK_NO_CONSOLE;
    Shex.lpVerb = "runas";
    Shex.lpFile = name;
    Shex.lpParameters = "";
    Shex.nShow = SW_SHOWNORMAL;

    ShellExecuteEx(&Shex);

    CreateThread(NULL, 0, WriteToPipe, sPipe, 0, &tid);
}

DWORD WINAPI WriteToPipe(void *pParam)
{
    HANDLE hPipe;
    char buffer[100];
    DWORD written;
    char *sPipe = (char *)pParam;

    hPipe = CreateNamedPipe(
            sPipe,
            PIPE_ACCESS_OUTBOUND,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE,
            1,
            sizeof(buffer),
            sizeof(buffer),
            5000,
            NULL
            );

    ConnectNamedPipe(hPipe, NULL);
    WriteFile(hPipe, &rMain, sizeof(rMain), &written, NULL);
    WriteFile(hPipe, &show, sizeof(show), NULL, NULL);
    iTopItem = SendMessage(hwnd_client, LB_GETTOPINDEX, 0, 0);
    WriteFile(hPipe, &iTopItem, sizeof(iTopItem), NULL, NULL);
    iCaretItem = SendMessage(hwnd_client, LB_GETCARETINDEX, 0, 0);
    WriteFile(hPipe, &iCaretItem, sizeof(iCaretItem), NULL, NULL);
    WriteFile(hPipe, filt_name, sizeof(filt_name ), NULL, NULL);
    WriteFile(hPipe, filt_pid, sizeof(filt_pid  ), NULL, NULL);
    WriteFile(hPipe, filt_title, sizeof(filt_title), NULL, NULL);
    CloseHandle(hPipe);

    if (sizeof(rMain) == written)
    {
        exit(0);
    }
    return 0;
}
void ReadFromPipe(void)
{
    HANDLE hPipe = CreateFile(
                   sPipe,
                   GENERIC_READ,
                   0,
                   NULL,
                   OPEN_EXISTING,
                   0,
                   NULL);

    ReadFile(hPipe, &rMain, sizeof(rMain), NULL, NULL);
    ReadFile(hPipe, &show, sizeof(show), NULL, NULL);
    ReadFile(hPipe, &iTopItem, sizeof(iTopItem), NULL, NULL);
    ReadFile(hPipe, &iCaretItem, sizeof(iCaretItem), NULL, NULL);
    ReadFile(hPipe, filt_name, sizeof(filt_name ), NULL, NULL);
    ReadFile(hPipe, filt_pid, sizeof(filt_pid  ), NULL, NULL);
    ReadFile(hPipe, filt_title, sizeof(filt_title), NULL, NULL);
    SetWindowText(hwnd_sedit, filt_name);

    CloseHandle(hPipe);
    hPipe = NULL;

}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSEX wc; /* A properties struct of our window */
    HWND hwnd; /* A 'HANDLE', hence the H, or a pointer to our window */
    MSG msg; /* A temporary location for all messages */
    HANDLE hAccelTable = NULL;

    ScanCmdLine(lpCmdLine);
    GetProcessList(NULL, FALSE);
    doCmdLine();

    ReadFromPipe();

    /* zero out the struct and set the stuff we want to modify */
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = 0;
    wc.lpfnWndProc = WndProc; /* This is where we will send messages to */
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    /* White, COLOR_WINDOW is just a #define for a system color, try Ctrl+Clicking it */
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = "MAINMENU";
    wc.lpszClassName = "KillProcess4Class";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(hInstance, IDI_APPLICATION); /* Load a standard icon */
    wc.hIconSm = LoadIcon(hInstance, IDI_APPLICATION); /* use the name "A" to use the project icon */

    g_hInst = hInstance;

    GetVersionString(NULL, versStr);

    if (!RegisterClassEx(&wc))
    {
        MessageBox(0, "Window Registration Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK | MB_SYSTEMMODAL);
        return 0;
    }

    hwnd = CreateWindowEx(WS_EX_CLIENTEDGE, "KillProcess4Class", "Kill Process 4", WS_OVERLAPPEDWINDOW,
                          CW_USEDEFAULT,
                          CW_USEDEFAULT,
                          745, 420,
                          NULL, NULL, hInstance, NULL);

    if (hwnd == NULL)
    {
        MessageBox(0, "Window Creation Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK | MB_SYSTEMMODAL);
        return 0;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(ID_TASTATUR));

    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        if (!TranslateAccelerator(hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

    }
    return msg.wParam;
}
