#include "rw.h"
#include <windows.h>

/* Системные критические секции Windows API */
static CRITICAL_SECTION g_cs_state; /* Защита счетчиков и приоритета писателя */
static CRITICAL_SECTION g_cs_write; /* Эксклюзивный доступ к записи данных */
static bool g_cs_initialized = false;

/* Разделяемые переменные задачи "Читатели - Писатель" (приоритет писателя) */
static volatile int g_active_readers = 0;
static volatile int g_waiting_writers = 0;
static volatile bool g_writer_active = false;
static volatile DWORD g_cs_epoch = 0;

/* Визуальные состояния 4 участников (0 - Писарь, 1..3 - Читатели) */
static volatile RWActorState g_actor_state[RW_ACTOR_COUNT] = {
    RW_STATE_IDLE, RW_STATE_IDLE, RW_STATE_IDLE, RW_STATE_IDLE};

/* Флаги достижения целевой позиции анимации для каждого участника */
static volatile bool g_actor_arrived[RW_ACTOR_COUNT] = {true, true, true, true};

/* Ползунки скорости внутри критической секции: [0] - чтение, [1] - запись */
static volatile int g_cs_speed[2] = {SIM_DEFAULT_SPEED, SIM_DEFAULT_SPEED};

/* Программная вероятность (в %) желания посетить КС после ожидания дома для читателей */
static volatile int g_reader_visit_chance = RW_READER_VISIT_CHANCE_DEFAULT;

/* Индивидуальные состояния генератора псевдослучайных чисел для потоков участников */
static volatile DWORD g_rng_state[RW_ACTOR_COUNT] = {0, 0, 0, 0};

/* Контроллер потоков симуляции (скорость ожидания перед заходом в КС, пауза, жизненный цикл) */
SimGroup g_rw_sim;

/**
 * Программно определяет, захочет ли участник посещать критическую секцию в текущем цикле
 * после ожидания на исходной позиции, согласно заданной частоте (вероятности в процентах).
 *
 * @param actor_id Идентификатор участника (0 - Писарь, 1..3 - Читатели).
 * @return true, если участник решил идти к книге; false, если остается дома еще на один цикл.
 */
static bool rw_should_visit_cs(int actor_id)
{
    if (actor_id < 0 || actor_id >= RW_ACTOR_COUNT)
        return false;

    DWORD s = g_rng_state[actor_id];
    if (s == 0)
    {
        LARGE_INTEGER qpc;
        QueryPerformanceCounter(&qpc);
        s = (DWORD)qpc.LowPart ^ GetTickCount() ^ ((DWORD)(actor_id + 1) * 2654435761u);
        if (s == 0)
            s = 1;
    }
    /* Алгоритм Xorshift32 для качественного независимого распределения по потокам */
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    g_rng_state[actor_id] = s;

    int chance = (actor_id == RW_WRITER_ID) ? RW_WRITER_VISIT_CHANCE : g_reader_visit_chance;
    int roll = (int)(s % 100u);
    if (roll < chance)
        return true;

    /* Если КС сейчас занята, а участник решил не вставать в очередь в этом такте,
     * он остается дома до завершения текущего визита в КС, чтобы не бросать жребий повторно */
    DWORD epoch = g_cs_epoch;
    while (g_rw_sim.running[actor_id] &&
           (g_writer_active || g_active_readers > 0) &&
           g_cs_epoch == epoch)
    {
        if (!sim_wait_active(&g_rw_sim, actor_id, NULL))
            return false;
        Sleep(SIM_SLEEP_TICK_MS);
    }
    return false;
}

/**
 * Устанавливает новое логическое состояние участника и сбрасывает флаг прибытия
 * в целевую точку, если состояние изменилось.
 *
 * @param actor_id Идентификатор участника (0 - Писарь, 1..3 - Читатели).
 * @param state    Новое состояние участника (RW_STATE_IDLE, RW_STATE_WAITING, RW_STATE_IN_CS).
 */
static void set_actor_state(int actor_id, RWActorState state)
{
    if (g_actor_state[actor_id] != state)
    {
        g_actor_arrived[actor_id] = false;
        g_actor_state[actor_id] = state;
        MemoryBarrier();
    }
}

/**
 * Ожидает завершения плавного перемещения спрайта участника в целевую точку
 * (на исходное место перед началом отсчета ожидания или к книге перед началом чтения/записи).
 *
 * @param actor_id Идентификатор участника (0 - Писарь, 1..3 - Читатели).
 * @return true, если персонаж достиг точки и поток продолжает работу; false при останове.
 */
static bool wait_actor_arrival(int actor_id)
{
    int elapsed = 0;
    while (g_rw_sim.running[actor_id] && !g_actor_arrived[actor_id] && elapsed < 1200)
    {
        Sleep(SIM_SLEEP_TICK_MS);
        elapsed += SIM_SLEEP_TICK_MS;
    }
    return g_rw_sim.running[actor_id];
}

