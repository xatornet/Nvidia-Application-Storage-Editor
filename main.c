#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellscalingapi.h>
#include <stdio.h>
#include "cJSON.h"

#pragma comment(lib, "shcore.lib")

#define ID_LISTBOX 1001
#define ID_SAVE    1002
#define ID_RESTORE 1003
#define ID_OPENAPP 1004
#define ID_CHECK1  2001
#define ID_CHECK2  2002
#define ID_CHECK3  2003
#define ID_CHECK4  2004
#define ID_CHECK5  2005

char jsonPath[MAX_PATH];
char backupPath[MAX_PATH];
char iniPath[MAX_PATH];
cJSON *root = NULL;
cJSON *currentApp = NULL;
HWND hList, hChecks[5], hRestoreBtn, hSaveBtn, hOpenBtn;
HFONT hFont;

const char *keys[5] = {
    "Disable_FG_Override",
    "Disable_RR_Override",
    "Disable_SR_Override",
    "Disable_RR_Model_Override",
    "Disable_SR_Model_Override"
};

const char *labels[5] = {
    "Habilitar DLSS-G Override",
    "Habilitar DLSS Ray Reconstruction Override",
    "Habilitar DLSS Override",
    "Habilitar Ray Reconstruction Model Override",
    "Habilitar DLSS Model Override"
};

// -----------------------------------------------------------
// Utilidades básicas
// -----------------------------------------------------------

BOOL FileExists(const char *path) {
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

BOOL CopyFileSimple(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return FALSE;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return FALSE; }

    char buffer[4096];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), in)) > 0)
        fwrite(buffer, 1, bytes, out);

    fclose(in);
    fclose(out);
    return TRUE;
}

char *ReadFileToString(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buf = malloc(size + 1);
    fread(buf, 1, size, f);
    buf[size] = 0;
    fclose(f);
    return buf;
}

void WriteStringToFile(const char *path, const char *data) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(data, 1, strlen(data), f);
    fclose(f);
}

// -----------------------------------------------------------
// Paths y JSON
// -----------------------------------------------------------

void GetPaths() {
    char localAppData[MAX_PATH];
    char appData[MAX_PATH];
    SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData);
    SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appData);
    sprintf(jsonPath, "%s\\NVIDIA Corporation\\NVIDIA App\\NvBackend\\ApplicationStorage.json", localAppData);
    sprintf(backupPath, "%s\\NVIDIA Corporation\\NVIDIA App\\NvBackend\\ApplicationStorage_backup.json", localAppData);
    sprintf(iniPath, "%s\\NvConfigEditor\\config.ini", appData);

    char dir[MAX_PATH];
    strcpy(dir, iniPath);
    *strrchr(dir, '\\') = '\0';
    CreateDirectoryA(dir, NULL);
}

void LoadJSON() {
    if (root) {
        cJSON_Delete(root);
        root = NULL;
    }
    char *data = ReadFileToString(jsonPath);
    if (!data) {
        MessageBox(NULL, "No se pudo leer el archivo JSON.", "Error", MB_ICONERROR);
        return;
    }
    root = cJSON_Parse(data);
    free(data);
    if (!root) {
        MessageBox(NULL, "Error al parsear el archivo JSON.", "Error", MB_ICONERROR);
    }
}

void BackupIfNeeded(HWND hwnd) {
    if (!FileExists(backupPath)) {
        if (CopyFileSimple(jsonPath, backupPath)) {
            MessageBox(hwnd, "Se ha creado una copia de seguridad inicial del JSON.", "Backup creado", MB_OK | MB_ICONINFORMATION);
        }
    }
    EnableWindow(hRestoreBtn, FileExists(backupPath));
}

void RestoreFromBackup(HWND hwnd) {
    if (!FileExists(backupPath)) {
        MessageBox(hwnd, "No existe copia de seguridad previa.", "Error", MB_ICONERROR);
        return;
    }

    int confirm = MessageBox(hwnd, "¿Deseas restaurar el archivo original desde la copia de seguridad?\nSe perderán los cambios hechos.", "Confirmar restauración", MB_YESNO | MB_ICONQUESTION);
    if (confirm != IDYES) return;

    if (CopyFileSimple(backupPath, jsonPath)) {
        LoadJSON();
        SendMessage(hList, LB_RESETCONTENT, 0, 0);
        cJSON *apps = cJSON_GetObjectItem(root, "Applications");
        if (apps) {
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, apps) {
                cJSON *app = cJSON_GetObjectItem(item, "Application");
                cJSON *name = cJSON_GetObjectItem(app, "DisplayName");
                if (name && cJSON_IsString(name))
                    SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)name->valuestring);
            }
        }
        MessageBox(hwnd, "Archivo restaurado correctamente desde el backup.", "Restaurado", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBox(hwnd, "No se pudo restaurar la copia de seguridad.", "Error", MB_ICONERROR);
    }
}

// -----------------------------------------------------------
// UI
// -----------------------------------------------------------

