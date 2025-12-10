#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- 定义与配置 --- */
#define ID_LISTBOX 101
#define ID_BTN_RUN 102
#define ID_EDIT_LOG 103
#define ID_EDIT_FILTER 104
#define ID_BTN_ADD 105
#define ID_BTN_CLEAR 106

#define IDI_APP_ICON 1 

#define MAX_FILES 500
#define MAX_DOMAINS 5000
#define MAX_DOMAIN_LEN 256

/* --- UI 样式配置 --- */
#define COLOR_BG_R 242
#define COLOR_BG_G 242
#define COLOR_BG_B 242

#define FONT_SIZE 20 
#define BTN_HEIGHT 45
#define INPUT_HEIGHT 30
#define LABEL_HEIGHT 25

/* --- 全局变量 --- */
HWND hList, hEdit, hBtnRun, hBtnAdd, hBtnClear, hFilter;
HFONT hFontUI;       
HBRUSH hBrushBg;     

char filePaths[MAX_FILES][MAX_PATH];
int fileCount = 0;

char uniqueDomains[MAX_DOMAINS][MAX_DOMAIN_LEN];
int domainCount = 0;

/* --- 逻辑部分 --- */
int wild_match(const char *pattern, const char *string) {
    const char *p = pattern;
    const char *s = string;
    const char *star_p = NULL;
    const char *star_s = NULL;

    while (*s) {
        if (*p == '?' || *p == *s) { p++; s++; } 
        else if (*p == '*') { star_p = p++; star_s = s; } 
        else if (star_p) { p = star_p + 1; s = ++star_s; } 
        else return 0;
    }
    while (*p == '*') p++;
    return !*p;
}

void SetCtrlFont(HWND hCtrl) {
    SendMessage(hCtrl, WM_SETFONT, (WPARAM)hFontUI, TRUE);
}

void LogMessage(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    int len = GetWindowTextLength(hEdit);
    SendMessage(hEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(hEdit, EM_REPLACESEL, 0, (LPARAM)buffer);
    SendMessage(hEdit, EM_REPLACESEL, 0, (LPARAM)"\r\n");
}

/* --- 文件列表管理 --- */
void AddFileToList(const char* path) {
    if (fileCount >= MAX_FILES) {
        LogMessage("警告: 文件列表已满 (%d)", MAX_FILES);
        return;
    }
    for (int i = 0; i < fileCount; i++) {
        if (strcmp(filePaths[i], path) == 0) return;
    }
    strcpy(filePaths[fileCount], path);
    SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)path);
    fileCount++;
}

void OpenFileSelectionDialog(HWND hwnd) {
    OPENFILENAME ofn;
    char *szFile = (char*)malloc(32768); 
    if (!szFile) return;
    
    ZeroMemory(szFile, 32768);
    ZeroMemory(&ofn, sizeof(ofn));

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = 32768;
    ofn.lpstrFilter = "文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

    if (GetOpenFileName(&ofn)) {
        char *p = szFile;
        char dirPath[MAX_PATH];
        strcpy(dirPath, p);
        p += strlen(p) + 1;

        if (*p == 0) {
            AddFileToList(dirPath);
            LogMessage("添加: %s", dirPath);
        } else {
            while (*p) {
                char fullPath[MAX_PATH];
                if (dirPath[strlen(dirPath)-1] != '\\')
                    sprintf(fullPath, "%s\\%s", dirPath, p);
                else
                    sprintf(fullPath, "%s%s", dirPath, p);
                AddFileToList(fullPath);
                p += strlen(p) + 1;
            }
            LogMessage("批量添加完成。");
        }
    }
    free(szFile);
}

void ClearFileList() {
    SendMessage(hList, LB_RESETCONTENT, 0, 0);
    fileCount = 0;
    LogMessage("列表已清空。");
}

/* --- 提取核心逻辑 --- */
int IsDuplicate(const char* domain) {
    for (int i = 0; i < domainCount; ++i) {
        if (strcmp(uniqueDomains[i], domain) == 0) return 1;
    }
    return 0;
}