/**
 * Рабочая функция потока Писателя (приоритет писателя). Использует системные функции
 * EnterCriticalSection() и LeaveCriticalSection() для блокировки входа новых читателей,
 * ожидания завершения текущих чтений и монопольной записи в книгу.
 *
 * @param lpParam Идентификатор участника (RW_WRITER_ID = 0), переданный через LPVOID.
 * @return Код завершения потока (0).
 */
static DWORD WINAPI WriterThread(LPVOID lpParam)
{
    int id = (int)(INT_PTR)lpParam;

    while (g_rw_sim.running[id])
    {
        /* 1. Некритическая секция: возвращение на исходную точку и ожидание перед заходом в КС */
        set_actor_state(id, RW_STATE_IDLE);
        if (!wait_actor_arrival(id))
            break;
        if (!sim_sleep(&g_rw_sim, id))
            break;
        if (!rw_should_visit_cs(id))
            continue;

        /* 2. Протокол входа с приоритетом писателя */
        bool entered = false;
        while (g_rw_sim.running[id])
        {
            /* Если писатель заторможен перед входом, снимаем бронь очереди,
             * чтобы не блокировать вход для активных читателей */
            if (sim_is_paused(&g_rw_sim, id) || sim_get_speed(&g_rw_sim, id) <= SIM_SPEED_MIN)
            {
                EnterCriticalSection(&g_cs_state);
                g_waiting_writers = 0;
                LeaveCriticalSection(&g_cs_state);

                if (!sim_wait_active(&g_rw_sim, id, NULL))
                    break;
                continue;
            }

            EnterCriticalSection(&g_cs_state);
            if (g_active_readers == 0)
            {
                g_waiting_writers = 0;
                g_writer_active = true;
                set_actor_state(id, RW_STATE_IN_CS);
                entered = true;
            }
            else
            {
                g_waiting_writers = 1;
                set_actor_state(id, RW_STATE_WAITING);
            }
            LeaveCriticalSection(&g_cs_state);

            if (entered)
                break;
            Sleep(SIM_SLEEP_TICK_MS);
        }

        if (!entered)
        {
            EnterCriticalSection(&g_cs_state);
            g_waiting_writers = 0;
            LeaveCriticalSection(&g_cs_state);
            break;
        }

        /* 3. Критическая секция: подход к книге и монопольная запись в книгу */
        EnterCriticalSection(&g_cs_write);
        set_actor_state(id, RW_STATE_IN_CS);

        if (wait_actor_arrival(id))
        {
            sim_sleep_custom(&g_rw_sim, id, &g_cs_speed[1]);
        }

        LeaveCriticalSection(&g_cs_write);

        /* 4. Протокол выхода из критической секции */
        EnterCriticalSection(&g_cs_state);
        g_writer_active = false;
        g_cs_epoch++;
        set_actor_state(id, RW_STATE_IDLE);
        LeaveCriticalSection(&g_cs_state);
    }

    g_actor_state[id] = RW_STATE_IDLE;
    g_actor_arrived[id] = true;
    g_rw_sim.running[id] = false;
    return 0;
}

/**
 * Рабочая функция потока Читателя. Использует EnterCriticalSection() и LeaveCriticalSection()
 * для проверки отсутствия активного или ожидающего писателя (уступая приоритет писателю)
 * и обеспечивает совместное чтение книги до трех читателей одновременно.
 *
 * @param lpParam Идентификатор читателя (1..3), переданный через LPVOID.
 * @return Код завершения потока (0).
 */
static DWORD WINAPI ReaderThread(LPVOID lpParam)
{
    int id = (int)(INT_PTR)lpParam;

    while (g_rw_sim.running[id])
    {
        /* 1. Некритическая секция: возвращение на исходную точку и ожидание перед заходом в КС */
        set_actor_state(id, RW_STATE_IDLE);
        if (!wait_actor_arrival(id))
            break;
        if (!sim_sleep(&g_rw_sim, id))
            break;
        if (!rw_should_visit_cs(id))
            continue;

        /* 2. Протокол входа читателя (проверка приоритета писателя) */
        bool entered = false;
        while (g_rw_sim.running[id])
        {
            if (!sim_wait_active(&g_rw_sim, id, NULL))
                break;

            EnterCriticalSection(&g_cs_state);
            if (!g_writer_active && g_waiting_writers == 0)
            {
                g_active_readers++;
                set_actor_state(id, RW_STATE_IN_CS);
                entered = true;
            }
            else
            {
                set_actor_state(id, RW_STATE_WAITING);
            }
            LeaveCriticalSection(&g_cs_state);

            if (entered)
                break;
            Sleep(SIM_SLEEP_TICK_MS);
        }

        if (!entered)
            break;

        /* 3. Критическая секция: подход к книге и чтение (до 3 читателей одновременно у книги) */
        if (wait_actor_arrival(id))
        {
            sim_sleep_custom(&g_rw_sim, id, &g_cs_speed[0]);
        }

        /* 4. Протокол выхода читателя */
        EnterCriticalSection(&g_cs_state);
        if (g_active_readers > 0)
            g_active_readers--;
        g_cs_epoch++;
        set_actor_state(id, RW_STATE_IDLE);
        LeaveCriticalSection(&g_cs_state);
    }

    g_actor_state[id] = RW_STATE_IDLE;
    g_actor_arrived[id] = true;
    g_rw_sim.running[id] = false;
    return 0;
}

