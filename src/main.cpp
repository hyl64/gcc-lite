// ============================================================================
//  main.cpp — 斗战者控制台 · 轻量版（纯 Win32 C++）
//
//  功能：
//    1. 四个性能模式切换（均衡 / 性能 / 静音 / 全速）
//    2. 风扇自由调速 + 实时转速/温度显示
//    3. 时间段调度：HH:MM - HH:MM → 自动处于某模式（最多 6 条规则）
//    4. 实时显示：电池电压 / 充放电功率 / 整机功率
//
//  硬件通路：root\WMI : MICommonInterface.MiInterface（ACPI-WMI）
//  协议：buf[1]=Get(250)/Set(251)，buf[3]=方法号，buf[4]=值
//        来源：逆向 斗战者控制台.dll 的 _MakeMethodPrams
// ============================================================================
#include <windows.h>
#include <commctrl.h>
#include <cstdio>
#include <string>
#include <vector>

#include "MiBridge.h"
#include "PowerInfo.h"
#include "Schedule.h"
#include "Sensors.h"
#include "EnergyMeter.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ---- 控件 ID ----
enum {
    IDC_MODE_BALANCE = 1001,
    IDC_MODE_PERF,
    IDC_MODE_QUIET,
    IDC_MODE_FULL,
    IDC_FAN_SLIDER = 1100,
    IDC_FAN_TEXT,
    IDC_RPM_TEXT,
    IDC_TEMP_TEXT,
    IDC_FAN_APPLY,
    IDC_MAXFAN_CHECK,
    IDC_SENSORS_TEXT,
    IDC_POWER_TEXT,
    IDC_STATUS = 1300,

    IDC_SCHED_BASE  = 2000,      // + row*10 + field
    IDC_SCHED_ONOFF = 4010,
    IDC_SCHED_INFO  = 4020,

    TIMER_SENSORS = 3000,
    TIMER_SCHED = 3001,
    TIMER_UI    = 3002,
};

enum { OFF_ENABLE = 0, OFF_START, OFF_END, OFF_MODE };

static const int kRowH = 30;

// ---- 全局 ----
static HINSTANCE g_hInst = nullptr;
static HWND      g_hMain = nullptr;
static HFONT     g_hFont = nullptr;

static gcc::MiBridge    g_bridge;
static gcc::CPdhSensors  g_sensors;
static gcc::CEnergyMeter g_energy;
static bool             g_energyOk = false;
static bool             g_sensorsOk = false;
static bool          g_bridgeOk = false;
static std::string   g_bridgeErr;

static std::vector<gcc::ScheduleRule> g_rules;
static bool g_schedEnabled    = false;
static int  g_lastAppliedRule = -99;
static bool g_uiReady         = false;

static HWND g_hStatus    = nullptr;
static HWND g_hPower     = nullptr;
static HWND g_hSchedInfo = nullptr;

// ---------------------------------------------------------------------------
static void SetFont(HWND h, HFONT f = nullptr) {
    SendMessage(h, WM_SETFONT, (WPARAM)(f ? f : g_hFont), TRUE);
}
static HWND MakeButton(HWND p, const wchar_t* t, int id, int x, int y, int w, int h) {
    HWND hw = CreateWindowEx(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                             x, y, w, h, p, (HMENU)(INT_PTR)id, g_hInst, nullptr);
    SetFont(hw); return hw;
}
static HWND MakeGroup(HWND p, const wchar_t* t, int x, int y, int w, int h) {
    HWND hw = CreateWindowEx(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                             x, y, w, h, p, nullptr, g_hInst, nullptr);
    SetFont(hw); return hw;
}
static HWND MakeStatic(HWND p, const wchar_t* t, int x, int y, int w, int h,
                       int id = 0, DWORD style = SS_LEFT) {
    HWND hw = CreateWindowEx(0, L"STATIC", t, WS_CHILD | WS_VISIBLE | style,
                             x, y, w, h, p, (HMENU)(INT_PTR)id, g_hInst, nullptr);
    SetFont(hw); return hw;
}
static HWND MakeEdit(HWND p, const wchar_t* t, int id, int x, int y, int w, int h) {
    HWND hw = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", t,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_CENTER,
        x, y, w, h, p, (HMENU)(INT_PTR)id, g_hInst, nullptr);
    SetFont(hw); return hw;
}
static HWND MakeCheck(HWND p, const wchar_t* t, int id, int x, int y, int w, int h) {
    HWND hw = CreateWindowEx(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                             x, y, w, h, p, (HMENU)(INT_PTR)id, g_hInst, nullptr);
    SetFont(hw); return hw;
}
static int SchedId(int row, int field) { return IDC_SCHED_BASE + row * 10 + field; }

