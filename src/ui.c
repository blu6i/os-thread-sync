#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0600
#define _WIN32_IE 0x0600
#include <windows.h>
#include <commctrl.h>
#ifndef LVS_EX_DOUBLEBUFFER
#define LVS_EX_DOUBLEBUFFER 0x00010000
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#include "ui.h"
#include "dekker.h"
#include "rw.h"

/* ============================================================================
 * Декларации GDI+ Flat API для чистого C (без C++ заголовков)
 * ============================================================================ */
typedef struct
{
    UINT32 GdiplusVersion;
    void *DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
} GdiplusStartupInput;

typedef void GpImage;
typedef void GpGraphics;
typedef int GpStatus;

extern GpStatus WINAPI GdiplusStartup(ULONG_PTR *token, const GdiplusStartupInput *input, void *output);
extern void WINAPI GdiplusShutdown(ULONG_PTR token);
extern GpStatus WINAPI GdipLoadImageFromFile(const WCHAR *filename, GpImage **image);
extern GpStatus WINAPI GdipDisposeImage(GpImage *image);
extern GpStatus WINAPI GdipCreateFromHDC(HDC hdc, GpGraphics **graphics);
extern GpStatus WINAPI GdipDeleteGraphics(GpGraphics *graphics);
extern GpStatus WINAPI GdipSetInterpolationMode(GpGraphics *graphics, int interpolationMode);
extern GpStatus WINAPI GdipDrawImageRectI(GpGraphics *graphics, GpImage *image, INT x, INT y, INT width, INT height);

/* ============================================================================
 * Идентификаторы элементов управления
 * ============================================================================ */
/* Вкладки переключения задач */
#define ID_TAB_DEKKER 90
#define ID_TAB_RW 91

/* Вкладка 1: Алгоритм Деккера */
#define ID_LIST_1 101
#define ID_LIST_2 102
#define ID_SLIDER_1 103
#define ID_SLIDER_2 104
#define ID_BTN_START_1 105
#define ID_BTN_START_2 106
#define ID_BTN_PAUSE_1 107
#define ID_BTN_PAUSE_2 108
#define ID_BTN_STOP_ALL 109
#define ID_EDIT_MAX 110
#define ID_EDIT_STEP 111

/* Вкладка 2: Читатели и Писатель */
#define ID_RW_SLIDER_READ 201
#define ID_RW_SLIDER_WRITE 202
#define ID_RW_SLIDER_CHANCE 203
#define ID_RW_SLIDER_ACT0 210 /* 210..213 */
#define ID_RW_BTN_START0 220  /* 220..223 */
#define ID_RW_BTN_PAUSE0 230  /* 230..233 */
#define ID_RW_BTN_STOP_ALL 240

/* Таймер плавной анимации (60 FPS) */
#define ID_TIMER_ANIM 301
#define ANIM_INTERVAL_MS 16
#define EASE_OUT_FACTOR 0.12f

/* Пользовательские сообщения для потокобезопасного обновления UI */
#define WM_APP_ROW (WM_APP + 1)
#define WM_APP_STATE (WM_APP + 2)
#define WM_APP_RW_STATE (WM_APP + 3)

/* Геометрия главного окна и верхней панели вкладок */
#define WIN_CLIENT_W 1180
#define WIN_CLIENT_H 760
#define TAB_BAR_H 38
#define TAB_BTN_W 230

/* Геометрия Вкладки 1 (Алгоритм Деккера) */
#define TABLE1_X 36
#define TABLE2_X 468
#define TABLE_Y 88
#define TABLE_W 412
#define TABLE_H 536

#define DESC1_X 36
#define DESC1_Y 638
#define DESC1_W 844
#define DESC1_H 90

#define PANEL1_X 908
#define PANEL1_Y 88
#define PANEL1_W 236
#define PANEL1_H 640

/* Геометрия Вкладки 2 (Читатели и Писатель) */
#define SCENE_X 0
#define SCENE_Y TAB_BAR_H
#define SCENE_W 750
#define SCENE_H (640 - TAB_BAR_H)

#define DESC2_X 36
#define DESC2_Y 644
#define DESC2_W 696
#define DESC2_H 84

#define PANEL2_X 756
#define PANEL2_Y 72
#define PANEL2_W 392
#define PANEL2_H 656

/* Размеры спрайтов на сцене Вкладки 2 (с сохранением пропорций оригинала) */
#define SPRITE_ACTOR_W 106
#define SPRITE_ACTOR_H 214
#define SPRITE_BOOK_W 138
#define SPRITE_BOOK_H 138

/* Цветовая палитра */
#define COL_BG_MAIN RGB(245, 242, 235)
#define COL_TEXT_DARK RGB(60, 56, 66)
#define COL_PANEL_BG RGB(221, 216, 216)
#define COL_TAB_ACTIVE RGB(103, 80, 164)
#define COL_TAB_ACTIVE_P RGB(86, 65, 140)
#define COL_TAB_INACTIVE RGB(221, 216, 216)
#define COL_BTN_START RGB(79, 150, 137)
#define COL_BTN_START_P RGB(63, 126, 114)
#define COL_BTN_PAUSE RGB(224, 161, 78)
#define COL_BTN_PAUSE_P RGB(198, 138, 58)
#define COL_BTN_STOP RGB(174, 36, 25)
#define COL_BTN_STOP_P RGB(142, 28, 18)
#define COL_BTN_DISABLED RGB(188, 183, 183)

/* Цвета цифр над персонажами для 3 статусов */
#define COL_NUM_IDLE_FILL RGB(118, 214, 118)
#define COL_NUM_IDLE_OUT RGB(52, 102, 52)
#define COL_NUM_WAIT_FILL RGB(250, 192, 46)
#define COL_NUM_WAIT_OUT RGB(118, 78, 14)
#define COL_NUM_CS_FILL RGB(174, 36, 25)
#define COL_NUM_CS_OUT RGB(96, 18, 12)

/* ============================================================================
 * Глобальные переменные интерфейса
 * ============================================================================ */
static HWND g_hwnd = NULL;
static int g_active_tab = 0; /* 0 - Алгоритм Деккера, 1 - Читатели и Писатель */
static bool g_dekker_active = false;
static bool g_rw_active = false;

/* Кнопки вкладок */
static HWND g_hTabBtn[2] = {NULL, NULL};

/* Элементы Вкладки 1 (Алгоритм Деккера) */
static HWND g_hTable[2] = {NULL, NULL};
static HWND g_hEditMax = NULL;
static HWND g_hEditStep = NULL;
static HWND g_hLblMax = NULL;
static HWND g_hLblStep = NULL;
static HWND g_hSlider[2] = {NULL, NULL};
static HWND g_hSpeedLbl[2] = {NULL, NULL};
static HWND g_hBtnStart[2] = {NULL, NULL};
static HWND g_hBtnPause[2] = {NULL, NULL};
static HWND g_hBtnStopAll = NULL;

/* Элементы Вкладки 2 (Читатели и Писатель) */
static HWND g_hRwReadLbl = NULL;
static HWND g_hRwWriteLbl = NULL;
static HWND g_hRwChanceLbl = NULL;
static HWND g_hRwSliderRead = NULL;
static HWND g_hRwSliderWrite = NULL;
static HWND g_hRwSliderChance = NULL;
static HWND g_hRwActorSpdLbl[RW_ACTOR_COUNT] = {NULL};
static HWND g_hRwActorSlider[RW_ACTOR_COUNT] = {NULL};
static HWND g_hRwBtnStart[RW_ACTOR_COUNT] = {NULL};
static HWND g_hRwBtnPause[RW_ACTOR_COUNT] = {NULL};
static HWND g_hRwBtnStopAll = NULL;

static WNDPROC g_origEditProc = NULL;

/* Шрифты и кисти */
static HFONT g_fontSection = NULL;
static HFONT g_fontPanelHdr = NULL;
static HFONT g_fontColHdr = NULL;
static HFONT g_fontSpeedVal = NULL;
static HFONT g_fontButton = NULL;
static HFONT g_fontTable = NULL;
static HFONT g_fontActorNum = NULL;

static HBRUSH g_brWhite = NULL;
static HBRUSH g_brPanelBg = NULL;

/* GDI+ ресурсы и спрайты Вкладки 2 */
static ULONG_PTR g_gdiplusToken = 0;
static GpImage *g_imgWriter = NULL;
static GpImage *g_imgReader = NULL;
static GpImage *g_imgBook = NULL;

/* Координаты книги и точек перемещения для 4 персонажей (0 - Писарь, 1..3 - Читатели) */
static const float g_book_x = 266.0f;
static const float g_book_y = 268.0f;

/* Исходные точки ("Активен") */
static const float g_home_x[RW_ACTOR_COUNT] = {44.0f, 456.0f, 594.0f, 492.0f};
static const float g_home_y[RW_ACTOR_COUNT] = {188.0f, 82.0f, 214.0f, 386.0f};

/* Конечные точки у книги ("В критической секции") */
static const float g_cs_x[RW_ACTOR_COUNT] = {176.0f, 336.0f, 398.0f, 352.0f};
static const float g_cs_y[RW_ACTOR_COUNT] = {208.0f, 142.0f, 222.0f, 316.0f};