int ShouldFilter(const char* domain) {
    char filterBuf[1024];
    GetWindowText(hFilter, filterBuf, sizeof(filterBuf));
    if (strlen(filterBuf) == 0) return 0;

    char *token = strtok(filterBuf, ";"); 
    while (token != NULL) {
        while(*token == ' ') token++;
        if (wild_match(token, domain)) {
            LogMessage("[过滤] 已剔除: %s", domain);
            return 1;
        }
        token = strtok(NULL, ";");
    }
    return 0;
}

void ExtractKeys(char* content, const char* key) {
    char* pos = content;
    int keyLen = strlen(key);
    while ((pos = strstr(pos, key)) != NULL) {
        char* start = pos + keyLen;
        char* end = start;
        while (*end && *end != '&' && *end != '#' && *end != '\r' && *end != '\n' && *end != ' ') end++;
        int len = end - start;
        if (len > 0 && len < MAX_DOMAIN_LEN - 1) {
            char temp[MAX_DOMAIN_LEN];
            strncpy(temp, start, len);
            temp[len] = '\0';
            if (!IsDuplicate(temp) && !ShouldFilter(temp)) {
                if (domainCount < MAX_DOMAINS) strcpy(uniqueDomains[domainCount++], temp);
            }
        }
        pos = end;
    }
}

void ProcessFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { LogMessage("跳过(无法读取): %s", path); return; }
    fseek(f, 0, SEEK_END); long fsize = ftell(f); fseek(f, 0, SEEK_SET);
    if (fsize <= 0) { fclose(f); return; }
    char* buffer = (char*)malloc(fsize + 1);
    if (!buffer) { fclose(f); return; }
    fread(buffer, 1, fsize, f); buffer[fsize] = '\0'; fclose(f);
    LogMessage("分析中: %s", path);
    ExtractKeys(buffer, "sni="); ExtractKeys(buffer, "host=");
    free(buffer);
}

void StartExtraction() {
    if (fileCount == 0) { MessageBox(NULL, "请先添加文件！", "提示", MB_OK | MB_ICONWARNING); return; }
    domainCount = 0; SetWindowText(hEdit, ""); 
    for (int i = 0; i < fileCount; ++i) ProcessFile(filePaths[i]);
    
    if (domainCount > 0) {
        FILE* f = fopen("extracted_domains.txt", "w");
        if (f) {
            for(int i=0; i<domainCount-1; i++) {
                for(int j=0; j<domainCount-i-1; j++) {
                    if(strcmp(uniqueDomains[j], uniqueDomains[j+1]) > 0) {
                        char temp[MAX_DOMAIN_LEN];
                        strcpy(temp, uniqueDomains[j]); 
                        strcpy(uniqueDomains[j], uniqueDomains[j+1]); 
                        strcpy(uniqueDomains[j+1], temp);
                    }
                }
            }
            for (int i = 0; i < domainCount; ++i) fprintf(f, "%s\n", uniqueDomains[i]);
            fclose(f);
            LogMessage("---------- 完成 ----------");
            LogMessage("已提取: %d 个地址", domainCount);
            MessageBox(NULL, "提取完成！\n结果已保存至 extracted_domains.txt", "成功", MB_OK | MB_ICONINFORMATION);
        }
    } else {
        LogMessage("未找到有效数据。");
    }
}