void PopulateList() {
    if (!root) return;
    SendMessage(hList, LB_RESETCONTENT, 0, 0);

    cJSON *apps = cJSON_GetObjectItem(root, "Applications");
    if (!apps) return;

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, apps) {
        cJSON *app = cJSON_GetObjectItem(item, "Application");
        cJSON *name = cJSON_GetObjectItem(app, "DisplayName");
        if (name && cJSON_IsString(name))
            SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)name->valuestring);
    }
}

void UpdateChecks(const char *name) {
    cJSON *apps = cJSON_GetObjectItem(root, "Applications");
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, apps) {
        cJSON *app = cJSON_GetObjectItem(item, "Application");
        cJSON *display = cJSON_GetObjectItem(app, "DisplayName");
        if (display && strcmp(display->valuestring, name) == 0) {
            currentApp = app;
            for (int i = 0; i < 5; i++) {
                cJSON *flag = cJSON_GetObjectItem(app, keys[i]);
                BOOL checked = (flag && cJSON_IsFalse(flag)) ? BST_CHECKED : BST_UNCHECKED;
                SendMessage(hChecks[i], BM_SETCHECK, checked, 0);
            }
            return;
        }
    }
}

void SaveChanges() {
    if (!currentApp) {
        MessageBox(NULL, "No hay ninguna aplicación seleccionada.", "Aviso", MB_ICONWARNING);
        return;
    }

    for (int i = 0; i < 5; i++) {
        BOOL checked = (SendMessage(hChecks[i], BM_GETCHECK, 0, 0) == BST_CHECKED);
        cJSON_ReplaceItemInObject(currentApp, keys[i], cJSON_CreateBool(!checked));
    }

    char *out = cJSON_Print(root);
    WriteStringToFile(jsonPath, out);
    free(out);
    MessageBox(NULL, "Cambios guardados correctamente.", "Guardado", MB_OK | MB_ICONINFORMATION);
}

// -----------------------------------------------------------
// Servicio + abrir NVIDIA App
// -----------------------------------------------------------

BOOL RestartService(const char *serviceName) {
    SC_HANDLE scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) return FALSE;

    SC_HANDLE svc = OpenServiceA(scm, serviceName, SERVICE_STOP | SERVICE_START | SERVICE_QUERY_STATUS);
    if (!svc) { CloseServiceHandle(scm); return FALSE; }

    SERVICE_STATUS status;
    ControlService(svc, SERVICE_CONTROL_STOP, &status);
    Sleep(3000);
    BOOL started = StartService(svc, 0, NULL);

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return started;
}

void OpenNvidiaApp(HWND hwnd) {
    const char *path = "C:\\Program Files\\NVIDIA Corporation\\NVIDIA App\\CEF\\NVIDIA App.exe";

    MessageBox(hwnd, "Reiniciando el servicio 'NvContainerLocalSystem' antes de abrir NVIDIA App...", "Espere", MB_OK | MB_ICONINFORMATION);
    RestartService("NvContainerLocalSystem");
    Sleep(1500);

    if (!FileExists(path)) {
        MessageBox(hwnd, "No se encontró la ruta de NVIDIA App.", "Error", MB_ICONERROR);
        return;
    }

    ShellExecute(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL);
    PostQuitMessage(0);
}

// -----------------------------------------------------------
// Guardar/restaurar geometría
// -----------------------------------------------------------

void SaveWindowPlacement(HWND hwnd) {
    RECT r;
    GetWindowRect(hwnd, &r);

    char section[] = "Window";
    char buf[64];
    sprintf(buf, "%ld", r.left);
    WritePrivateProfileStringA(section, "Left", buf, iniPath);
    sprintf(buf, "%ld", r.top);
    WritePrivateProfileStringA(section, "Top", buf, iniPath);
    sprintf(buf, "%ld", r.right - r.left);
    WritePrivateProfileStringA(section, "Width", buf, iniPath);
    sprintf(buf, "%ld", r.bottom - r.top);
    WritePrivateProfileStringA(section, "Height", buf, iniPath);
}

BOOL LoadWindowPlacement(int *x, int *y, int *w, int *h) {
    *x = GetPrivateProfileIntA("Window", "Left", CW_USEDEFAULT, iniPath);
    *y = GetPrivateProfileIntA("Window", "Top", CW_USEDEFAULT, iniPath);
    *w = GetPrivateProfileIntA("Window", "Width", 560, iniPath);
    *h = GetPrivateProfileIntA("Window", "Height", 400, iniPath);
    return TRUE;
}

// -----------------------------------------------------------
// Layout dinámico
// -----------------------------------------------------------

void ResizeLayout(HWND hwnd, int width, int height) {
    MoveWindow(hList, 10, 10, width / 2 - 20, height - 80, TRUE);

    int rightX = width / 2 + 10;
    for (int i = 0; i < 5; i++) {
        MoveWindow(hChecks[i], rightX, 30 + (i * 30), width / 2 - 30, 25, TRUE);
    }

    MoveWindow(hSaveBtn, rightX, height - 110, 120, 35, TRUE);
    MoveWindow(hRestoreBtn, rightX + 130, height - 110, 120, 35, TRUE);
    MoveWindow(hOpenBtn, rightX, height - 60, width / 2 - 30, 35, TRUE);
}