/* Текущие плавные координаты персонажей */
static float g_cur_x[RW_ACTOR_COUNT] = {44.0f, 456.0f, 594.0f, 492.0f};
static float g_cur_y[RW_ACTOR_COUNT] = {188.0f, 82.0f, 214.0f, 386.0f};
static RWActorState g_last_state[RW_ACTOR_COUNT] = {
    RW_STATE_IDLE, RW_STATE_IDLE, RW_STATE_IDLE, RW_STATE_IDLE};

/* Счетчики поколений для таблиц Вкладки 1 */
static volatile LONG g_gen[2] = {0, 0};

typedef struct
{
    int thread_id;
    int row_idx;
    LONG gen;
    char *col1;
    char *col2;
} RowPacket;

/**
 * Преобразует строку в кодировке UTF-8 в широкую строку UTF-16 (wchar_t) для вызовов WinAPI.
 *
 * @param utf8      Исходная строка в кодировке UTF-8 (с завершающим нулем).
 * @param wbuf      Выходной буфер символов wchar_t.
 * @param max_chars Максимальная емкость выходного буфера wbuf в символах.
 */
static void utf8_to_wide(const char *utf8, wchar_t *wbuf, int max_chars)
{
    if (!utf8 || max_chars <= 0)
        return;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wbuf, max_chars);
}

/**
 * Создает в динамической памяти (heap) копию C-строки.
 *
 * @param s Исходная нуль-терминированная строка для копирования.
 * @return Указатель на выделенную копию строки (освобождается через free) или NULL при нехватке памяти.
 */
static char *dup_cstr(const char *s)
{
    size_t len = strlen(s) + 1;
    char *copy = (char *)malloc(len);
    if (copy)
    {
        memcpy(copy, s, len);
    }
    return copy;
}

/**
 * Загружает PNG-изображение через GDI+ Flat API, проверяя путь как относительно корня проекта,
 * так и относительно подкаталога build/.
 *
 * @param rel_path_utf8 Относительный путь к PNG-файлу в кодировке UTF-8.
 * @return Указатель на загруженный объект GpImage или NULL в случае ошибки.
 */
static GpImage *load_png_asset(const char *rel_path_utf8)
{
    wchar_t wpath[256];
    utf8_to_wide(rel_path_utf8, wpath, 256);

    GpImage *img = NULL;
    if (GdipLoadImageFromFile(wpath, &img) == 0 && img != NULL)
    {
        return img;
    }
    char alt_utf8[256];
    snprintf(alt_utf8, sizeof(alt_utf8), "../%s", rel_path_utf8);
    utf8_to_wide(alt_utf8, wpath, 256);
    if (GdipLoadImageFromFile(wpath, &img) == 0 && img != NULL)
    {
        return img;
    }
    return NULL;
}

/**
 * Потокобезопасно отправляет пакет данных из рабочего потока алгоритма Деккера в UI-поток
 * для добавления или обновления строки в таблице результатов.
 *
 * @param thread_id     Идентификатор потока (0 - первый поток, 1 - второй поток).
 * @param row_idx       Индекс строки в таблице (начиная с 0).
 * @param col_available Текст для колонки "Доступные данные".
 * @param col_computed  Текст для колонки "Вычисляемые данные".
 */
void ui_post_row(int thread_id, int row_idx, const char *col_available, const char *col_computed)
{
    if (!g_hwnd || thread_id < 0 || thread_id > 1 || row_idx < 0)
        return;

    RowPacket *pkt = (RowPacket *)malloc(sizeof(RowPacket));
    if (!pkt)
        return;
    pkt->thread_id = thread_id;
    pkt->row_idx = row_idx;
    pkt->gen = g_gen[thread_id];
    pkt->col1 = dup_cstr(col_available ? col_available : "");
    pkt->col2 = dup_cstr(col_computed ? col_computed : "");

    if (!pkt->col1 || !pkt->col2)
    {
        free(pkt->col1);
        free(pkt->col2);
        free(pkt);
        return;
    }
    if (!PostMessageW(g_hwnd, WM_APP_ROW, 0, (LPARAM)pkt))
    {
        free(pkt->col1);
        free(pkt->col2);
        free(pkt);
    }
}

/**
 * Очищает таблицу указанного потока (или обе таблицы при thread_id < 0) и инкрементирует
 * счетчик поколения, чтобы отбросить устаревшие асинхронные пакеты в очереди сообщений.
 *
 * @param thread_id Идентификатор таблицы потока (0, 1 или -1 для очистки обеих таблиц).
 */
void ui_post_clear(int thread_id)
{
    if (thread_id == 0 || thread_id < 0)
    {
        InterlockedIncrement(&g_gen[0]);
        if (g_hTable[0])
            SendMessageW(g_hTable[0], LVM_DELETEALLITEMS, 0, 0);
    }
    if (thread_id == 1 || thread_id < 0)
    {
        InterlockedIncrement(&g_gen[1]);
        if (g_hTable[1])
            SendMessageW(g_hTable[1], LVM_DELETEALLITEMS, 0, 0);
    }
}

/**
 * Упаковывает флаг активности сессии и массив доступности кнопок "Старт" в битовую маску WPARAM
 * и асинхронно отправляет сообщение в главный поток окна.
 *
 * @param msg            Идентификатор оконного сообщения (WM_APP_STATE или WM_APP_RW_STATE).
 * @param session_active Флаг активности сессии симуляции.
 * @param start_enabled  Массив флагов доступности кнопок "Старт".
 * @param count          Количество потоков в группе.
 */
static void post_sim_state_msg(UINT msg, bool session_active, const bool *start_enabled, int count)
{
    if (!g_hwnd)
        return;
    WPARAM wp = (session_active ? 1 : 0);
    for (int i = 0; i < count; i++)
    {
        if (start_enabled && start_enabled[i])
        {
            wp |= (WPARAM)(1 << (i + 1));
        }
    }
    PostMessageW(g_hwnd, msg, wp, 0);
}

/**
 * Потокобезопасно уведомляет главное окно об изменении состояния сессии алгоритма Деккера
 * и доступности кнопок "Старт" / "Останов" на первой вкладке.
 *
 * @param session_active Флаг активности хотя бы одного потока Деккера.
 * @param start_enabled  Массив флагов доступности кнопок "Старт" для каждого потока.
 * @param count          Количество потоков в группе (2).
 */
void ui_post_state(bool session_active, const bool *start_enabled, int count)
{
    post_sim_state_msg(WM_APP_STATE, session_active, start_enabled, count);
}

/**
 * Потокобезопасно уведомляет главное окно об изменении состояния сессии задачи
 * "Читатели - Писатель" и доступности кнопок "Старт" / "Останов" на второй вкладке.
 *
 * @param session_active Флаг активности симуляции на второй вкладке.
 * @param start_enabled  Массив флагов доступности кнопок "Старт" для каждого участника.
 * @param count          Количество участников в группе (4).
 */
void ui_post_rw_state(bool session_active, const bool *start_enabled, int count)
{
    post_sim_state_msg(WM_APP_RW_STATE, session_active, start_enabled, count);
}

/**
 * Форматирует целое число и отображает его в текстовой метке (STATIC) над ползунком скорости.
 *
 * @param hLbl Дескриптор окна текстовой метки (HWND).
 * @param val  Целочисленное значение скорости для вывода (0..100).
 */
static void set_label_int(HWND hLbl, int val)
{
    char buf[32];
    wchar_t wbuf[32];
    snprintf(buf, sizeof(buf), "%d", val);
    utf8_to_wide(buf, wbuf, 32);
    SetWindowTextW(hLbl, wbuf);
}

/**
 * Обновляет надпись на кнопке паузы потока ("Пауза" или "Продолжить")
 * на основе состояния группы симуляции и запрашивает перерисовку кнопки.
 *
 * @param hBtn Дескриптор кнопки паузы (HWND).
 * @param grp  Указатель на группу потоков симуляции.
 * @param id   Индекс потока в группе.
 */
static void update_pause_btn_text(HWND hBtn, const SimGroup *grp, int id)
{
    wchar_t wbuf[64];
    const char *txt = sim_is_paused(grp, id) ? "Продолжить" : "Пауза";
    utf8_to_wide(txt, wbuf, 64);
    SetWindowTextW(hBtn, wbuf);
    InvalidateRect(hBtn, NULL, TRUE);
}

/**
 * Блокирует или разблокирует кнопки переключения вкладок в зависимости от того,
 * запущена ли в данный момент любая из двух симуляций.
 */
static void update_tab_locks(void)
{
    BOOL sim_running = (g_dekker_active || g_rw_active) ? TRUE : FALSE;
    EnableWindow(g_hTabBtn[0], !sim_running);
    EnableWindow(g_hTabBtn[1], !sim_running);
    InvalidateRect(g_hTabBtn[0], NULL, TRUE);
    InvalidateRect(g_hTabBtn[1], NULL, TRUE);
}

/**
 * Переключает активную вкладку интерфейса (0 - Алгоритм Деккера, 1 - Читатели и Писатель),
 * скрывая элементы неактивной вкладки и отображая элементы выбранной.
 *
 * @param tab_idx Индекс вкладки для активации (0 или 1).
 */
