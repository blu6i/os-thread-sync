# ==============================================================================
# Makefile для сборки C-проекта на Windows (MinGW / GCC)
# ==============================================================================

# Компилятор и стандартные флаги
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Iinclude
LDFLAGS = -static -static-libgcc -lgdi32 -lcomctl32 -lgdiplus -mwindows

# Режим отладки (вызов: make DEBUG=1)
ifeq ($(DEBUG), 1)
    CFLAGS += -g -O0 -DDEBUG
else
    CFLAGS += -O2 -DNDEBUG
endif

# Структура директорий
SRCDIR   = src
INCDIR   = include
BUILDDIR = build
TARGET   = $(BUILDDIR)/app.exe

# Автоматический поиск всех .c файлов в директории src/
SRCS = $(wildcard $(SRCDIR)/*.c)
OBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))

# Псевдоцели (не привязаны к файлам)
.PHONY: all clean run rebuild dirs map help

# Сборка по умолчанию
all: dirs $(TARGET)

# Создание директории для артефактов сборки
dirs:
	@if not exist $(BUILDDIR) mkdir $(BUILDDIR)

# Линковка исполняемого файла
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

# Компиляция каждого .c файла в .o
$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Сборка и запуск приложения
run: all
	@$(TARGET)

# Полная пересборка проекта
rebuild: clean all

# Очистка директории сборки
clean:
	@if exist $(BUILDDIR) rmdir /s /q $(BUILDDIR)

# Генерация Markdown-карты функций проекта и списка функций без docstring
map:
	@python scripts/generate_code_map.py

# Справка по доступным командам
help:
	@echo Доступные цели:
	@echo   make          - Собрать проект (Release)
	@echo   make DEBUG=1  - Собрать проект с отладочной информацией (Debug)
	@echo   make run      - Собрать и запустить исполняемый файл
	@echo   make clean    - Удалить скомпилированные файлы
	@echo   make rebuild  - Полная пересборка
	@echo   make map      - Сгенерировать карту функций проекта (docs/PROJECT_MAP.md)
	@echo   make help     - Показать эту справку
