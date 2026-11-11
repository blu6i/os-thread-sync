#ifndef DEKKER_H
#define DEKKER_H

#include <stdbool.h>
#include "sim_control.h"

#define FILE_NAME "data.txt"
#define X_START 1.0
#define DEFAULT_X_MAX 12.0
#define DEFAULT_STEP 1.0
#define DEKKER_DELAY_MIN 60
#define DEKKER_DELAY_MAX 1500

/* Коллбэк добавления или обновления строки row_idx в таблице потока (thread_id: 0 или 1) */
typedef void (*DekkerRowCallback)(int thread_id, int row_idx, const char *col_available, const char *col_computed);

/* Коллбэк очистки таблицы потока (thread_id: 0, 1 или -1 для обоих) */
typedef void (*DekkerClearCallback)(int thread_id);

/* Группа управления потоками алгоритма Деккера (скорость, пауза, состояние сессии) */
extern SimGroup g_dekker_sim;

/**
 * Инициализирует модуль алгоритма Деккера и связывает его с коллбэками графического интерфейса.
 *
 * @param row_cb   Коллбэк добавления или обновления строки в таблице потока.
 * @param clear_cb Коллбэк очистки таблицы потока.
 * @param state_cb Коллбэк уведомления UI об активности сессии и доступности кнопок "Старт".
 */
void dekker_init(DekkerRowCallback row_cb, DekkerClearCallback clear_cb, SimStateCallback state_cb);

/**
 * Задает параметры интервала вычислений (максимальное значение x и шаг h) перед стартом потоков.
 *
 * @param x_max  Верхняя граница интервала x (не меньше X_START = 1.0).
 * @param step_h Шаг изменения аргумента h (положительное число).
 */
void dekker_set_params(double x_max, double step_h);

/**
 * Запускает выбранный вычислительный поток алгоритма Деккера (0 - запись, 1 - чтение).
 *
 * @param thread_id Идентификатор запускаемого потока (0 или 1).
 */
void dekker_start_thread(int thread_id);

/**
 * Полностью останавливает оба потока алгоритма Деккера, очищает рабочий файл и таблицы.
 */
void dekker_stop_all(void);

#endif /* DEKKER_H */
