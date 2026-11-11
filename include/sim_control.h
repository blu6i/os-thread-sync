#ifndef SIM_CONTROL_H
#define SIM_CONTROL_H

#include <stdbool.h>
#include <windows.h>

/* Максимальное число управляемых потоков в одной группе симуляции */
#define SIM_MAX_THREADS 4
#define SIM_SPEED_MIN 0
#define SIM_SPEED_MAX 100
#define SIM_DEFAULT_SPEED 50
#define SIM_SLEEP_TICK_MS 20

/* Коллбэк уведомления UI об активности сессии и доступности кнопок "Старт" */
typedef void (*SimStateCallback)(bool session_active, const bool *start_enabled, int count);

/* Универсальный контроллер группы потоков симуляции (скорость, пауза, жизненный цикл) */
typedef struct
{
    int count;
    int delay_min_ms;
    int delay_max_ms;
    HANDLE threads[SIM_MAX_THREADS];
    volatile bool running[SIM_MAX_THREADS];
    volatile bool started[SIM_MAX_THREADS];
    volatile bool paused[SIM_MAX_THREADS];
    volatile int speed[SIM_MAX_THREADS];
    volatile bool session_active;
    SimStateCallback state_cb;
} SimGroup;

/**
 * Инициализирует контроллер группы потоков симуляции значениями по умолчанию.
 *
 * @param grp          Указатель на структуру группы потоков SimGroup.
 * @param count        Количество потоков в группе (от 1 до SIM_MAX_THREADS).
 * @param delay_min_ms Минимальная задержка шага (при скорости 100), в миллисекундах.
 * @param delay_max_ms Максимальная задержка шага (при малой скорости), в миллисекундах.
 * @param state_cb     Коллбэк для уведомления UI о состоянии кнопок.
 */
void sim_group_init(SimGroup *grp, int count, int delay_min_ms, int delay_max_ms, SimStateCallback state_cb);

/**
 * Ограничивает значение скорости допустимым диапазоном [SIM_SPEED_MIN; SIM_SPEED_MAX].
 *
 * @param speed Исходное значение скорости.
 * @return Нормализованное значение скорости в диапазоне 0..100.
 */
int sim_clamp_speed(int speed);

/**
 * Отправляет в UI актуальное состояние активности сессии и флагов доступности кнопок "Старт".
 *
 * @param grp Указатель на группу потоков SimGroup.
 */
void sim_notify_state(SimGroup *grp);

/**
 * Проверяет, завершились ли все потоки группы, и при полном завершении сбрасывает флаг сессии.
 *
 * @param grp Указатель на группу потоков SimGroup.
 */
void sim_check_completion(SimGroup *grp);

/**
 * Приостанавливает текущий поток, пока он находится на паузе или его скорость равна 0.
 *
 * @param grp       Указатель на группу потоков SimGroup.
 * @param id        Индекс потока в группе.
 * @param speed_ptr Указатель на отслеживаемую переменную скорости (или NULL для скорости потока).
 * @return true, если поток продолжает работу; false, если получен сигнал останова.
 */
bool sim_wait_active(SimGroup *grp, int id, const volatile int *speed_ptr);

/**
 * Выполняет задержку шага потока обратно пропорционально его индивидуальному ползунку скорости.
 *
 * @param grp Указатель на группу потоков SimGroup.
 * @param id  Индекс потока в группе.
 * @return true, если задержка завершена штатно; false, если поток был остановлен.
 */
bool sim_sleep(SimGroup *grp, int id);

/**
 * Выполняет задержку шага потока обратно пропорционально внешнему ползунку скорости (чтения/записи у книги).
 *
 * @param grp       Указатель на группу потоков SimGroup.
 * @param id        Индекс потока в группе.
 * @param speed_ptr Указатель на переменную скорости (0..100).
 * @return true, если задержка завершена штатно; false, если поток был остановлен.
 */
bool sim_sleep_custom(SimGroup *grp, int id, const volatile int *speed_ptr);

/**
 * Останавливает один поток из группы, дожидаясь его завершения через WaitForSingleObject.
 *
 * @param grp Указатель на группу потоков SimGroup.
 * @param id  Индекс останавливаемого потока.
 */
void sim_stop_thread(SimGroup *grp, int id);

/**
 * Запускает один поток из группы с указанной стартовой функцией WinAPI.
 *
 * @param grp   Указатель на группу потоков SimGroup.
 * @param id    Индекс запускаемого потока.
 * @param proc  Потоковая функция (LPTHREAD_START_ROUTINE).
 * @param param Параметр, передаваемый в потоковую функцию.
 */
void sim_start_thread(SimGroup *grp, int id, LPTHREAD_START_ROUTINE proc, LPVOID param);

/**
 * Останавливает все потоки группы, закрывает их дескрипторы и сбрасывает состояние сессии.
 *
 * @param grp Указатель на группу потоков SimGroup.
 */
void sim_stop_all(SimGroup *grp);

/**
 * Переключает состояние паузы для указанного потока группы.
 *
 * @param grp Указатель на группу потоков SimGroup.
 * @param id  Индекс потока в группе.
 * @return Новое состояние паузы (true - на паузе, false - активен).
 */
bool sim_toggle_paused(SimGroup *grp, int id);

/**
 * Проверяет, находится ли указанный поток группы на паузе.
 *
 * @param grp Указатель на группу потоков SimGroup.
 * @param id  Индекс потока в группе.
 * @return true, если поток поставлен на паузу; иначе false.
 */
bool sim_is_paused(const SimGroup *grp, int id);

/**
 * Устанавливает индивидуальную скорость для указанного потока группы.
 *
 * @param grp   Указатель на группу потоков SimGroup.
 * @param id    Индекс потока в группе.
 * @param speed Новое значение скорости (0..100).
 */
void sim_set_speed(SimGroup *grp, int id, int speed);

/**
 * Возвращает текущую индивидуальную скорость указанного потока группы.
 *
 * @param grp Указатель на группу потоков SimGroup.
 * @param id  Индекс потока в группе.
 * @return Значение скорости потока (0..100).
 */
int sim_get_speed(const SimGroup *grp, int id);

#endif /* SIM_CONTROL_H */