static void switch_tab(int tab_idx)
{
    if (tab_idx < 0 || tab_idx > 1)
        return;
    if (g_dekker_active || g_rw_active)
        return;
    g_active_tab = tab_idx;

    int show1 = (tab_idx == 0) ? SW_SHOW : SW_HIDE;
    int show2 = (tab_idx == 1) ? SW_SHOW : SW_HIDE;

    /* Элементы Вкладки 1 */
    ShowWindow(g_hTable[0], show1);
    ShowWindow(g_hTable[1], show1);
    ShowWindow(g_hLblMax, show1);
    ShowWindow(g_hEditMax, show1);
    ShowWindow(g_hLblStep, show1);
    ShowWindow(g_hEditStep, show1);
    ShowWindow(g_hSpeedLbl[0], show1);
    ShowWindow(g_hSpeedLbl[1], show1);
    ShowWindow(g_hSlider[0], show1);
    ShowWindow(g_hSlider[1], show1);
    ShowWindow(g_hBtnStart[0], show1);
    ShowWindow(g_hBtnStart[1], show1);
    ShowWindow(g_hBtnPause[0], show1);
    ShowWindow(g_hBtnPause[1], show1);
    ShowWindow(g_hBtnStopAll, show1);

    /* Элементы Вкладки 2 */
    ShowWindow(g_hRwReadLbl, show2);
    ShowWindow(g_hRwWriteLbl, show2);
    ShowWindow(g_hRwChanceLbl, show2);
    ShowWindow(g_hRwSliderRead, show2);
    ShowWindow(g_hRwSliderWrite, show2);
    ShowWindow(g_hRwSliderChance, show2);
    for (int i = 0; i < RW_ACTOR_COUNT; i++)
    {
        ShowWindow(g_hRwActorSpdLbl[i], show2);
        ShowWindow(g_hRwActorSlider[i], show2);
        ShowWindow(g_hRwBtnStart[i], show2);
        ShowWindow(g_hRwBtnPause[i], show2);
    }
    ShowWindow(g_hRwBtnStopAll, show2);

    InvalidateRect(g_hTabBtn[0], NULL, TRUE);
    InvalidateRect(g_hTabBtn[1], NULL, TRUE);
    InvalidateRect(g_hwnd, NULL, TRUE);
}

/**
 * Вычисляет целевые координаты (x, y) персонажа на сцене в зависимости от его статуса:
 * исходная позиция (IDLE), середина пути к книге (WAITING) или вплотную к книге (IN_CS).
 *
 * @param actor_id Идентификатор участника (0 - Писарь, 1..3 - Читатели).
 * @param st       Текущее логическое состояние участника.
 * @param out_x    Указатель для записи целевой координаты X.
 * @param out_y    Указатель для записи целевой координаты Y.
 */
static void get_actor_target(int actor_id, RWActorState st, float *out_x, float *out_y)
{
    if (st == RW_STATE_IN_CS)
    {
        /* В КС: персонаж подходит вплотную к книге */
        *out_x = g_cs_x[actor_id];
        *out_y = g_cs_y[actor_id];
    }
    else if (st == RW_STATE_WAITING)
    {
        /* В ожидании: персонаж проходит ровно половину пути до книги */
        *out_x = (g_home_x[actor_id] + g_cs_x[actor_id]) * 0.5f;
        *out_y = (g_home_y[actor_id] + g_cs_y[actor_id]) * 0.5f;
    }
    else
    {
        /* Ничего не делает: персонаж стоит на исходной позиции */
        *out_x = g_home_x[actor_id];
        *out_y = g_home_y[actor_id];
    }
}

/**
 * Выполняет один шаг (тик 60 FPS) плавной анимации перемещения персонажей с эффектом
 * экспоненциального затухания скорости при приближении к целевой точке (Ease-Out).
 */
static void tick_animation(void)
{
    bool need_redraw = false;

    for (int i = 0; i < RW_ACTOR_COUNT; i++)
    {
        RWActorState st = rw_get_actor_state(i);
        if (st != g_last_state[i])
        {
            g_last_state[i] = st;
            need_redraw = true;
        }

        float tx = 0.0f, ty = 0.0f;
        get_actor_target(i, st, &tx, &ty);

        float dx = tx - g_cur_x[i];
        float dy = ty - g_cur_y[i];
        float dist_sq = dx * dx + dy * dy;

        if (dist_sq > 0.25f)
        {
            /* Экспоненциальное затухание скорости по мере приближения к цели (Ease-Out) */
            float dist = sqrtf(dist_sq);
            float step = dist * EASE_OUT_FACTOR;
            if (step < 1.1f)
            {
                step = 1.1f;
            }

            if (step >= dist)
            {
                g_cur_x[i] = tx;
                g_cur_y[i] = ty;
                rw_notify_actor_arrived(i);
            }
            else
            {
                g_cur_x[i] += (dx / dist) * step;
                g_cur_y[i] += (dy / dist) * step;
                float rem_x = tx - g_cur_x[i];
                float rem_y = ty - g_cur_y[i];
                if (rem_x * rem_x + rem_y * rem_y <= 4.0f)
                {
                    rw_notify_actor_arrived(i);
                }
            }
            need_redraw = true;
        }
        else
        {
            if (dist_sq > 0.0001f)
            {
                g_cur_x[i] = tx;
                g_cur_y[i] = ty;
                need_redraw = true;
            }
            rw_notify_actor_arrived(i);
        }
    }

    if (need_redraw && g_active_tab == 1 && g_hwnd)
    {
        RECT rcScene = {SCENE_X, SCENE_Y, SCENE_X + SCENE_W, SCENE_Y + SCENE_H};
        InvalidateRect(g_hwnd, &rcScene, FALSE);
    }
}

/**
 * Отрисовывает над головой персонажа крупную метку ("П" для писаря, "1".."3" для читателей)
 * с контурной обводкой и цветом заливки, соответствующим текущему статусу потока.
 *
 * @param hdc      Контекст устройства отрисовки (HDC).
 * @param actor_id Идентификатор участника (0 - Писарь, 1..3 - Читатели).
 * @param x        Текущая экранная координата X левого верхнего угла спрайта.
 * @param y        Текущая экранная координата Y левого верхнего угла спрайта.
 * @param st       Текущий статус участника (определяет цвет метки).
 */