/* --- Win32 GUI 消息处理 --- */
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        hFontUI = CreateFont(FONT_SIZE, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, 
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
                             DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Microsoft YaHei");
        hBrushBg = CreateSolidBrush(RGB(COLOR_BG_R, COLOR_BG_G, COLOR_BG_B));

        {
            HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;
            HICON hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_APP_ICON));
            if(hIcon) {
                SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
                SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
            }
        }

        /* --- 界面布局 (已移除 Emoji) --- */
        
        // 1. 列表区
        HWND hLabel1 = CreateWindow("STATIC", "待处理文件 (支持拖拽/多选):", WS_VISIBLE | WS_CHILD, 20, 15, 300, LABEL_HEIGHT, hwnd, NULL, NULL, NULL);
        SetCtrlFont(hLabel1);

        hList = CreateWindowEx(WS_EX_CLIENTEDGE, "LISTBOX", "", 
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 
            20, 45, 360, 220, hwnd, (HMENU)ID_LISTBOX, NULL, NULL);
        SetCtrlFont(hList);
        
        // 2. 右侧按钮区
        int btnX = 400;
        int btnW = 160;
        
        // [修改处] 移除了 Emoji
        hBtnAdd = CreateWindow("BUTTON", "添加文件...", 
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_FLAT, 
            btnX, 45, btnW, BTN_HEIGHT, hwnd, (HMENU)ID_BTN_ADD, NULL, NULL);
        SetCtrlFont(hBtnAdd);

        // [修改处] 移除了 Emoji
        hBtnClear = CreateWindow("BUTTON", "清空列表", 
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_FLAT, 
            btnX, 100, btnW, BTN_HEIGHT, hwnd, (HMENU)ID_BTN_CLEAR, NULL, NULL);
        SetCtrlFont(hBtnClear);

        // [修改处] 移除了 Emoji
        hBtnRun = CreateWindow("BUTTON", "开始提取", 
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON | BS_FLAT, 
            btnX, 220, btnW, BTN_HEIGHT, hwnd, (HMENU)ID_BTN_RUN, NULL, NULL);
        SetCtrlFont(hBtnRun);

        // 3. 过滤区
        HWND hLabel2 = CreateWindow("STATIC", "排除规则 (* 通配符):", WS_VISIBLE | WS_CHILD, 20, 280, 400, LABEL_HEIGHT, hwnd, NULL, NULL, NULL);
        SetCtrlFont(hLabel2);

        hFilter = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "*.workers.dev;*google*", 
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 
            20, 310, 540, INPUT_HEIGHT, hwnd, (HMENU)ID_EDIT_FILTER, NULL, NULL);
        SetCtrlFont(hFilter);

        // 4. 日志区
        HWND hLabel3 = CreateWindow("STATIC", "运行日志:", WS_VISIBLE | WS_CHILD, 20, 350, 300, LABEL_HEIGHT, hwnd, NULL, NULL, NULL);
        SetCtrlFont(hLabel3);

        hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", 
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 
            20, 380, 540, 160, hwnd, (HMENU)ID_EDIT_LOG, NULL, NULL);
        SetCtrlFont(hEdit);

        DragAcceptFiles(hwnd, TRUE);
        break;

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(50, 50, 50)); SetBkMode(hdc, TRANSPARENT); return (LRESULT)hBrushBg;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
        SetTextColor((HDC)wParam, RGB(0, 0, 0)); SetBkMode((HDC)wParam, OPAQUE); return (LRESULT)GetStockObject(WHITE_BRUSH);
    }

    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        int count = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);
        for (int i = 0; i < count; i++) {
            char path[MAX_PATH];
            DragQueryFile(hDrop, i, path, MAX_PATH);
            AddFileToList(path);
        }
        LogMessage("拖拽添加 %d 个项目。", count);
        DragFinish(hDrop);
        break;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BTN_ADD) OpenFileSelectionDialog(hwnd);
        if (LOWORD(wParam) == ID_BTN_CLEAR) ClearFileList();
        if (LOWORD(wParam) == ID_BTN_RUN) StartExtraction();
        break;

    case WM_DESTROY:
        DeleteObject(hFontUI); DeleteObject(hBrushBg); PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc = {0};
    wc.lpszClassName = "CleanApp";
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = CreateSolidBrush(RGB(COLOR_BG_R, COLOR_BG_G, COLOR_BG_B));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClass(&wc)) return -1;

    HWND hwnd = CreateWindow("CleanApp", "网址提取工具 (专业版)", 
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, 
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 600, NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}