static const wchar_t* ModeName(uint8_t m) {
    switch (m) {
        case gcc::kSchedBalance:     return L"均衡";
        case gcc::kSchedPerformance: return L"性能";
        case gcc::kSchedQuiet:       return L"静音";
        case gcc::kSchedFullspeed:   return L"全速";
        default:                     return L"未知";
    }
}

// ---------------------------------------------------------------------------
//  界面 <-> 规则
// ---------------------------------------------------------------------------
static bool ReadRuleFromUI(int row, gcc::ScheduleRule& r) {
    wchar_t buf[32];
    r.enabled = (SendDlgItemMessage(g_hMain, SchedId(row, OFF_ENABLE),
                                    BM_GETCHECK, 0, 0) == BST_CHECKED);

    GetDlgItemText(g_hMain, SchedId(row, OFF_START), buf, 32);
    int s = gcc::ParseMinutes(buf);
    if (s < 0) return false;
    r.startMin = s;

    GetDlgItemText(g_hMain, SchedId(row, OFF_END), buf, 32);
    int e = gcc::ParseMinutes(buf);
    if (e < 0) return false;
    r.endMin = e;

    int sel = (int)SendDlgItemMessage(g_hMain, SchedId(row, OFF_MODE), CB_GETCURSEL, 0, 0);
    if (sel < 0) sel = 0;
    r.mode = (uint8_t)sel;
    return true;
}
static void WriteRuleToUI(int row, const gcc::ScheduleRule& r) {
    HWND hEnable = GetDlgItem(g_hMain, SchedId(row, OFF_ENABLE));
    if (hEnable)
        SendMessage(hEnable, BM_SETCHECK, r.enabled ? BST_CHECKED : BST_UNCHECKED, 0);

    std::wstring sStart = gcc::FmtMinutes(r.startMin);
    std::wstring sEnd   = gcc::FmtMinutes(r.endMin);

    HWND hStart = GetDlgItem(g_hMain, SchedId(row, OFF_START));
    if (hStart) SetWindowTextW(hStart, sStart.c_str());

    HWND hEnd = GetDlgItem(g_hMain, SchedId(row, OFF_END));
    if (hEnd) SetWindowTextW(hEnd, sEnd.c_str());

    HWND hMode = GetDlgItem(g_hMain, SchedId(row, OFF_MODE));
    if (hMode) SendMessage(hMode, CB_SETCURSEL, r.mode, 0);
}
static void PullAllRulesFromUI() {
    for (int i = 0; i < (int)g_rules.size(); ++i) {
        gcc::ScheduleRule r;
        if (ReadRuleFromUI(i, r)) g_rules[i] = r;
    }
}

// ---------------------------------------------------------------------------
//  调度
// ---------------------------------------------------------------------------
static void ApplyMode(uint8_t mode, const wchar_t* why) {
    if (!g_bridgeOk) return;
    std::string err;
    if (g_bridge.SetPerfMode(mode, err)) {
        wchar_t buf[256];
        swprintf_s(buf, _countof(buf), L"已切换到「%s」模式（%s）", ModeName(mode), why);
        SetWindowText(g_hStatus, buf);
        int id = IDC_MODE_BALANCE;
        switch (mode) {
            case gcc::kSchedBalance:     id = IDC_MODE_BALANCE; break;
            case gcc::kSchedPerformance: id = IDC_MODE_PERF;    break;
            case gcc::kSchedQuiet:       id = IDC_MODE_QUIET;   break;
            case gcc::kSchedFullspeed:   id = IDC_MODE_FULL;    break;
        }
        CheckRadioButton(g_hMain, IDC_MODE_BALANCE, IDC_MODE_FULL, id);
    }
}