static void draw_actor_badge(HDC hdc, int actor_id, int x, int y, RWActorState st)
{
    wchar_t label[8];
    if (actor_id == RW_WRITER_ID)
    {
        utf8_to_wide("П", label, 8);
    }
    else
    {
        char num_buf[8];
        snprintf(num_buf, sizeof(num_buf), "%d", actor_id);
        utf8_to_wide(num_buf, label, 8);
    }

    COLORREF fill_col = COL_NUM_IDLE_FILL;
    COLORREF out_col = COL_NUM_IDLE_OUT;
    if (st == RW_STATE_WAITING)
    {
        fill_col = COL_NUM_WAIT_FILL;
        out_col = COL_NUM_WAIT_OUT;
    }
    else if (st == RW_STATE_IN_CS)
    {
        fill_col = COL_NUM_CS_FILL;
        out_col = COL_NUM_CS_OUT;
    }

    HFONT oldFont = (HFONT)SelectObject(hdc, g_fontActorNum);
    SetBkMode(hdc, TRANSPARENT);

    int x_off = (actor_id == RW_WRITER_ID) ? -6 : 6;
    RECT rc = {x + x_off, y - 36, x + SPRITE_ACTOR_W + x_off, y + 16};

    /* Темная контурная обводка цифры (2px) */
    SetTextColor(hdc, out_col);
    static const int offs[12][2] = {
        {-2, 0}, {2, 0}, {0, -2}, {0, 2}, {-2, -2}, {2, -2}, {-2, 2}, {2, 2}, {-1, -2}, {1, -2}, {-1, 2}, {1, 2}};
    for (int k = 0; k < 12; k++)
    {
        RECT rcOut = rc;
        OffsetRect(&rcOut, offs[k][0], offs[k][1]);
        DrawTextW(hdc, label, -1, &rcOut, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* Основная заливка цифры цветом текущего статуса */
    SetTextColor(hdc, fill_col);
    DrawTextW(hdc, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, oldFont);
}

/* Отрисовка сцены Вкладки 2 с Y-сортировкой глубины (алгоритм художника) */
typedef struct
{
    int type; /* 0..3 - персонаж, 4 - книга */
    int x;
    int y;
    int foot_y;
} RenderItem;

/**
 * Отрисовывает левую сцену второй вкладки (кафедру с книгой, писаря, трех читателей,
 * их цветовые метки и легенду статусов) с Y-сортировкой по глубине (алгоритм художника).
 *
 * @param hdc Буферизованный контекст устройства (HDC) для рисования.
 */
static void paint_rw_scene(HDC hdc)
{
    RenderItem items[5];
    for (int i = 0; i < RW_ACTOR_COUNT; i++)
    {
        items[i].type = i;
        items[i].x = (int)(g_cur_x[i] + 0.5f);
        items[i].y = (int)(g_cur_y[i] + 0.5f);
        items[i].foot_y = items[i].y + SPRITE_ACTOR_H;
    }
    items[4].type = 4;
    items[4].x = (int)(g_book_x + 0.5f);
    items[4].y = (int)(g_book_y + 0.5f);
    items[4].foot_y = items[4].y + SPRITE_BOOK_H - 12;

    /* Сортируем объекты по нижней границе (foot_y), чтобы ближние перекрывали дальних */
    for (int i = 0; i < 4; i++)
    {
        for (int j = i + 1; j < 5; j++)
        {
            if (items[j].foot_y < items[i].foot_y)
            {
                RenderItem tmp = items[i];
                items[i] = items[j];
                items[j] = tmp;
            }
        }
    }

    for (int i = 0; i < 5; i++)
    {
        /* 1. Рисуем PNG-спрайт через GDI+ и сразу закрываем GpGraphics,
         * чтобы GDI+ не блокировал область отсечения (Clip Region) у HDC для текста */
        GpGraphics *graphics = NULL;
        GdipCreateFromHDC(hdc, &graphics);
        if (graphics)
        {
            GdipSetInterpolationMode(graphics, 7); /* HighQualityBicubic */
            if (items[i].type == 4)
            {
                if (g_imgBook)
                {
                    GdipDrawImageRectI(graphics, g_imgBook,
                                       items[i].x, items[i].y, SPRITE_BOOK_W, SPRITE_BOOK_H);
                }
            }
            else
            {
                int actor_id = items[i].type;
                GpImage *sprite = (actor_id == RW_WRITER_ID) ? g_imgWriter : g_imgReader;
                if (sprite)
                {
                    GdipDrawImageRectI(graphics, sprite,
                                       items[i].x, items[i].y, SPRITE_ACTOR_W, SPRITE_ACTOR_H);
                }
            }
            GdipDeleteGraphics(graphics);
        }

        /* 2. Поверх спрайта рисуем программную цифру (1, 2, 3) средствами GDI */
        if (items[i].type < RW_ACTOR_COUNT)
        {
            int actor_id = items[i].type;
            draw_actor_badge(hdc, actor_id, items[i].x, items[i].y, rw_get_actor_state(actor_id));
        }
    }

    /* Небольшая легенда цветов статусов внизу слева сцены */
    SetBkMode(hdc, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(hdc, g_fontColHdr);
    const struct
    {
        COLORREF col;
        COLORREF out;
        const char *text;
        int x;
    } legend[3] = {
        {COL_NUM_IDLE_FILL, COL_NUM_IDLE_OUT, "Активен", 36},
        {COL_NUM_WAIT_FILL, COL_NUM_WAIT_OUT, "Ожидание", 150},
        {COL_NUM_CS_FILL, COL_NUM_CS_OUT, "В критической секции", 280}};
    int ly = DESC2_Y - 32;
    for (int i = 0; i < 3; i++)
    {
        HBRUSH br = CreateSolidBrush(legend[i].col);
        HPEN pn = CreatePen(PS_SOLID, 2, legend[i].out);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
        HPEN op = (HPEN)SelectObject(hdc, pn);
        Ellipse(hdc, legend[i].x, ly + 2, legend[i].x + 16, ly + 18);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(br);
        DeleteObject(pn);

        wchar_t wtxt[64];
        utf8_to_wide(legend[i].text, wtxt, 64);
        SetTextColor(hdc, COL_TEXT_DARK);
        RECT rcL = {legend[i].x + 22, ly, legend[i].x + 260, ly + 22};
        DrawTextW(hdc, wtxt, -1, &rcL, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hdc, oldFont);
}

/**
 * Рисует скругленный информационный блок с текстом формулировки задания
 * и внутренними отступами от краев блока.
 *
 * @param hdc       Контекст устройства отрисовки (HDC).
 * @param x         Координата X левого верхнего угла блока.
 * @param y         Координата Y левого верхнего угла блока.
 * @param w         Ширина блока в пикселях.
 * @param h         Высота блока в пикселях.
 * @param text_utf8 Текст описания задания в кодировке UTF-8.
 */
static void draw_task_description_block(HDC hdc, int x, int y, int w, int h, const char *text_utf8)
{
    HPEN penBlock = CreatePen(PS_SOLID, 1, COL_PANEL_BG);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, g_brPanelBg);
    HPEN oldPen = (HPEN)SelectObject(hdc, penBlock);

    RoundRect(hdc, x, y, x + w, y + h, 18, 18);

    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(penBlock);

    wchar_t wtext[512];
    utf8_to_wide(text_utf8, wtext, 512);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, COL_TEXT_DARK);
    HFONT oldFont = (HFONT)SelectObject(hdc, g_fontTable);

    /* Внутренний отступ текста от краев блока (18px по горизонтали, 14px по вертикали) */
    RECT rcText = {x + 18, y + 14, x + w - 18, y + h - 14};
    DrawTextW(hdc, wtext, -1, &rcText, DT_LEFT | DT_TOP | DT_WORDBREAK);

    SelectObject(hdc, oldFont);
}

/**
 * Оконная процедура-фильтр (subclassing) для полей ввода EDIT: разрешает ввод только
 * неотрицательных целых и вещественных чисел, автоматически заменяя запятую на точку.
 *
 * @param hwnd   Дескриптор поля ввода EDIT.
 * @param msg    Код сообщения Windows.
 * @param wParam Код вводимого символа или параметр сообщения.
 * @param lParam Дополнительный параметр сообщения.
 * @return Результат обработки сообщения родительским классом или 0 при блокировке символа.
 */
static LRESULT CALLBACK PositiveFloatEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_CHAR)
    {
        wchar_t ch = (wchar_t)wParam;
        if (ch < 32)
        {
            return CallWindowProcW(g_origEditProc, hwnd, msg, wParam, lParam);
        }
        if (ch == L',')
        {
            ch = L'.';
            wParam = L'.';
        }
        if (ch >= L'0' && ch <= L'9')
        {
            return CallWindowProcW(g_origEditProc, hwnd, msg, wParam, lParam);
        }
        if (ch == L'.')
        {
            wchar_t buf[64] = {0};
            GetWindowTextW(hwnd, buf, 64);
            if (wcschr(buf, L'.') == NULL)
            {
                return CallWindowProcW(g_origEditProc, hwnd, msg, wParam, lParam);
            }
        }
        return 0;
    }
    else if (msg == WM_PASTE)
    {
        return 0;
    }
    return CallWindowProcW(g_origEditProc, hwnd, msg, wParam, lParam);
}

/**
 * Считывает введенные пользователем значения "Макс. x" и "Шаг (h)" из полей ввода,
 * проверяет их корректность и передает в модуль алгоритма Деккера.
 */
static void apply_user_params(void)
{
    wchar_t wbuf[64];
    wchar_t *endptr = NULL;

    GetWindowTextW(g_hEditMax, wbuf, 64);
    double x_max = wcstod(wbuf, &endptr);
    if (x_max < X_START)
    {
        x_max = DEFAULT_X_MAX;
        SetWindowTextW(g_hEditMax, L"12");
    }

    GetWindowTextW(g_hEditStep, wbuf, 64);
    double step_h = wcstod(wbuf, &endptr);
    if (step_h <= 0.0001)
    {
        step_h = DEFAULT_STEP;
        SetWindowTextW(g_hEditStep, L"1");
    }

    dekker_set_params(x_max, step_h);
}

/**
 * Отрисовывает кастомные скругленные кнопки управления ("Старт", "Пауза", "Останов")
 * и прижатые к верху кнопки переключения вкладок (WM_DRAWITEM).
 *
 * @param dis Указатель на структуру DRAWITEMSTRUCT с параметрами отрисовываемой кнопки.
 */