/**
 * Инициализирует модуль задачи "Читатели - Писатель" и системные критические секции Windows API.
 *
 * @param state_cb Коллбэк уведомления UI об активности сессии и доступности кнопок "Старт".
 */
void rw_init(SimStateCallback state_cb)
{
    sim_group_init(&g_rw_sim, RW_ACTOR_COUNT, RW_DELAY_MIN, RW_DELAY_MAX, state_cb);
    if (!g_cs_initialized)
    {
        InitializeCriticalSection(&g_cs_state);
        InitializeCriticalSection(&g_cs_write);
        g_cs_initialized = true;
    }
}

/**
 * Останавливает все активные потоки задачи "Читатели - Писатель" и удаляет
 * системные критические секции при завершении работы приложения.
 */
void rw_cleanup(void)
{
    rw_stop_all();
    if (g_cs_initialized)
    {
        DeleteCriticalSection(&g_cs_state);
        DeleteCriticalSection(&g_cs_write);
        g_cs_initialized = false;
    }
}

/**
 * Запускает поток выбранного участника (Писаря или одного из трех Читателей).
 *
 * @param actor_id Идентификатор запускаемого участника (0 - Писарь, 1..3 - Читатели).
 */
void rw_start_actor(int actor_id)
{
    if (actor_id < 0 || actor_id >= RW_ACTOR_COUNT)
        return;
    if (g_rw_sim.started[actor_id])
        return;

    sim_stop_thread(&g_rw_sim, actor_id);
    g_actor_state[actor_id] = RW_STATE_IDLE;
    g_actor_arrived[actor_id] = true;

    LPTHREAD_START_ROUTINE proc = (actor_id == RW_WRITER_ID) ? WriterThread : ReaderThread;
    sim_start_thread(&g_rw_sim, actor_id, proc, (LPVOID)(INT_PTR)actor_id);
}

/**
 * Принудительно останавливает потоки всех участников, сбрасывает счетчики
 * и возвращает всех персонажей на исходные позиции.
 */
void rw_stop_all(void)
{
    sim_stop_all(&g_rw_sim);
    for (int i = 0; i < RW_ACTOR_COUNT; i++)
    {
        g_actor_state[i] = RW_STATE_IDLE;
        g_actor_arrived[i] = true;
    }

    if (g_cs_initialized)
    {
        EnterCriticalSection(&g_cs_state);
        g_active_readers = 0;
        g_waiting_writers = 0;
        g_writer_active = false;
        LeaveCriticalSection(&g_cs_state);
    }

    sim_notify_state(&g_rw_sim);
}

/**
 * Устанавливает скорость работы в критической секции (чтения для читателей или записи для писаря).
 *
 * @param is_write true - изменить скорость записи; false - изменить скорость чтения.
 * @param speed    Значение скорости в диапазоне от 0 до 100.
 */
void rw_set_cs_speed(bool is_write, int speed)
{
    int idx = is_write ? 1 : 0;
    g_cs_speed[idx] = sim_clamp_speed(speed);
    MemoryBarrier();
}

/**
 * Возвращает текущую скорость работы в критической секции (чтения или записи).
 *
 * @param is_write true - получить скорость записи; false - получить скорость чтения.
 * @return Значение скорости (0..100).
 */
int rw_get_cs_speed(bool is_write)
{
    return g_cs_speed[is_write ? 1 : 0];
}

/**
 * Возвращает текущее логическое состояние участника для визуализации на сцене.
 *
 * @param actor_id Идентификатор участника (0 - Писарь, 1..3 - Читатели).
 * @return Состояние участника (RW_STATE_IDLE, RW_STATE_WAITING или RW_STATE_IN_CS).
 */
RWActorState rw_get_actor_state(int actor_id)
{
    if (actor_id < 0 || actor_id >= RW_ACTOR_COUNT)
        return RW_STATE_IDLE;
    return g_actor_state[actor_id];
}

/**
 * Уведомляет модуль синхронизации о том, что спрайт участника завершил анимацию
 * перехода и достиг целевой точки (исходного места или книги).
 *
 * @param actor_id Идентификатор участника (0 - Писарь, 1..3 - Читатели).
 */
void rw_notify_actor_arrived(int actor_id)
{
    if (actor_id < 0 || actor_id >= RW_ACTOR_COUNT)
        return;
    g_actor_arrived[actor_id] = true;
    MemoryBarrier();
}

/**
 * Устанавливает вероятность посещения критической секции читателями.
 *
 * @param chance Вероятность в процентах (0..100).
 */
void rw_set_reader_chance(int chance)
{
    g_reader_visit_chance = sim_clamp_speed(chance);
    MemoryBarrier();
}

/**
 * Возвращает текущую вероятность посещения критической секции читателями.
 *
 * @return Вероятность в процентах (0..100).
 */
int rw_get_reader_chance(void)
{
    return g_reader_visit_chance;
}