static void CheckSchedule() {
    if (!g_hMain || !g_hSchedInfo) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    int nowMin = st.wHour * 60 + st.wMinute;
    wchar_t buf[512];

    if (!g_schedEnabled) {
        swprintf_s(buf, _countof(buf),
                   L"未启用            当前时间 %02d:%02d", st.wHour, st.wMinute);
        SetWindowText(g_hSchedInfo, buf);
        return;
    }

    uint8_t mode = 0;
    int idx = -1;
    if (gcc::FindActiveMode(g_rules, nowMin, mode, idx)) {
        swprintf_s(buf, _countof(buf),
            L"生效中 → 「%s」  规则%d: %s–%s            当前 %02d:%02d",
            ModeName(mode), idx + 1,
            gcc::FmtMinutes(g_rules[idx].startMin).c_str(),
            gcc::FmtMinutes(g_rules[idx].endMin).c_str(),
            st.wHour, st.wMinute);
        SetWindowText(g_hSchedInfo, buf);

        if (idx != g_lastAppliedRule) {
            ApplyMode(mode, L"时间段");
            g_lastAppliedRule = idx;
        }
    } else {
        swprintf_s(buf, _countof(buf),
            L"当前无匹配规则（保持现状）            当前 %02d:%02d",
            st.wHour, st.wMinute);
        SetWindowText(g_hSchedInfo, buf);
        g_lastAppliedRule = -1;
    }
}

// ---------------------------------------------------------------------------
//  硬件 / 功率刷新
// ---------------------------------------------------------------------------
static void RefreshHardware() {
    if (!g_bridgeOk || !g_hMain) return;
    std::string err;
    uint8_t v = 0;
    wchar_t buf[256];

    if (g_bridge.GetPerfMode(v, err)) {
        int id = IDC_MODE_BALANCE;
        switch (v) {
            case 0: id = IDC_MODE_BALANCE; break;
            case 1: id = IDC_MODE_PERF;    break;
            case 2: id = IDC_MODE_QUIET;   break;
            case 3: id = IDC_MODE_FULL;    break;
            default: id = 0; break;
        }
        if (id) CheckRadioButton(g_hMain, IDC_MODE_BALANCE, IDC_MODE_FULL, id);
    }

    gcc::MiBridge::FanRpm rpm;
    if (g_bridge.GetFanRpm(rpm, err)) {
        swprintf_s(buf, _countof(buf), L"实测转速：CPU %d / GPU %d RPM", rpm.cpu, rpm.gpu);
        SetDlgItemText(g_hMain, IDC_RPM_TEXT, buf);
    }

    if (g_bridge.GetCpuTemp(v, err)) {
        swprintf_s(buf, _countof(buf), L"CPU 温度：%u °C", v);
        SetDlgItemText(g_hMain, IDC_TEMP_TEXT, buf);
    }
}

