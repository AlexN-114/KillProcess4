#ifndef KILLPROCESS_H
#define KILLPROCESS_H

#include <windows.h>

#define TITLE_SIZE          64
#define PROCESS_SIZE        MAX_PATH


typedef struct _TASK_LIST
{
    DWORD       dwProcessId;
    DWORD       dwParentProcessId;
    DWORD       dwModuleId;
    DWORD       cntThreads;
    DWORD       dwInheritedFromProcessId;
    BOOL        flags;
    HANDLE      hwnd;
    CHAR        ProcessName[PROCESS_SIZE];
    CHAR        WindowTitle[TITLE_SIZE];
    char        ModulePath[MAX_PATH];
} TASK_LIST, *PTASK_LIST;

typedef struct _SHOW
{
    int pid;
    int parent_pid;
    int name;
    int path;
    int title;
    int cntThreads;
    int hwnd;
} SHOW, *PSHOW;

typedef struct _handle_data
{
    unsigned long process_id;
    HWND window_handle;
} handle_data;

typedef union
{
    LPARAM lparam;
    struct
    {
        WORD x;
        WORD y;
    } pt;
} lParamPt;

//-- Funktionsprototypen
char* GetVersionString(char *szModul, char *szVersion);
BOOL EnableDebugPrivNT( VOID );
char* GetVersionString(char *szModul, char *szVersion);
void RestartAsAdmin(void);
DWORD WINAPI WriteToPipe(void *pParam);
void ReadFromPipe(void);
int Kill(HWND hDlg);
int KillMulti(HWND hDlg);
BOOL KillProcess( PTASK_LIST tlist, BOOL fForce );
int CreateLine(TASK_LIST t, char *Line, int lng);
int CreateLineInfo(TASK_LIST t, char *Line, int lng);
static LRESULT CALLBACK WndProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK DlgProcFilter(HWND hwndEDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK DlgProcShow(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK DlgDisplay(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

#if WINVER < 0x0500
    #define sprintf_s(x,y,z,...) sprintf(x,z,##__VA_ARGS__)
    #define strcat_s(x,y,z) strcat(x,z)
    #define strcpy_s(x,y,z) strcpy(x,z)
#endif

#endif
