#include "dekker.h"
#include <stdio.h>
#include <math.h>
#include <windows.h>

/* Общие переменные алгоритма Деккера */
static volatile bool flag[2] = {false, false};
static volatile int turn = 0;

/* Разделяемые данные вычислительной задачи */
static double Z = 0.0;
static double g_x_max = DEFAULT_X_MAX;
static double g_step_h = DEFAULT_STEP;
static int g_total_steps = 12;

/* Контроллер потоков симуляции (скорость, пауза, жизненный цикл) */
SimGroup g_dekker_sim;

static DekkerRowCallback g_row_cb = NULL;
static DekkerClearCallback g_clear_cb = NULL;

/**
 * Отправляет строку результатов или статуса ожидания в таблицу указанного потока через UI-коллбэк.
 *
 * @param thread_id Идентификатор вычислительного потока (0 - запись, 1 - чтение).
 * @param row_idx   Индекс строки операции в таблице (начиная с 0).
 * @param col1      Текст первой колонки данных ("Доступные").
 * @param col2      Текст второй колонки данных ("Вычисляемые" или "[ОЖИДАНИЕ]").
 */
static void emit_row(int thread_id, int row_idx, const char *col1, const char *col2)
{
    if (!g_row_cb || !g_dekker_sim.running[thread_id])
        return;
    g_row_cb(thread_id, row_idx, col1, col2);
}

/**
 * Вход в критическую секцию по алгоритму Деккера для потока id (0 или 1).
 * При занятости критической секции другим потоком выводит статус "Ожидание".
 *
 * @param id        Идентификатор текущего потока (0 или 1).
 * @param row_idx   Номер текущей строки в таблице для отображения ожидания.
 * @param wait_col1 Текст первой колонки во время ожидания входа в КС.
 * @return true, если вход в критическую секцию выполнен успешно; false, если поток остановлен.
 */
static bool dekker_enter_cs(int id, int row_idx, const char *wait_col1)
{
    int other = 1 - id;
    flag[id] = true;
    MemoryBarrier();

    while (flag[other] && g_dekker_sim.running[id])
    {
        emit_row(id, row_idx, wait_col1, "[ОЖИДАНИЕ]");
        if (turn == other)
        {
            flag[id] = false;
            MemoryBarrier();
            while (turn == other && g_dekker_sim.running[id])
            {
                if (!sim_sleep(&g_dekker_sim, id))
                {
                    flag[id] = false;
                    return false;
                }
                if (turn == other && g_dekker_sim.running[id])
                {
                    emit_row(id, row_idx, wait_col1, "[ОЖИДАНИЕ]");
                }
            }
            flag[id] = true;
            MemoryBarrier();
        }
        else
        {
            if (!sim_sleep(&g_dekker_sim, id))
            {
                flag[id] = false;
                return false;
            }
        }
    }

    if (!g_dekker_sim.running[id])
    {
        flag[id] = false;
        return false;
    }
    return true;
}

/**
 * Выход из критической секции по алгоритму Деккера: передает очередь
 * другому потоку (turn = 1 - id) и снимает флаг готовности текущего потока.
 *
 * @param id Идентификатор выходящего из КС потока (0 или 1).
 */
static void dekker_leave_cs(int id)
{
    turn = 1 - id;
    flag[id] = false;
    MemoryBarrier();
}

/**
 * Рабочая функция первого потока (Process0): синхронизируется по алгоритму Деккера,
 * вычисляет значения функции Y = x^2 / 18 на заданном интервале и записывает их в файл.
 *
 * @param lpParam Пользовательский параметр потока WinAPI (не используется).
 * @return Код завершения потока (0).
 */
static DWORD WINAPI Process0(LPVOID lpParam)
{
    (void)lpParam;
    char col1[128];
    char col2[128];
    int total = g_total_steps;
    double step = g_step_h;

    for (int idx = 0; idx < total && g_dekker_sim.running[0]; idx++)
    {
        if (!sim_wait_active(&g_dekker_sim, 0, NULL))
            break;

        double x = X_START + idx * step;
        snprintf(col1, sizeof(col1), "x = %.4g", x);

        if (!dekker_enter_cs(0, idx, col1))
            break;

        /* Критический раздел */
        if (!sim_sleep(&g_dekker_sim, 0))
        {
            flag[0] = false;
            break;
        }

        double y = (x * x) / 18.0;
        FILE *fp = fopen(FILE_NAME, "a");
        if (fp != NULL)
        {
            fprintf(fp, "%.10f\n", y);
            fclose(fp);
        }

        snprintf(col2, sizeof(col2), "Y = %.6f", y);
        emit_row(0, idx, col1, col2);

        dekker_leave_cs(0);

        /* Внекритический код */
        if (!sim_sleep(&g_dekker_sim, 0))
            break;
    }

    flag[0] = false;
    g_dekker_sim.running[0] = false;
    MemoryBarrier();

    sim_check_completion(&g_dekker_sim);
    return 0;
}