static void UpdateSensors() {
    if (!g_hMain) return;
    gcc::SysSensors s;
    if (!g_sensorsOk || !g_sensors.Sample(s)) {
        SetDlgItemText(g_hMain, IDC_SENSORS_TEXT, L"监控数据不可用");
        return;
    }

    wchar_t buf[512];
    wchar_t gpuTxt[64], speedTxt[64], pwTxt[64];

    if (s.gpuUsagePct >= 0.0)
        swprintf_s(gpuTxt, _countof(gpuTxt), L"%.1f %%", s.gpuUsagePct);
    else
        wcscpy_s(gpuTxt, L"--");

    if (s.cpuSpeedMhz > 0.0)
        swprintf_s(speedTxt, _countof(speedTxt), L"%.2f GHz", s.cpuSpeedMhz / 1000.0);
    else
        wcscpy_s(speedTxt, L"--");

    // CPU 功耗（RAPL）
    static double lastCpuW = -1.0;
    if (g_energyOk) {
        gcc::PowerReading pr;
        if (g_energy.SampleCpu(pr) && pr.raplValid)
            lastCpuW = pr.cpuPackageW;
    }
    if (lastCpuW >= 0.0)
        swprintf_s(pwTxt, _countof(pwTxt), L"%.1f W", lastCpuW);
    else
        wcscpy_s(pwTxt, L"--");

    swprintf_s(buf, _countof(buf),
        L"CPU 占用率：%.1f %%        频率：%s\r\n"
        L"GPU 占用率：%s\r\n"
        L"内存占用率：%.1f %%        %.1f / %.1f GB\r\n"
        L"CPU 功耗：%s",
        s.cpuUsagePct, speedTxt,
        gpuTxt,
        s.memUsagePct,
        s.memUsedBytes / 1073741824.0,
        s.memTotalBytes / 1073741824.0,
        pwTxt);

    SetDlgItemText(g_hMain, IDC_SENSORS_TEXT, buf);
}

static void UpdatePower() {
    gcc::PowerSnapshot p = gcc::CollectPower();
    wchar_t buf[512];

    const wchar_t* dir = L"待机";
    if (p.chargeRateMa > 0)         dir = L"充电中";
    else if (p.dischargeRateMa > 0) dir = L"放电中";

    // --- 整机功率估算 ---
    //  放电时：电池功率 = 电压 × 放电电流 → 这是真实整机功耗（最准）
    //  接电源时：无电流采样（固件不提供充电电流），只能给出说明
    wchar_t sysLine[160];
    if (p.dischargeRateMa > 0 && p.voltageMv > 0) {
        double w = (p.voltageMv / 1000.0) * (p.dischargeRateMa / 1000.0);
        swprintf_s(sysLine, _countof(sysLine),
                   L"整机功率：%.2f W  （电池实测，最准）", w);
    } else if (p.chargeRateMa > 0 && p.voltageMv > 0) {
        double w = (p.voltageMv / 1000.0) * (p.chargeRateMa / 1000.0);
        swprintf_s(sysLine, _countof(sysLine),
                   L"充电功率：%.2f W  （电池实测）", w);
    } else {
        wcscpy_s(sysLine,
                 L"整机功率：接电源时本机固件不提供电流采样");
    }

    swprintf_s(buf, _countof(buf),
        L"电池电压：%.2f V        剩余容量：%u mWh (%u%%)\r\n"
        L"充电电流：%d mA        放电电流：%d mA\r\n"
        L"电池功率：%.2f W  (%s)\r\n"
        L"%s\r\n"
        L"满电容量：%u mWh",
        p.voltageMv / 1000.0, p.remainingMwh, p.batteryPercent,
        p.chargeRateMa, p.dischargeRateMa,
        p.BatteryWatts(), dir,
        sysLine,
        p.fullChargeMwh);

    SetWindowText(g_hPower, buf);
}

// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        int y = 10;

        // 性能模式
        MakeGroup(hWnd, L"性能模式", 10, y, 300, 130);
        MakeButton(hWnd, L"均衡模式", IDC_MODE_BALANCE, 25,  y + 30, 130, 34);
        MakeButton(hWnd, L"性能模式", IDC_MODE_PERF,    170, y + 30, 125, 34);
        MakeButton(hWnd, L"静音模式", IDC_MODE_QUIET,   25,  y + 72, 130, 34);
        MakeButton(hWnd, L"全速模式", IDC_MODE_FULL,    170, y + 72, 125, 34);

        // 风扇
        int fx = 325;
        MakeGroup(hWnd, L"风扇调速", fx, y, 340, 130);
        MakeStatic(hWnd, L"目标转速：", fx + 15, y + 32, 70, 22);
        HWND hSl = CreateWindowEx(0, TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
            fx + 88, y + 28, 140, 30, hWnd, (HMENU)IDC_FAN_SLIDER, g_hInst, nullptr);
        SendMessage(hSl, TBM_SETRANGE, TRUE, MAKELPARAM(0, 255));
        SendMessage(hSl, TBM_SETTICFREQ, 16, 0);
        SendMessage(hSl, TBM_SETPOS, TRUE, 128);
        SetFont(hSl);
        MakeStatic(hWnd, L"128", fx + 234, y + 32, 42, 22, IDC_FAN_TEXT, SS_CENTER);

        MakeButton(hWnd, L"应用转速", IDC_FAN_APPLY, fx + 15, y + 68, 110, 30);
        MakeCheck(hWnd, L"风扇全速", IDC_MAXFAN_CHECK, fx + 140, y + 72, 120, 24);

        MakeStatic(hWnd, L"实测转速：--", fx + 15, y + 100, 300, 22, IDC_RPM_TEXT);
        MakeStatic(hWnd, L"CPU 温度：--",  fx + 15, y + 119, 300, 22, IDC_TEMP_TEXT);

        // 时间段调度
        int sy    = y + 145;
        int rowsH = kRowH * gcc::MAX_RULES;
        int schedH = 64 + rowsH + 36;
        MakeGroup(hWnd, L"时间段调度（开始 – 结束 → 自动处于该模式）", 10, sy, 655, schedH);

        int hx = 28;
        MakeStatic(hWnd, L"启用", hx,       sy + 26, 40, 20);
        MakeStatic(hWnd, L"开始", hx + 42,  sy + 26, 60, 20);
        MakeStatic(hWnd, L"结束", hx + 108, sy + 26, 60, 20);
        MakeStatic(hWnd, L"模式", hx + 174, sy + 26, 90, 20);

        for (int i = 0; i < gcc::MAX_RULES; ++i) {
            int ry = sy + 48 + i * kRowH;
            MakeCheck(hWnd, L"", SchedId(i, OFF_ENABLE), hx + 8, ry + 2, 30, 22);
            MakeEdit(hWnd, L"08:00", SchedId(i, OFF_START), hx + 40, ry, 62, 24);
            MakeEdit(hWnd, L"18:00", SchedId(i, OFF_END),   hx + 106, ry, 62, 24);

            HWND cb = CreateWindowEx(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                hx + 172, ry, 92, 200, hWnd,
                (HMENU)(INT_PTR)SchedId(i, OFF_MODE), g_hInst, nullptr);
            SetFont(cb);
            SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)L"均衡");
            SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)L"性能");
            SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)L"静音");
            SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)L"全速");
            SendMessage(cb, CB_SETCURSEL, 0, 0);
        }

        int by = sy + 48 + rowsH + 6;
        MakeCheck(hWnd, L"启用时间段调度", IDC_SCHED_ONOFF, hx + 8, by, 150, 24);
        g_hSchedInfo = MakeStatic(hWnd, L"未启用", hx + 168, by + 3, 460, 22);

        // 功率
        int py = sy + schedH + 8;
        MakeGroup(hWnd, L"实时功率与电池", 10, py, 655, 168);
        g_hPower = CreateWindowEx(0, L"STATIC", L"正在读取...",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            26, py + 26, 625, 134, hWnd, (HMENU)IDC_POWER_TEXT, g_hInst, nullptr);
        SetFont(g_hPower);

        // 系统监控（CPU / GPU / 内存 占用率）
        int my = py + 176;
        MakeGroup(hWnd, L"系统占用率与 CPU 功耗", 10, my, 655, 108);
        HWND hSens = CreateWindowEx(0, L"STATIC", L"监控数据加载中...",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            26, my + 24, 625, 80, hWnd, (HMENU)IDC_SENSORS_TEXT, g_hInst, nullptr);
        SetFont(hSens);

        // 状态栏
        g_hStatus = MakeStatic(hWnd, L"就绪", 18, my + 116, 640, 22, IDC_STATUS);

        // 默认规则：08:00–18:00 性能；18:00–23:00 均衡；其余停用
        // 注意：此刻 g_hMain 尚未赋值（WM_CREATE 在 CreateWindowEx 内部触发），
        //       因此下面直接把默认规则写入 g_rules，并在窗口显示后同步到界面。
        g_rules[0] = { true,  8 * 60,  18 * 60, gcc::kSchedPerformance };
        g_rules[1] = { true,  18 * 60, 23 * 60, gcc::kSchedBalance };
        for (int i = 2; i < gcc::MAX_RULES; ++i)
            g_rules[i] = { false, 0, 0, gcc::kSchedQuiet };

        g_uiReady = false;   // 等 g_hMain 就绪后再同步界面

        UpdatePower();       // 只更新不依赖 g_hMain 的部分
        // RefreshHardware / CheckSchedule 依赖 g_hMain，改在窗口就绪后调用

        SetTimer(hWnd, TIMER_SCHED, 1000, nullptr);
        SetTimer(hWnd, TIMER_UI,    2000, nullptr);
        SetTimer(hWnd, TIMER_SENSORS, 1500, nullptr);
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        wchar_t buf[256];

        if (id == IDC_MODE_BALANCE || id == IDC_MODE_PERF ||
            id == IDC_MODE_QUIET   || id == IDC_MODE_FULL) {
            uint8_t m = (id == IDC_MODE_BALANCE) ? 0 :
                        (id == IDC_MODE_PERF)    ? 1 :
                        (id == IDC_MODE_QUIET)   ? 2 : 3;
            CheckRadioButton(hWnd, IDC_MODE_BALANCE, IDC_MODE_FULL, id);
            std::string e2;
            if (g_bridgeOk && g_bridge.SetPerfMode(m, e2))
                swprintf_s(buf, _countof(buf), L"已切换到「%s」模式（手动）", ModeName(m));
            else
                swprintf_s(buf, _countof(buf), L"切换失败：%S", e2.c_str());
            SetWindowText(g_hStatus, buf);
            return 0;
        }

        if (id == IDC_FAN_APPLY) {
            int pos = (int)SendDlgItemMessage(hWnd, IDC_FAN_SLIDER, TBM_GETPOS, 0, 0);
            std::string e2;
            if (g_bridgeOk && g_bridge.SetFanSpeed((uint8_t)pos, e2))
                swprintf_s(buf, _countof(buf), L"风扇目标转速已设为 %d", pos);
            else
                swprintf_s(buf, _countof(buf), L"风扇设置失败：%S", e2.c_str());
            SetWindowText(g_hStatus, buf);
            return 0;
        }

        if (id == IDC_MAXFAN_CHECK) {
            bool on = (SendDlgItemMessage(hWnd, IDC_MAXFAN_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED);
            std::string e2;
            if (g_bridgeOk && g_bridge.SetMaxFanSpeed(on, e2))
                SetWindowText(g_hStatus, on ? L"风扇全速已开启" : L"风扇全速已关闭");
            else {
                swprintf_s(buf, _countof(buf), L"全速设置失败：%S", e2.c_str());
                SetWindowText(g_hStatus, buf);
            }
            return 0;
        }

        if (id == IDC_SCHED_ONOFF) {
            g_schedEnabled = (SendDlgItemMessage(hWnd, IDC_SCHED_ONOFF,
                                                 BM_GETCHECK, 0, 0) == BST_CHECKED);
            PullAllRulesFromUI();
            g_lastAppliedRule = -99;
            SetWindowText(g_hStatus, g_schedEnabled
                ? L"时间段调度已启用" : L"时间段调度已停用");
            CheckSchedule();
            return 0;
        }

        // 规则行：启用勾选 / 模式下拉
        if (id >= IDC_SCHED_BASE && id < IDC_SCHED_BASE + gcc::MAX_RULES * 10) {
            int row = (id - IDC_SCHED_BASE) / 10;
            int fld = (id - IDC_SCHED_BASE) % 10;
            if ((fld == OFF_ENABLE || fld == OFF_MODE) && g_uiReady) {
                gcc::ScheduleRule r;
                if (ReadRuleFromUI(row, r)) {
                    g_rules[row] = r;
                    g_lastAppliedRule = -99;
                    CheckSchedule();
                }
            }
            return 0;
        }

        // 编辑框失焦：解析时间
        if (HIWORD(wParam) == EN_KILLFOCUS &&
            id >= IDC_SCHED_BASE && id < IDC_SCHED_BASE + gcc::MAX_RULES * 10) {
            int row = (id - IDC_SCHED_BASE) / 10;
            if (g_uiReady && row >= 0 && row < (int)g_rules.size()) {
                gcc::ScheduleRule r;
                if (ReadRuleFromUI(row, r)) {
                    g_rules[row] = r;
                    g_lastAppliedRule = -99;
                    SetWindowText(g_hStatus, L"规则已更新");
                    CheckSchedule();
                } else {
                    SetWindowText(g_hStatus, L"时间格式错误，请用 HH:MM（如 08:00）");
                }
            }
            return 0;
        }
        return 0;
    }

    case WM_HSCROLL: {
        HWND hCtl = (HWND)lParam;
        if (GetDlgCtrlID(hCtl) == IDC_FAN_SLIDER) {
            int pos = (int)SendMessage(hCtl, TBM_GETPOS, 0, 0);
            wchar_t b[16];
            swprintf_s(b, _countof(b), L"%d", pos);
            SetDlgItemText(hWnd, IDC_FAN_TEXT, b);
        }
        return 0;
    }

    case WM_TIMER: {
        if (wParam == TIMER_SCHED)      CheckSchedule();
        else if (wParam == TIMER_UI)  { UpdatePower(); RefreshHardware(); }
        else if (wParam == TIMER_SENSORS) UpdateSensors();
        return 0;
    }

    case WM_CLOSE:   DestroyWindow(hWnd); return 0;
    case WM_DESTROY: KillTimer(hWnd, TIMER_SCHED);
                     KillTimer(hWnd, TIMER_UI);
                     KillTimer(hWnd, TIMER_SENSORS);
                     PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    g_hInst = hInst;

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_hFont = CreateFontIndirectW(&ncm.lfMessageFont);

    g_bridgeOk = g_bridge.Open(g_bridgeErr);
    { std::string serr; g_sensorsOk = g_sensors.Open(serr); }
    { std::string eerr; g_energyOk = g_energy.Open(eerr); }
    g_rules.assign(gcc::MAX_RULES, gcc::ScheduleRule{});

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"GCCLiteWnd";
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    int schedH = 64 + kRowH * gcc::MAX_RULES + 36;
    int height = 10 + 130 + 15 + schedH + 8 + 168 + 176 + 108 + 45;

    g_hMain = CreateWindowExW(0, L"GCCLiteWnd", L"斗战者控制台 · 轻量版",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 690, height,
        nullptr, nullptr, hInst, nullptr);
    if (!g_hMain) return 1;

    ShowWindow(g_hMain, SW_SHOW);
    UpdateWindow(g_hMain);

    // 窗口就绪后再把默认规则同步到界面控件（WM_CREATE 时 g_hMain 还未赋值）
    for (int i = 0; i < (int)g_rules.size(); ++i)
        WriteRuleToUI(i, g_rules[i]);
    UpdateSensors();
    g_uiReady = true;
    CheckSchedule();

    if (!g_bridgeOk) {
        std::wstring m = L"⚠ 硬件接口未连接：";
        m += std::wstring(g_bridgeErr.begin(), g_bridgeErr.end());
        m += L"  请以管理员身份运行。功率显示仍可用。";
        SetWindowText(g_hStatus, m.c_str());
    } else {
        SetWindowText(g_hStatus, L"已连接硬件接口（root\\WMI : MICommonInterface）");
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessage(g_hMain, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    g_bridge.Close();
    if (g_hFont) DeleteObject(g_hFont);
    return (int)msg.wParam;
}