static void draw_rounded_button(LPDRAWITEMSTRUCT dis)
{
    BOOL isDisabled = (dis->itemState & ODS_DISABLED) != 0;
    BOOL isSelected = (dis->itemState & ODS_SELECTED) != 0;
    UINT id = dis->CtlID;

    BOOL isTabBtn = (id == ID_TAB_DEKKER || id == ID_TAB_RW);
    FillRect(dis->hDC, &dis->rcItem, isTabBtn ? g_brWhite : g_brPanelBg);

    COLORREF btnColor = COL_BTN_DISABLED;
    COLORREF textColor = RGB(255, 255, 255);

    if (isTabBtn)
    {
        bool active = (id == ID_TAB_DEKKER && g_active_tab == 0) ||
                      (id == ID_TAB_RW && g_active_tab == 1);
        if (active)
        {
            btnColor = isSelected ? COL_TAB_ACTIVE_P : COL_TAB_ACTIVE;
            textColor = RGB(255, 255, 255);
        }
        else
        {
            btnColor = isDisabled ? RGB(204, 200, 200) : COL_TAB_INACTIVE;
            textColor = isDisabled ? RGB(140, 136, 140) : COL_TEXT_DARK;
        }

        /* Вкладки прижаты к верхнему краю окна (верхние углы прямые, нижние скругленные) */
        HBRUSH hBrush = CreateSolidBrush(btnColor);
        HPEN hPen = CreatePen(PS_SOLID, 1, btnColor);
        HBRUSH oldBr = (HBRUSH)SelectObject(dis->hDC, hBrush);
        HPEN oldPen = (HPEN)SelectObject(dis->hDC, hPen);

        RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top - 8,
                  dis->rcItem.right, dis->rcItem.bottom, 12, 12);

        SelectObject(dis->hDC, oldBr);
        SelectObject(dis->hDC, oldPen);
        DeleteObject(hBrush);
        DeleteObject(hPen);

        wchar_t text[64] = {0};
        GetWindowTextW(dis->hwndItem, text, 64);

        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, textColor);
        HFONT oldFont = (HFONT)SelectObject(dis->hDC, g_fontPanelHdr);
        RECT rcText = dis->rcItem;
        DrawTextW(dis->hDC, text, -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dis->hDC, oldFont);
        return;
    }
    else if (!isDisabled)
    {
        if (id == ID_BTN_START_1 || id == ID_BTN_START_2 ||
            (id >= ID_RW_BTN_START0 && id < ID_RW_BTN_START0 + RW_ACTOR_COUNT))
        {
            btnColor = isSelected ? COL_BTN_START_P : COL_BTN_START;
        }
        else if (id == ID_BTN_PAUSE_1 || id == ID_BTN_PAUSE_2 ||
                 (id >= ID_RW_BTN_PAUSE0 && id < ID_RW_BTN_PAUSE0 + RW_ACTOR_COUNT))
        {
            btnColor = isSelected ? COL_BTN_PAUSE_P : COL_BTN_PAUSE;
        }
        else if (id == ID_BTN_STOP_ALL || id == ID_RW_BTN_STOP_ALL)
        {
            btnColor = isSelected ? COL_BTN_STOP_P : COL_BTN_STOP;
        }
    }
    else
    {
        textColor = RGB(120, 115, 115);
    }

    HBRUSH hBrush = CreateSolidBrush(btnColor);
    HPEN hPen = CreatePen(PS_SOLID, 1, btnColor);
    HBRUSH oldBr = (HBRUSH)SelectObject(dis->hDC, hBrush);
    HPEN oldPen = (HPEN)SelectObject(dis->hDC, hPen);

    RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top,
              dis->rcItem.right, dis->rcItem.bottom, 12, 12);

    SelectObject(dis->hDC, oldBr);
    SelectObject(dis->hDC, oldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);

    wchar_t text[64] = {0};
    GetWindowTextW(dis->hwndItem, text, 64);

    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, textColor);
    HFONT oldFont = (HFONT)SelectObject(dis->hDC, isTabBtn ? g_fontPanelHdr : g_fontButton);

    RECT rcText = dis->rcItem;
    if (isSelected && !isDisabled)
    {
        OffsetRect(&rcText, 0, 1);
    }
    DrawTextW(dis->hDC, text, -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dis->hDC, oldFont);
}

/**
 * Отрисовывает полный кадр клиентской области окна в буфере памяти в зависимости
 * от выбранной вкладки (фон, заголовки, серую панель управления, сцену и блок задания).
 *
 * @param hdc Буферизованный контекст устройства (memDC).
 */
