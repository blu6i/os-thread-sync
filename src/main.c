#include "dekker.h"
#include "rw.h"
#include "ui.h"
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <windows.h>

static volatile bool g_cleanup_done = false;

/**
 * Удаляет все побочные текстовые файлы (*.txt), созданные во время работы приложения
 * в текущем рабочем каталоге.
 */
static void cleanup_side_txt_files(void)
{
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA("*.txt", &fd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                DeleteFileA(fd.cFileName);
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    DeleteFileA(FILE_NAME);
}

/**
 * Блок финализации (finally): останавливает фоновые потоки симуляции, освобождает
 * системные критические секции и удаляет все побочные файлы .txt.
 */
static void app_finally_cleanup(void)
{
    if (g_cleanup_done)
        return;
    g_cleanup_done = true;

    dekker_stop_all();
    rw_cleanup();
    cleanup_side_txt_files();
}

/**
 * Обработчик аварийных сигналов стандартной библиотеки C (SIGSEGV, SIGABRT, SIGTERM и др.).
 * Гарантирует выполнение блока очистки и удаление файлов .txt при аварийном завершении.
 *
 * @param sig Код перехваченного сигнала.
 */
static void crash_signal_handler(int sig)
{
    app_finally_cleanup();
    signal(sig, SIG_DFL);
    raise(sig);
}

/**
 * Фильтр необработанных структурных исключений Windows (SEH).
 * Вызывает блок очистки и удаление файлов .txt при критическом сбое процесса.
 *
 * @param ep Указатель на структуру с информацией об исключении Windows.
 * @return Код обработки исключения (EXCEPTION_EXECUTE_HANDLER).
 */
static LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS *ep)
{
    (void)ep;
    app_finally_cleanup();
    return EXCEPTION_EXECUTE_HANDLER;
}

/**
 * Точка входа приложения. Регистрирует перехватчики штатного и аварийного завершения
 * (аналог конструкции try / finally), инициализирует модули синхронизации, запускает UI
 * и гарантированно удаляет все побочные файлы .txt при выходе.
 *
 * @return Код завершения приложения (0 при штатном выходе).
 */
int main(void)
{
    int code = 0;

    /* Регистрация обработчиков аварийного и штатного выхода для гарантии finally-очистки */
    SetUnhandledExceptionFilter(unhandled_exception_filter);
    atexit(app_finally_cleanup);
    signal(SIGINT, crash_signal_handler);
    signal(SIGTERM, crash_signal_handler);
    signal(SIGABRT, crash_signal_handler);
    signal(SIGSEGV, crash_signal_handler);
    signal(SIGFPE, crash_signal_handler);
    signal(SIGILL, crash_signal_handler);

    /* === TRY блок === */
    {
        /* Связываем логику алгоритма Деккера (Вкладка 1) с графическим интерфейсом */
        dekker_init(ui_post_row, ui_post_clear, ui_post_state);

        /* Связываем логику задачи "Читатели и Писатель" (Вкладка 2) с графическим интерфейсом */
        rw_init(ui_post_rw_state);

        /* Запускаем главный цикл окна */
        code = ui_run();
    }
    /* === FINALLY блок === */
    app_finally_cleanup();

    return code;
}