/**
 * Рабочая функция второго потока (Process1): синхронизируется по алгоритму Деккера,
 * считывает очередное значение Y из общего файла и накапливает сумму в переменной Z.
 *
 * @param lpParam Пользовательский параметр потока WinAPI (не используется).
 * @return Код завершения потока (0).
 */
static DWORD WINAPI Process1(LPVOID lpParam)
{
    (void)lpParam;
    int count = 0;
    int total_steps = g_total_steps;
    long read_pos = 0;
    char col1[128];
    char col2[128];

    while (count < total_steps && g_dekker_sim.running[1])
    {
        if (!sim_wait_active(&g_dekker_sim, 1, NULL))
            break;

        if (!dekker_enter_cs(1, count, "[ОЖИДАНИЕ]"))
            break;

        /* Критический раздел */
        if (!sim_sleep(&g_dekker_sim, 1))
        {
            flag[1] = false;
            break;
        }

        bool got_value = false;
        double y = 0.0;

        FILE *fp = fopen(FILE_NAME, "rb");
        if (fp != NULL)
        {
            fseek(fp, read_pos, SEEK_SET);
            if (fscanf(fp, "%lf", &y) == 1)
            {
                read_pos = ftell(fp);
                Z += y;
                got_value = true;
            }
            fclose(fp);
        }

        if (got_value)
        {
            snprintf(col1, sizeof(col1), "Y = %.6f", y);
            snprintf(col2, sizeof(col2), "Z = %.6f", Z);
            emit_row(1, count, col1, col2);
            count++;
        }

        dekker_leave_cs(1);

        /* Внекритический код */
        if (!sim_sleep(&g_dekker_sim, 1))
            break;
    }

    flag[1] = false;
    g_dekker_sim.running[1] = false;
    MemoryBarrier();

    sim_check_completion(&g_dekker_sim);
    return 0;
}

/**
 * Инициализирует модуль алгоритма Деккера и связывает его с коллбэками графического интерфейса.
 *
 * @param row_cb   Коллбэк добавления или обновления строки в таблице потока.
 * @param clear_cb Коллбэк очистки таблицы потока.
 * @param state_cb Коллбэк уведомления UI об активности сессии и доступности кнопок "Старт".
 */
void dekker_init(DekkerRowCallback row_cb, DekkerClearCallback clear_cb, SimStateCallback state_cb)
{
    g_row_cb = row_cb;
    g_clear_cb = clear_cb;
    sim_group_init(&g_dekker_sim, 2, DEKKER_DELAY_MIN, DEKKER_DELAY_MAX, state_cb);
}

/**
 * Задает параметры интервала вычислений (максимальное значение x и шаг h) перед стартом потоков.
 *
 * @param x_max  Верхняя граница интервала x (не меньше X_START = 1.0).
 * @param step_h Шаг изменения аргумента h (положительное число).
 */
void dekker_set_params(double x_max, double step_h)
{
    if (g_dekker_sim.session_active)
        return;
    if (step_h <= 0.0001)
        step_h = DEFAULT_STEP;
    if (x_max < X_START)
        x_max = X_START;

    g_x_max = x_max;
    g_step_h = step_h;
    g_total_steps = (int)floor((g_x_max - X_START + 1e-9) / g_step_h) + 1;
    if (g_total_steps < 1)
        g_total_steps = 1;
    if (g_total_steps > 10000)
        g_total_steps = 10000;
}

/**
 * Запускает выбранный вычислительный поток алгоритма Деккера (0 - запись, 1 - чтение).
 *
 * @param thread_id Идентификатор запускаемого потока (0 или 1).
 */
void dekker_start_thread(int thread_id)
{
    if (thread_id < 0 || thread_id > 1)
        return;
    if (g_dekker_sim.started[thread_id])
        return;

    flag[thread_id] = false;
    sim_stop_thread(&g_dekker_sim, thread_id);

    if (thread_id == 0)
    {
        FILE *fp = fopen(FILE_NAME, "w");
        if (fp != NULL)
            fclose(fp);
        turn = 0;
    }
    else
    {
        FILE *fp = fopen(FILE_NAME, "a");
        if (fp != NULL)
            fclose(fp);
        Z = 0.0;
    }

    if (g_clear_cb)
    {
        g_clear_cb(thread_id);
    }

    LPTHREAD_START_ROUTINE proc = (thread_id == 0) ? Process0 : Process1;
    sim_start_thread(&g_dekker_sim, thread_id, proc, NULL);
}

/**
 * Полностью останавливает оба потока алгоритма Деккера, очищает рабочий файл и таблицы.
 */
void dekker_stop_all(void)
{
    flag[0] = false;
    flag[1] = false;
    sim_stop_all(&g_dekker_sim);

    FILE *fp = fopen(FILE_NAME, "w");
    if (fp != NULL)
        fclose(fp);

    Z = 0.0;
    turn = 0;
    MemoryBarrier();

    if (g_clear_cb)
    {
        g_clear_cb(-1);
    }
    sim_notify_state(&g_dekker_sim);
}