static void paint_window_content(HDC hdc)
{
    RECT rcClient = {0, 0, WIN_CLIENT_W, WIN_CLIENT_H};
    FillRect(hdc, &rcClient, g_brWhite);

    wchar_t wbuf[64];
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, COL_TEXT_DARK);
    HFONT oldFont = (HFONT)SelectObject(hdc, g_fontSection);

    if (g_active_tab == 0)
    {
        /* --- Вкладка 1: Алгоритм Деккера --- */
        utf8_to_wide("1 поток", wbuf, 64);
        RECT rcT1 = {TABLE1_X, TABLE_Y - 34, TABLE1_X + TABLE_W, TABLE_Y - 4};
        DrawTextW(hdc, wbuf, -1, &rcT1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        utf8_to_wide("2 поток", wbuf, 64);
        RECT rcT2 = {TABLE2_X, TABLE_Y - 34, TABLE2_X + TABLE_W, TABLE_Y - 4};
        DrawTextW(hdc, wbuf, -1, &rcT2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        HPEN penPanel = CreatePen(PS_SOLID, 1, COL_PANEL_BG);
        HBRUSH oldBr = (HBRUSH)SelectObject(hdc, g_brPanelBg);
        HPEN oldPen = (HPEN)SelectObject(hdc, penPanel);
        RoundRect(hdc, PANEL1_X, PANEL1_Y, PANEL1_X + PANEL1_W, PANEL1_Y + PANEL1_H, 24, 24);
        SelectObject(hdc, oldBr);
        SelectObject(hdc, oldPen);
        DeleteObject(penPanel);

        SelectObject(hdc, g_fontPanelHdr);
        utf8_to_wide("1 поток", wbuf, 64);
        RECT rcP1 = {PANEL1_X + 16, PANEL1_Y + 130, PANEL1_X + 114, PANEL1_Y + 154};
        DrawTextW(hdc, wbuf, -1, &rcP1, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        utf8_to_wide("2 поток", wbuf, 64);
        RECT rcP2 = {PANEL1_X + 122, PANEL1_Y + 130, PANEL1_X + 220, PANEL1_Y + 154};
        DrawTextW(hdc, wbuf, -1, &rcP2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        /* Блок с описанием 1-го задания под таблицами */
        draw_task_description_block(
            hdc, DESC1_X, DESC1_Y, DESC1_W, DESC1_H,
            "С помощью алгоритма Деккера организовать работу параллельных вычислительных "
            "потоков. Первый поток записывает в файл подсчитанные значения функции Y=x^2/18 "
            "на интервале x в [1; 12] с шагом h=1. Второй поток считывает из файла очередное "
            "значение и подсчитывает сумму всех считанных значений в переменной Z.");
    }
    else
    {
        /* --- Вкладка 2: Задача о писателе и читателях --- */
        paint_rw_scene(hdc);

        /* Блок с описанием 2-го задания под анимацией */
        draw_task_description_block(
            hdc, DESC2_X, DESC2_Y, DESC2_W, DESC2_H,
            "Задачу о писателе и читателе решить с помощью системных функций "
            "EnterCriticalSection() и LeaveCriticalSection() (три читателя, приоритет писателя).");

        /* Правая скругленная серая панель управления */
        HPEN penPanel = CreatePen(PS_SOLID, 1, COL_PANEL_BG);
        HBRUSH oldBr = (HBRUSH)SelectObject(hdc, g_brPanelBg);
        HPEN oldPen = (HPEN)SelectObject(hdc, penPanel);
        RoundRect(hdc, PANEL2_X, PANEL2_Y, PANEL2_X + PANEL2_W, PANEL2_Y + PANEL2_H, 24, 24);
        SelectObject(hdc, oldBr);
        SelectObject(hdc, oldPen);
        DeleteObject(penPanel);

        /* Заголовки "Скорость чтения", "Скорость записи" и "Шанс похода читателей" */
        SelectObject(hdc, g_fontPanelHdr);

        utf8_to_wide("Скорость чтения", wbuf, 64);
        RECT rcHdrRead = {PANEL2_X + 20, PANEL2_Y + 14, PANEL2_X + 360, PANEL2_Y + 36};
        DrawTextW(hdc, wbuf, -1, &rcHdrRead, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        utf8_to_wide("Скорость записи", wbuf, 64);
        RECT rcHdrWrite = {PANEL2_X + 20, PANEL2_Y + 76, PANEL2_X + 360, PANEL2_Y + 98};
        DrawTextW(hdc, wbuf, -1, &rcHdrWrite, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        utf8_to_wide("Шанс похода читателей (%)", wbuf, 64);
        RECT rcHdrChance = {PANEL2_X + 20, PANEL2_Y + 138, PANEL2_X + 360, PANEL2_Y + 160};
        DrawTextW(hdc, wbuf, -1, &rcHdrChance, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        /* Заголовки 4 колонок: Писарь, Читатель 1, Читатель 2, Читатель 3 */
        SelectObject(hdc, g_fontColHdr);
        static const char *col_titles[RW_ACTOR_COUNT] = {
            "Писарь", "Читатель 1", "Читатель 2", "Читатель 3"};
        for (int i = 0; i < RW_ACTOR_COUNT; i++)
        {
            int cx = PANEL2_X + 16 + i * 92;
            utf8_to_wide(col_titles[i], wbuf, 64);
            RECT rcCol = {cx - 4, PANEL2_Y + 200, cx + 88, PANEL2_Y + 224};
            DrawTextW(hdc, wbuf, -1, &rcCol, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    SelectObject(hdc, oldFont);
}

/**
 * Создает стандартную таблицу Windows (SysListView32 в режиме LVS_REPORT с сеткой
 * и двойной буферизацией) с тремя колонками: "№", "Доступные" и "Вычисляемые".
 *
 * @param parent     Дескриптор родительского окна.
 * @param x          Координата X левого верхнего угла таблицы.
 * @param y          Координата Y левого верхнего угла таблицы.
 * @param w          Ширина таблицы в пикселях.
 * @param h          Высота таблицы в пикселях.
 * @param id         Идентификатор дочернего элемента управления.
 * @param col1_title Заголовок первой колонки данных ("Доступные").
 * @param col2_title Заголовок второй колонки данных ("Вычисляемые").
 * @return Дескриптор созданного элемента управления списком (HWND).
 */
static HWND create_standard_table(HWND parent, int x, int y, int w, int h, int id,
                                  const char *col1_title, const char *col2_title)
{
    HWND hLv = CreateWindowExW(
        0, WC_LISTVIEWW, NULL,
        WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_NOSORTHEADER,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageW(hLv, WM_SETFONT, (WPARAM)g_fontTable, TRUE);

    DWORD exStyle = LVS_EX_GRIDLINES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER;
    SendMessageW(hLv, LVM_SETEXTENDEDLISTVIEWSTYLE, exStyle, exStyle);

    wchar_t wbuf[128];
    LVCOLUMNW lvc = {0};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    utf8_to_wide("№", wbuf, 128);
    lvc.iSubItem = 0;
    lvc.cx = 44;
    lvc.pszText = wbuf;
    SendMessageW(hLv, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);

    utf8_to_wide(col1_title, wbuf, 128);
    lvc.iSubItem = 1;
    lvc.cx = 165;
    lvc.pszText = wbuf;
    SendMessageW(hLv, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);

    utf8_to_wide(col2_title, wbuf, 128);
    lvc.iSubItem = 2;
    lvc.cx = 198;
    lvc.pszText = wbuf;
    SendMessageW(hLv, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);

    SendMessageW(hLv, LVM_SETCOLUMNWIDTH, 2, LVSCW_AUTOSIZE_USEHEADER);
    return hLv;
}

/**
 * Добавляет новую строку с номером операции в таблицу ListView или обновляет существующую
 * строку row_idx на месте (заменяя статус "[ОЖИДАНИЕ]" вычисленным значением).
 *
 * @param hLv       Дескриптор таблицы SysListView32.
 * @param row_idx   Индекс строки операции (начиная с 0).
 * @param col1_utf8 Значение первой колонки данных в кодировке UTF-8.
 * @param col2_utf8 Значение второй колонки данных в кодировке UTF-8.
 */
static void upsert_table_row(HWND hLv, int row_idx, const char *col1_utf8, const char *col2_utf8)
{
    char num_buf[32];
    wchar_t wnum[32];
    wchar_t wcol1[128];
    wchar_t wcol2[128];

    snprintf(num_buf, sizeof(num_buf), "%d", row_idx + 1);
    utf8_to_wide(num_buf, wnum, 32);
    utf8_to_wide(col1_utf8, wcol1, 128);
    utf8_to_wide(col2_utf8, wcol2, 128);

    int count = (int)SendMessageW(hLv, LVM_GETITEMCOUNT, 0, 0);
    if (row_idx >= count)
    {
        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = row_idx;
        lvi.iSubItem = 0;
        lvi.pszText = wnum;
        int inserted = (int)SendMessageW(hLv, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

        LVITEMW lvi1 = {0};
        lvi1.iSubItem = 1;
        lvi1.pszText = wcol1;
        SendMessageW(hLv, LVM_SETITEMTEXTW, inserted, (LPARAM)&lvi1);

        LVITEMW lvi2 = {0};
        lvi2.iSubItem = 2;
        lvi2.pszText = wcol2;
        SendMessageW(hLv, LVM_SETITEMTEXTW, inserted, (LPARAM)&lvi2);

        SendMessageW(hLv, LVM_SETCOLUMNWIDTH, 2, LVSCW_AUTOSIZE_USEHEADER);
        SendMessageW(hLv, LVM_ENSUREVISIBLE, inserted, FALSE);
    }
    else
    {
        LVITEMW lvi1 = {0};
        lvi1.iSubItem = 1;
        lvi1.pszText = wcol1;
        SendMessageW(hLv, LVM_SETITEMTEXTW, row_idx, (LPARAM)&lvi1);

        LVITEMW lvi2 = {0};
        lvi2.iSubItem = 2;
        lvi2.pszText = wcol2;
        SendMessageW(hLv, LVM_SETITEMTEXTW, row_idx, (LPARAM)&lvi2);
        SendMessageW(hLv, LVM_ENSUREVISIBLE, row_idx, FALSE);
    }
}

/**
 * Создает все дочерние элементы управления главного окна для обеих вкладок
 * (вкладки, таблицы, поля ввода, ползунки скорости и кнопки управления).
 *
 * @param hwnd Дескриптор главного окна приложения.
 */
static void create_controls(HWND hwnd)
{
    wchar_t wbuf[128];

    /* --- Верхняя панель вкладок (прижата к верхнему краю окна, без номеров) --- */
    utf8_to_wide("Алгоритм Деккера", wbuf, 128);
    g_hTabBtn[0] = CreateWindowExW(0, L"BUTTON", wbuf,
                                   WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                   36, 0, TAB_BTN_W, TAB_BAR_H, hwnd, (HMENU)ID_TAB_DEKKER, NULL, NULL);

    utf8_to_wide("Читатели и Писатель", wbuf, 128);
    g_hTabBtn[1] = CreateWindowExW(0, L"BUTTON", wbuf,
                                   WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                   36 + TAB_BTN_W + 6, 0, TAB_BTN_W, TAB_BAR_H, hwnd, (HMENU)ID_TAB_RW, NULL, NULL);

    /* ========================================================================
     * Элементы Вкладки 1: Алгоритм Деккера
     * ======================================================================== */
    g_hTable[0] = create_standard_table(hwnd, TABLE1_X, TABLE_Y, TABLE_W, TABLE_H, ID_LIST_1,
                                        "Доступные (x)", "Вычисляемые (Y = x²/18)");

    g_hTable[1] = create_standard_table(hwnd, TABLE2_X, TABLE_Y, TABLE_W, TABLE_H, ID_LIST_2,
                                        "Доступные (Y из файла)", "Вычисляемые (Сумма Z)");

    utf8_to_wide("Макс. x:", wbuf, 128);
    g_hLblMax = CreateWindowExW(0, L"STATIC", wbuf,
                                WS_CHILD | WS_VISIBLE | SS_LEFT,
                                PANEL1_X + 18, PANEL1_Y + 24, 96, 22, hwnd, NULL, NULL, NULL);
    SendMessageW(g_hLblMax, WM_SETFONT, (WPARAM)g_fontSpeedVal, TRUE);

    g_hEditMax = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"12",
                                 WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 PANEL1_X + 122, PANEL1_Y + 21, 96, 26, hwnd, (HMENU)ID_EDIT_MAX, NULL, NULL);
    SendMessageW(g_hEditMax, WM_SETFONT, (WPARAM)g_fontSpeedVal, TRUE);
    g_origEditProc = (WNDPROC)SetWindowLongPtrW(g_hEditMax, GWLP_WNDPROC, (LONG_PTR)PositiveFloatEditProc);

    utf8_to_wide("Шаг (h):", wbuf, 128);
    g_hLblStep = CreateWindowExW(0, L"STATIC", wbuf,
                                 WS_CHILD | WS_VISIBLE | SS_LEFT,
                                 PANEL1_X + 18, PANEL1_Y + 62, 96, 22, hwnd, NULL, NULL, NULL);
    SendMessageW(g_hLblStep, WM_SETFONT, (WPARAM)g_fontSpeedVal, TRUE);

    g_hEditStep = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1",
                                  WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                  PANEL1_X + 122, PANEL1_Y + 59, 96, 26, hwnd, (HMENU)ID_EDIT_STEP, NULL, NULL);
    SendMessageW(g_hEditStep, WM_SETFONT, (WPARAM)g_fontSpeedVal, TRUE);
    SetWindowLongPtrW(g_hEditStep, GWLP_WNDPROC, (LONG_PTR)PositiveFloatEditProc);

    g_hSpeedLbl[0] = CreateWindowExW(0, L"STATIC", L"",
                                     WS_CHILD | WS_VISIBLE | SS_CENTER,
                                     PANEL1_X + 16, PANEL1_Y + 160, 98, 20, hwnd, NULL, NULL, NULL);
    SendMessageW(g_hSpeedLbl[0], WM_SETFONT, (WPARAM)g_fontSpeedVal, TRUE);
    set_label_int(g_hSpeedLbl[0], sim_get_speed(&g_dekker_sim, 0));

    g_hSpeedLbl[1] = CreateWindowExW(0, L"STATIC", L"",
                                     WS_CHILD | WS_VISIBLE | SS_CENTER,
                                     PANEL1_X + 122, PANEL1_Y + 160, 98, 20, hwnd, NULL, NULL, NULL);
    SendMessageW(g_hSpeedLbl[1], WM_SETFONT, (WPARAM)g_fontSpeedVal, TRUE);
    set_label_int(g_hSpeedLbl[1], sim_get_speed(&g_dekker_sim, 1));

    g_hSlider[0] = CreateWindowExW(0, TRACKBAR_CLASSW, NULL,
                                   WS_CHILD | WS_VISIBLE | TBS_VERT | TBS_BOTH | TBS_NOTICKS,
                                   PANEL1_X + 45, PANEL1_Y + 184, 40, 196, hwnd, (HMENU)ID_SLIDER_1, NULL, NULL);
    SendMessageW(g_hSlider[0], TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    SendMessageW(g_hSlider[0], TBM_SETPOS, TRUE, 100 - sim_get_speed(&g_dekker_sim, 0));

    g_hSlider[1] = CreateWindowExW(0, TRACKBAR_CLASSW, NULL,
                                   WS_CHILD | WS_VISIBLE | TBS_VERT | TBS_BOTH | TBS_NOTICKS,
                                   PANEL1_X + 151, PANEL1_Y + 184, 40, 196, hwnd, (HMENU)ID_SLIDER_2, NULL, NULL);
    SendMessageW(g_hSlider[1], TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    SendMessageW(g_hSlider[1], TBM_SETPOS, TRUE, 100 - sim_get_speed(&g_dekker_sim, 1));

    utf8_to_wide("Старт", wbuf, 128);
    g_hBtnStart[0] = CreateWindowExW(0, L"BUTTON", wbuf,
                                     WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                     PANEL1_X + 18, PANEL1_Y + 400, 94, 36, hwnd, (HMENU)ID_BTN_START_1, NULL, NULL);

    g_hBtnStart[1] = CreateWindowExW(0, L"BUTTON", wbuf,
                                     WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                     PANEL1_X + 124, PANEL1_Y + 400, 94, 36, hwnd, (HMENU)ID_BTN_START_2, NULL, NULL);

    utf8_to_wide("Пауза", wbuf, 128);
    g_hBtnPause[0] = CreateWindowExW(0, L"BUTTON", wbuf,
                                     WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                     PANEL1_X + 18, PANEL1_Y + 448, 94, 36, hwnd, (HMENU)ID_BTN_PAUSE_1, NULL, NULL);

    g_hBtnPause[1] = CreateWindowExW(0, L"BUTTON", wbuf,
                                     WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                     PANEL1_X + 124, PANEL1_Y + 448, 94, 36, hwnd, (HMENU)ID_BTN_PAUSE_2, NULL, NULL);

    utf8_to_wide("Останов", wbuf, 128);
    g_hBtnStopAll = CreateWindowExW(0, L"BUTTON", wbuf,
                                    WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_OWNERDRAW,
                                    PANEL1_X + 18, PANEL1_Y + 564, 200, 42, hwnd, (HMENU)ID_BTN_STOP_ALL, NULL, NULL);

    /* ========================================================================
     * Элементы Вкладки 2: Задача о писателе и читателях
     * ======================================================================== */
    /* Горизонтальный ползунок "Скорость чтения" */
    g_hRwReadLbl = CreateWindowExW(0, L"STATIC", L"50",
                                   WS_CHILD | SS_CENTER,
                                   PANEL2_X + 18, PANEL2_Y + 40, 28, 22, hwnd, NULL, NULL, NULL);
    SendMessageW(g_hRwReadLbl, WM_SETFONT, (WPARAM)g_fontSpeedVal, TRUE);

    g_hRwSliderRead = CreateWindowExW(0, TRACKBAR_CLASSW, NULL,
                                      WS_CHILD | TBS_HORZ | TBS_BOTH | TBS_NOTICKS,
                                      PANEL2_X + 50, PANEL2_Y + 36, 322, 30, hwnd, (HMENU)ID_RW_SLIDER_READ, NULL, NULL);
    SendMessageW(g_hRwSliderRead, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    SendMessageW(g_hRwSliderRead, TBM_SETPOS, TRUE, rw_get_cs_speed(false));

    /* Горизонтальный ползунок "Скорость записи" */
    g_hRwWriteLbl = CreateWindowExW(0, L"STATIC", L"50",
                                    WS_CHILD | SS_CENTER,
                                    PANEL2_X + 18, PANEL2_Y + 102, 28, 22, hwnd, NULL, NULL, NULL);
    SendMessageW(g_hRwWriteLbl, WM_SETFONT, (WPARAM)g_fontSpeedVal, TRUE);

    g_hRwSliderWrite = CreateWindowExW(0, TRACKBAR_CLASSW, NULL,
                                       WS_CHILD | TBS_HORZ | TBS_BOTH | TBS_NOTICKS,
                                       PANEL2_X + 50, PANEL2_Y + 98, 322, 30, hwnd, (HMENU)ID_RW_SLIDER_WRITE, NULL, NULL);
    SendMessageW(g_hRwSliderWrite, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    SendMessageW(g_hRwSliderWrite, TBM_SETPOS, TRUE, rw_get_cs_speed(true));

    /* Горизонтальный ползунок "Шанс похода читателей" */
    g_hRwChanceLbl = CreateWindowExW(0, L"STATIC", L"30",
                                     WS_CHILD | SS_CENTER,
                                     PANEL2_X + 18, PANEL2_Y + 164, 28, 22, hwnd, NULL, NULL, NULL);
    SendMessageW(g_hRwChanceLbl, WM_SETFONT, (WPARAM)g_fontSpeedVal, TRUE);
    set_label_int(g_hRwChanceLbl, rw_get_reader_chance());

    g_hRwSliderChance = CreateWindowExW(0, TRACKBAR_CLASSW, NULL,
                                        WS_CHILD | TBS_HORZ | TBS_BOTH | TBS_NOTICKS,
                                        PANEL2_X + 50, PANEL2_Y + 160, 322, 30, hwnd, (HMENU)ID_RW_SLIDER_CHANCE, NULL, NULL);
    SendMessageW(g_hRwSliderChance, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    SendMessageW(g_hRwSliderChance, TBM_SETPOS, TRUE, rw_get_reader_chance());

    /* 4 колонки: Писарь (0), Читатель 1 (1), Читатель 2 (2), Читатель 3 (3) */
    for (int i = 0; i < RW_ACTOR_COUNT; i++)
    {
        int col_x = PANEL2_X + 16 + i * 92;

        g_hRwActorSpdLbl[i] = CreateWindowExW(0, L"STATIC", L"50",
                                              WS_CHILD | SS_CENTER,
                                              col_x, PANEL2_Y + 226, 82, 20, hwnd, NULL, NULL, NULL);
        SendMessageW(g_hRwActorSpdLbl[i], WM_SETFONT, (WPARAM)g_fontSpeedVal, TRUE);
        set_label_int(g_hRwActorSpdLbl[i], sim_get_speed(&g_rw_sim, i));

        g_hRwActorSlider[i] = CreateWindowExW(0, TRACKBAR_CLASSW, NULL,
                                              WS_CHILD | TBS_VERT | TBS_BOTH | TBS_NOTICKS,
                                              col_x + 21, PANEL2_Y + 250, 40, 192, hwnd,
                                              (HMENU)(INT_PTR)(ID_RW_SLIDER_ACT0 + i), NULL, NULL);
        SendMessageW(g_hRwActorSlider[i], TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendMessageW(g_hRwActorSlider[i], TBM_SETPOS, TRUE, 100 - sim_get_speed(&g_rw_sim, i));

        utf8_to_wide("Старт", wbuf, 128);
        g_hRwBtnStart[i] = CreateWindowExW(0, L"BUTTON", wbuf,
                                           WS_CHILD | BS_OWNERDRAW,
                                           col_x, PANEL2_Y + 458, 82, 36, hwnd,
                                           (HMENU)(INT_PTR)(ID_RW_BTN_START0 + i), NULL, NULL);

        utf8_to_wide("Пауза", wbuf, 128);
        g_hRwBtnPause[i] = CreateWindowExW(0, L"BUTTON", wbuf,
                                           WS_CHILD | BS_OWNERDRAW,
                                           col_x, PANEL2_Y + 506, 82, 36, hwnd,
                                           (HMENU)(INT_PTR)(ID_RW_BTN_PAUSE0 + i), NULL, NULL);
    }

    /* Общая кнопка "Останов" снизу панели Вкладки 2 */
    utf8_to_wide("Останов", wbuf, 128);
    g_hRwBtnStopAll = CreateWindowExW(0, L"BUTTON", wbuf,
                                      WS_CHILD | WS_DISABLED | BS_OWNERDRAW,
                                      PANEL2_X + 16, PANEL2_Y + 580, 358, 44, hwnd, (HMENU)ID_RW_BTN_STOP_ALL, NULL, NULL);
}

/**
 * Главная оконная процедура приложения: обрабатывает сообщения таймера анимации,
 * двойной буферизации WM_PAINT, прокрутки ползунков, нажатия кнопок и межпоточных событий.
 *
 * @param hwnd   Дескриптор главного окна.
 * @param msg    Код оконного сообщения Windows.
 * @param wParam Числовой параметр сообщения.
 * @param lParam Указатель или дополнительный параметр сообщения.
 * @return Результат обработки сообщения (LRESULT).
 */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        create_controls(hwnd);
        SetTimer(hwnd, ID_TIMER_ANIM, ANIM_INTERVAL_MS, NULL);
        return 0;

    case WM_TIMER:
        if (wParam == ID_TIMER_ANIM)
        {
            tick_animation();
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        /* Двойная буферизация для абсолютно бесшовной отрисовки без мерцания */
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, WIN_CLIENT_W, WIN_CLIENT_H);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        paint_window_content(memDC);

        BitBlt(hdc, 0, 0, WIN_CLIENT_W, WIN_CLIENT_H, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        if (hCtrl == g_hEditMax || hCtrl == g_hEditStep)
        {
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
        SetTextColor(hdc, COL_TEXT_DARK);
        SetBkColor(hdc, COL_PANEL_BG);
        return (LRESULT)g_brPanelBg;
    }

    case WM_HSCROLL:
    {
        HWND hScroll = (HWND)lParam;
        if (hScroll == g_hRwSliderRead)
        {
            int spd = (int)SendMessageW(g_hRwSliderRead, TBM_GETPOS, 0, 0);
            rw_set_cs_speed(false, spd);
            set_label_int(g_hRwReadLbl, spd);
        }
        else if (hScroll == g_hRwSliderWrite)
        {
            int spd = (int)SendMessageW(g_hRwSliderWrite, TBM_GETPOS, 0, 0);
            rw_set_cs_speed(true, spd);
            set_label_int(g_hRwWriteLbl, spd);
        }
        else if (hScroll == g_hRwSliderChance)
        {
            int chance = (int)SendMessageW(g_hRwSliderChance, TBM_GETPOS, 0, 0);
            rw_set_reader_chance(chance);
            set_label_int(g_hRwChanceLbl, chance);
        }
        return 0;
    }

    case WM_VSCROLL:
    {
        HWND hScroll = (HWND)lParam;
        if (hScroll == g_hSlider[0])
        {
            int pos = (int)SendMessageW(g_hSlider[0], TBM_GETPOS, 0, 0);
            int spd = 100 - pos;
            sim_set_speed(&g_dekker_sim, 0, spd);
            set_label_int(g_hSpeedLbl[0], spd);
        }
        else if (hScroll == g_hSlider[1])
        {
            int pos = (int)SendMessageW(g_hSlider[1], TBM_GETPOS, 0, 0);
            int spd = 100 - pos;
            sim_set_speed(&g_dekker_sim, 1, spd);
            set_label_int(g_hSpeedLbl[1], spd);
        }
        else
        {
            for (int i = 0; i < RW_ACTOR_COUNT; i++)
            {
                if (hScroll == g_hRwActorSlider[i])
                {
                    int pos = (int)SendMessageW(g_hRwActorSlider[i], TBM_GETPOS, 0, 0);
                    int spd = 100 - pos;
                    sim_set_speed(&g_rw_sim, i, spd);
                    set_label_int(g_hRwActorSpdLbl[i], spd);
                    break;
                }
            }
        }
        return 0;
    }

    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlType == ODT_BUTTON)
        {
            draw_rounded_button(dis);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        /* Переключение вкладок */
        if (id == ID_TAB_DEKKER)
        {
            switch_tab(0);
        }
        else if (id == ID_TAB_RW)
        {
            switch_tab(1);
        }
        /* Кнопки Вкладки 1 (Алгоритм Деккера) */
        else if (id == ID_BTN_START_1)
        {
            apply_user_params();
            dekker_start_thread(0);
        }
        else if (id == ID_BTN_START_2)
        {
            apply_user_params();
            dekker_start_thread(1);
        }
        else if (id == ID_BTN_PAUSE_1)
        {
            sim_toggle_paused(&g_dekker_sim, 0);
            update_pause_btn_text(g_hBtnPause[0], &g_dekker_sim, 0);
        }
        else if (id == ID_BTN_PAUSE_2)
        {
            sim_toggle_paused(&g_dekker_sim, 1);
            update_pause_btn_text(g_hBtnPause[1], &g_dekker_sim, 1);
        }
        else if (id == ID_BTN_STOP_ALL)
        {
            dekker_stop_all();
        }
        /* Кнопки Вкладки 2 (Читатели и Писатель) */
        else if (id >= ID_RW_BTN_START0 && id < ID_RW_BTN_START0 + RW_ACTOR_COUNT)
        {
            rw_start_actor(id - ID_RW_BTN_START0);
        }
        else if (id >= ID_RW_BTN_PAUSE0 && id < ID_RW_BTN_PAUSE0 + RW_ACTOR_COUNT)
        {
            int actor_id = id - ID_RW_BTN_PAUSE0;
            sim_toggle_paused(&g_rw_sim, actor_id);
            update_pause_btn_text(g_hRwBtnPause[actor_id], &g_rw_sim, actor_id);
        }
        else if (id == ID_RW_BTN_STOP_ALL)
        {
            rw_stop_all();
        }
        return 0;
    }

    case WM_APP_ROW:
    {
        RowPacket *pkt = (RowPacket *)lParam;
        if (pkt)
        {
            int tid = pkt->thread_id;
            if (tid >= 0 && tid <= 1 && pkt->gen == g_gen[tid])
            {
                upsert_table_row(g_hTable[tid], pkt->row_idx, pkt->col1, pkt->col2);
            }
            free(pkt->col1);
            free(pkt->col2);
            free(pkt);
        }
        return 0;
    }

    case WM_APP_STATE:
    {
        BOOL session_active = (wParam & 1) != 0;
        BOOL start0_en = (wParam & 2) != 0;
        BOOL start1_en = (wParam & 4) != 0;

        g_dekker_active = (session_active != FALSE);
        update_tab_locks();

        EnableWindow(g_hEditMax, !session_active);
        EnableWindow(g_hEditStep, !session_active);
        EnableWindow(g_hBtnStart[0], start0_en);
        EnableWindow(g_hBtnStart[1], start1_en);
        EnableWindow(g_hBtnStopAll, session_active);

        if (!session_active)
        {
            update_pause_btn_text(g_hBtnPause[0], &g_dekker_sim, 0);
            update_pause_btn_text(g_hBtnPause[1], &g_dekker_sim, 1);
        }

        InvalidateRect(g_hBtnStart[0], NULL, TRUE);
        InvalidateRect(g_hBtnStart[1], NULL, TRUE);
        InvalidateRect(g_hBtnStopAll, NULL, TRUE);
        return 0;
    }

    case WM_APP_RW_STATE:
    {
        BOOL session_active = (wParam & 1) != 0;
        g_rw_active = (session_active != FALSE);
        update_tab_locks();

        for (int i = 0; i < RW_ACTOR_COUNT; i++)
        {
            BOOL start_en = (wParam & (1 << (i + 1))) != 0;
            EnableWindow(g_hRwBtnStart[i], start_en);
            InvalidateRect(g_hRwBtnStart[i], NULL, TRUE);
            if (!session_active)
            {
                update_pause_btn_text(g_hRwBtnPause[i], &g_rw_sim, i);
            }
        }
        EnableWindow(g_hRwBtnStopAll, session_active);
        InvalidateRect(g_hRwBtnStopAll, NULL, TRUE);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, ID_TIMER_ANIM);
        dekker_stop_all();
        rw_stop_all();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/**
 * Инициализирует библиотеку Common Controls и GDI+, загружает PNG-спрайты, создает шрифты,
 * регистрирует класс окна и запускает главный цикл обработки сообщений Windows.
 *
 * @return Код возврата из цикла сообщений (wParam сообщения WM_QUIT).
 */
int ui_run(void)
{
    InitCommonControls();

    /* Инициализация GDI+ и загрузка PNG-спрайтов */
    GdiplusStartupInput gdiplusStartupInput = {1, NULL, FALSE, FALSE};
    GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);

    g_imgWriter = load_png_asset("static/img/biblyotekar.png");
    g_imgReader = load_png_asset("static/img/actor.png");
    g_imgBook = load_png_asset("static/img/book.png");

    HINSTANCE hInst = GetModuleHandleW(NULL);

    g_brWhite = CreateSolidBrush(COL_BG_MAIN);
    g_brPanelBg = CreateSolidBrush(COL_PANEL_BG);

    g_fontSection = CreateFontW(-20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    g_fontPanelHdr = CreateFontW(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    g_fontColHdr = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    g_fontSpeedVal = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    g_fontButton = CreateFontW(-12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    g_fontTable = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    g_fontActorNum = CreateFontW(-38, 0, 0, 0, FW_HEAVY, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = g_brWhite;
    wc.lpszClassName = L"OsSyncLabWindowClass";

    if (!RegisterClassExW(&wc))
    {
        return 1;
    }

    RECT rc = {0, 0, WIN_CLIENT_W, WIN_CLIENT_H};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    AdjustWindowRect(&rc, style, FALSE);

    wchar_t wTitle[128];
    utf8_to_wide("ЛР 3, Шеклеин К.М.", wTitle, 128);

    g_hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        wTitle,
        style | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, hInst, NULL);

    if (!g_hwnd)
    {
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_imgWriter)
        GdipDisposeImage(g_imgWriter);
    if (g_imgReader)
        GdipDisposeImage(g_imgReader);
    if (g_imgBook)
        GdipDisposeImage(g_imgBook);
    if (g_gdiplusToken)
        GdiplusShutdown(g_gdiplusToken);

    DeleteObject(g_fontSection);
    DeleteObject(g_fontPanelHdr);
    DeleteObject(g_fontColHdr);
    DeleteObject(g_fontSpeedVal);
    DeleteObject(g_fontButton);
    DeleteObject(g_fontTable);
    DeleteObject(g_fontActorNum);
    DeleteObject(g_brWhite);
    DeleteObject(g_brPanelBg);

    return (int)msg.wParam;
}
