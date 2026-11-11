#include "sim_control.h"

/**
 * Ограничивает значение скорости допустимым диапазоном [SIM_SPEED_MIN; SIM_SPEED_MAX].
 *
 * @param speed Исходное значение скорости.
 * @return Нормализованное значение скорости в диапазоне 0..100.
 */
int sim_clamp_speed(int speed)
{
    if (speed < SIM_SPEED_MIN)
        return SIM_SPEED_MIN;
    if (speed > SIM_SPEED_MAX)
        return SIM_SPEED_MAX;
    return speed;
}

/**
 * Инициализирует контроллер группы потоков симуляции значениями по умолчанию.
 *
 * @param grp          Указатель на структуру группы потоков SimGroup.
 * @param count        Количество потоков в группе (от 1 до SIM_MAX_THREADS).
 * @param delay_min_ms Минимальная задержка шага (при скорости 100), в миллисекундах.
 * @param delay_max_ms Максимальная задержка шага (при малой скорости), в миллисекундах.
 * @param state_cb     Коллбэк для уведомления UI о состоянии кнопок.
 */
void sim_group_init(SimGroup *grp, int count, int delay_min_ms, int delay_max_ms, SimStateCallback state_cb)
{
    if (!grp)
        return;
    if (count < 1)
        count = 1;
    if (count > SIM_MAX_THREADS)
        count = SIM_MAX_THREADS;

    grp->count = count;
    grp->delay_min_ms = delay_min_ms;
    grp->delay_max_ms = delay_max_ms;
    grp->session_active = false;
    grp->state_cb = state_cb;

    for (int i = 0; i < SIM_MAX_THREADS; i++)
    {
        grp->threads[i] = NULL;
        grp->running[i] = false;
        grp->started[i] = false;
        grp->paused[i] = false;
        grp->speed[i] = SIM_DEFAULT_SPEED;
    }
}

/**
 * Отправляет в UI актуальное состояние активности сессии и флагов доступности кнопок "Старт".
 *
 * @param grp Указатель на группу потоков SimGroup.
 */
void sim_notify_state(SimGroup *grp)
{
    if (!grp || !grp->state_cb)
        return;
    bool start_en[SIM_MAX_THREADS];
    for (int i = 0; i < grp->count; i++)
    {
        start_en[i] = !grp->started[i];
    }
    grp->state_cb(grp->session_active, start_en, grp->count);
}

/**
 * Проверяет, завершились ли все потоки группы, и при полном завершении сбрасывает флаг сессии.
 *
 * @param grp Указатель на группу потоков SimGroup.
 */
void sim_check_completion(SimGroup *grp)
{
    if (!grp)
        return;
    bool any_running = false;
    for (int i = 0; i < grp->count; i++)
    {
        if (grp->running[i])
        {
            any_running = true;
            break;
        }
    }
    if (!any_running)
    {
        grp->session_active = false;
        for (int i = 0; i < grp->count; i++)
        {
            grp->started[i] = false;
            grp->paused[i] = false;
        }
        MemoryBarrier();
    }
    sim_notify_state(grp);
}

/**
 * Приостанавливает текущий поток, пока он находится на паузе или его скорость равна 0.
 *
 * @param grp       Указатель на группу потоков SimGroup.
 * @param id        Индекс потока в группе.
 * @param speed_ptr Указатель на отслеживаемую переменную скорости (или NULL для скорости потока).
 * @return true, если поток продолжает работу; false, если получен сигнал останова.
 */
bool sim_wait_active(SimGroup *grp, int id, const volatile int *speed_ptr)
{
    if (!grp || id < 0 || id >= grp->count)
        return false;
    const volatile int *spd = speed_ptr ? speed_ptr : &grp->speed[id];

    while (grp->running[id] && (grp->paused[id] || *spd <= SIM_SPEED_MIN))
    {
        Sleep(SIM_SLEEP_TICK_MS);
    }
    return grp->running[id];
}

/**
 * Выполняет задержку шага потока обратно пропорционально внешнему ползунку скорости (чтения/записи у книги).
 *
 * @param grp       Указатель на группу потоков SimGroup.
 * @param id        Индекс потока в группе.
 * @param speed_ptr Указатель на переменную скорости (0..100).
 * @return true, если задержка завершена штатно; false, если поток был остановлен.
 */