// -----------------------------------------------------------
// UI helper
// -----------------------------------------------------------

void ApplyFont(HWND hWnd, HFONT hFont) {
    SendMessage(hWnd, WM_SETFONT, (WPARAM)hFont, TRUE);
}

// -----------------------------------------------------------
// Window Procedure
// -----------------------------------------------------------

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        hList = CreateWindow("LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_BORDER | WS_VSCROLL | LBS_DISABLENOSCROLL,
            10, 10, 200, 300, hwnd, (HMENU)ID_LISTBOX, NULL, NULL);
        ApplyFont(hList, hFont);

        for (int i = 0; i < 5; i++) {
            hChecks[i] = CreateWindow("BUTTON", labels[i],
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                230, 30 + (i * 30), 280, 25, hwnd, (HMENU)(intptr_t)(ID_CHECK1 + i), NULL, NULL);
            ApplyFont(hChecks[i], hFont);
        }

        hSaveBtn = CreateWindow("BUTTON", "Guardar",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            230, 220, 120, 35, hwnd, (HMENU)ID_SAVE, NULL, NULL);
        ApplyFont(hSaveBtn, hFont);

        hRestoreBtn = CreateWindow("BUTTON", "Restaurar todo",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            360, 220, 120, 35, hwnd, (HMENU)ID_RESTORE, NULL, NULL);
        ApplyFont(hRestoreBtn, hFont);

        hOpenBtn = CreateWindow("BUTTON", "Abrir NVIDIA App",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            230, 270, 250, 35, hwnd, (HMENU)ID_OPENAPP, NULL, NULL);
        ApplyFont(hOpenBtn, hFont);

        GetPaths();
        BackupIfNeeded(hwnd);
        LoadJSON();
        PopulateList();
        break;

    case WM_SIZE:
        ResizeLayout(hwnd, LOWORD(lParam), HIWORD(lParam));
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_LISTBOX && HIWORD(wParam) == LBN_SELCHANGE) {
            int sel = SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                char name[128];
                SendMessage(hList, LB_GETTEXT, sel, (LPARAM)name);
                UpdateChecks(name);
            }
        } else if (LOWORD(wParam) == ID_SAVE) {
            SaveChanges();
        } else if (LOWORD(wParam) == ID_RESTORE) {
            RestoreFromBackup(hwnd);
        } else if (LOWORD(wParam) == ID_OPENAPP) {
            OpenNvidiaApp(hwnd);
        }
        break;

    case WM_DESTROY:
        SaveWindowPlacement(hwnd);
        if (root) cJSON_Delete(root);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// -----------------------------------------------------------
// MAIN - Con soporte High DPI
// -----------------------------------------------------------

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nCmdShow) {
    // DPI awareness setup
    typedef BOOL(WINAPI *SetProcessDpiAwarenessContextProc)(HANDLE);
    typedef HRESULT(WINAPI *SetProcessDpiAwarenessProc)(int);

    HMODULE hUser32 = LoadLibraryA("user32.dll");
    if (hUser32) {
        SetProcessDpiAwarenessContextProc SetCtx = (SetProcessDpiAwarenessContextProc)
            GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (SetCtx) {
            SetCtx((HANDLE)-4); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        } else {
            HMODULE hShcore = LoadLibraryA("shcore.dll");
            if (hShcore) {
                SetProcessDpiAwarenessProc SetAwareness =
                    (SetProcessDpiAwarenessProc)GetProcAddress(hShcore, "SetProcessDpiAwareness");
                if (SetAwareness)
                    SetAwareness(2); // PROCESS_PER_MONITOR_DPI_AWARE
                FreeLibrary(hShcore);
            } else {
                SetProcessDPIAware();
            }
        }
        FreeLibrary(hUser32);
    }

    // Obtener factor de escala actual
    UINT dpi = 96;
    HDC hdc = GetDC(NULL);
    if (hdc) {
        dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(NULL, hdc);
    }
    double scale = (double)dpi / 96.0;

    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icex);

    LOGFONT lf;
    SystemParametersInfo(SPI_GETICONTITLELOGFONT, sizeof(lf), &lf, 0);
    hFont = CreateFontIndirect(&lf);

    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "NvJSONEditor";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClass(&wc);

    int x, y, w, h;
    GetPaths();
    LoadWindowPlacement(&x, &y, &w, &h);

    w = (int)(w * scale);
    h = (int)(h * scale);

    HWND hwnd = CreateWindow("NvJSONEditor", "NVIDIA Application Storage Editor",
        WS_OVERLAPPEDWINDOW,
        x, y, w, h, NULL, NULL, hInst, NULL);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DeleteObject(hFont);
    return msg.wParam;
}