bool sim_sleep_custom(SimGroup *grp, int id, const volatile int *speed_ptr)
{
    if (!grp || id < 0 || id >= grp->count)
        return false;
    const volatile int *spd_ref = speed_ptr ? speed_ptr : &grp->speed[id];

    int elapsed = 0;
    while (grp->running[id])
    {
        if (!sim_wait_active(grp, id, spd_ref))
            return false;

        int spd = sim_clamp_speed(*spd_ref);
        if (spd <= 0)
            spd = 1;

        int target_ms = (grp->delay_min_ms * SIM_SPEED_MAX) / spd;
        if (target_ms > grp->delay_max_ms)
        {
            target_ms = grp->delay_max_ms;
        }
        if (elapsed >= target_ms)
            break;

        Sleep(SIM_SLEEP_TICK_MS);
        elapsed += SIM_SLEEP_TICK_MS;
    }
    return sim_wait_active(grp, id, spd_ref);
}

/**
 * Выполняет задержку шага потока обратно пропорционально его индивидуальному ползунку скорости.
 *
 * @param grp Указатель на группу потоков SimGroup.
 * @param id  Индекс потока в группе.
 * @return true, если задержка завершена штатно; false, если поток был остановлен.
 */
bool sim_sleep(SimGroup *grp, int id)
{
    if (!grp || id < 0 || id >= grp->count)
        return false;
    return sim_sleep_custom(grp, id, &grp->speed[id]);
}

/**
 * Останавливает один поток из группы, дожидаясь его завершения через WaitForSingleObject.
 *
 * @param grp Указатель на группу потоков SimGroup.
 * @param id  Индекс останавливаемого потока.
 */
void sim_stop_thread(SimGroup *grp, int id)
{
    if (!grp || id < 0 || id >= grp->count)
        return;
    if (grp->threads[id] != NULL)
    {
        grp->running[id] = false;
        MemoryBarrier();
        WaitForSingleObject(grp->threads[id], INFINITE);
        CloseHandle(grp->threads[id]);
        grp->threads[id] = NULL;
    }
}

/**
 * Запускает один поток из группы с указанной стартовой функцией WinAPI.
 *
 * @param grp   Указатель на группу потоков SimGroup.
 * @param id    Индекс запускаемого потока.
 * @param proc  Потоковая функция (LPTHREAD_START_ROUTINE).
 * @param param Параметр, передаваемый в потоковую функцию.
 */
void sim_start_thread(SimGroup *grp, int id, LPTHREAD_START_ROUTINE proc, LPVOID param)
{
    if (!grp || id < 0 || id >= grp->count)
        return;

    grp->session_active = true;
    grp->started[id] = true;
    grp->running[id] = true;
    MemoryBarrier();

    sim_notify_state(grp);
    grp->threads[id] = CreateThread(NULL, 0, proc, param, 0, NULL);
}

/**
 * Останавливает все потоки группы, закрывает их дескрипторы и сбрасывает состояние сессии.
 *
 * @param grp Указатель на группу потоков SimGroup.
 */
void sim_stop_all(SimGroup *grp)
{
    if (!grp)
        return;
    for (int i = 0; i < grp->count; i++)
    {
        grp->running[i] = false;
    }
    MemoryBarrier();

    for (int i = 0; i < grp->count; i++)
    {
        sim_stop_thread(grp, i);
        grp->started[i] = false;
        grp->paused[i] = false;
    }

    grp->session_active = false;
    MemoryBarrier();
}

/**
 * Переключает состояние паузы для указанного потока группы.
 *
 * @param grp Указатель на группу потоков SimGroup.
 * @param id  Индекс потока в группе.
 * @return Новое состояние паузы (true - на паузе, false - активен).
 */
bool sim_toggle_paused(SimGroup *grp, int id)
{
    if (!grp || id < 0 || id >= grp->count)
        return false;
    grp->paused[id] = !grp->paused[id];
    MemoryBarrier();
    return grp->paused[id];
}

/**
 * Проверяет, находится ли указанный поток группы на паузе.
 *
 * @param grp Указатель на группу потоков SimGroup.
 * @param id  Индекс потока в группе.
 * @return true, если поток поставлен на паузу; иначе false.
 */
bool sim_is_paused(const SimGroup *grp, int id)
{
    if (!grp || id < 0 || id >= grp->count)
        return false;
    return grp->paused[id];
}

/**
 * Устанавливает индивидуальную скорость для указанного потока группы.
 *
 * @param grp   Указатель на группу потоков SimGroup.
 * @param id    Индекс потока в группе.
 * @param speed Новое значение скорости (0..100).
 */
void sim_set_speed(SimGroup *grp, int id, int speed)
{
    if (!grp || id < 0 || id >= grp->count)
        return;
    grp->speed[id] = sim_clamp_speed(speed);
    MemoryBarrier();
}

/**
 * Возвращает текущую индивидуальную скорость указанного потока группы.
 *
 * @param grp Указатель на группу потоков SimGroup.
 * @param id  Индекс потока в группе.
 * @return Значение скорости потока (0..100).
 */
int sim_get_speed(const SimGroup *grp, int id)
{
    if (!grp || id < 0 || id >= grp->count)
        return 0;
    return grp->speed[id];
}
