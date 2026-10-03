#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

// Структура для таблицы almost_seq
typedef struct {
    char table_name[50];
    char column_name[50];
    int current_number;
} AlmostSeq;

// Структура для таблицы almost_restr
typedef struct {
    char table_name[50];
    int byte_count;
    char restriction[20];  // "unique", "primary key", "check"
} AlmostRestr;

// Структура для таблицы almost_relate
typedef struct {
    char table1_name[50];
    char column1_name[50];
    char table2_name[50];
    char column2_name[50];
} AlmostRelate;

// Базовые функции для работы с файлами
void initialize_file(const char* filename, size_t record_size) {
    FILE* file = fopen(filename, "ab");
    if (file != NULL) {
        fclose(file);
        printf("Файл %s инициализирован\n", filename);
    }
}

int add_record(const char* filename, void* record, size_t record_size) {
    FILE* file = fopen(filename, "ab");
    if (file == NULL) {
        printf("Ошибка открытия файла %s\n", filename);
        return 0;
    }
    
    fwrite(record, record_size, 1, file);
    fclose(file);
    return 1;
}

void read_all_records(const char* filename, void* records, size_t record_size, int* count) {
    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        *count = 0;
        return;
    }
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    *count = file_size / record_size;
    
    if (*count > 0) {
        fread(records, record_size, *count, file);
    }
    
    fclose(file);
}

// ФУНКЦИИ ДЛЯ ALMOST_SEQ
void create_seq() {
    AlmostSeq seq;
    printf("Введите название таблицы: ");
    scanf("%49s", seq.table_name);
    printf("Введите название колонки: ");
    scanf("%49s", seq.column_name);
    printf("Введите начальный номер: ");
    scanf("%d", &seq.current_number);
    
    if (add_record("almost_seq.bin", &seq, sizeof(AlmostSeq))) {
        printf("Запись успешно создана!\n");
    } else {
        printf("Ошибка создания записи!\n");
    }
}

void view_all_seq() {
    AlmostSeq records[100];
    int count;
    
    read_all_records("almost_seq.bin", records, sizeof(AlmostSeq), &count);
    
    printf("\n=== Все записи almost_seq ===\n");
    for (int i = 0; i < count; i++) {
        printf("%d. Таблица: %s, Колонка: %s, Номер: %d\n", 
               i+1, records[i].table_name, records[i].column_name, records[i].current_number);
    }
    printf("Всего записей: %d\n", count);
}

void update_seq() {
    char table_name[50], column_name[50];
    int new_number;
    
    printf("Введите название таблицы для обновления: ");
    scanf("%49s", table_name);
    printf("Введите название колонки: ");
    scanf("%49s", column_name);
    printf("Введите новый номер: ");
    scanf("%d", &new_number);
    
    AlmostSeq records[100];
    int count;
    read_all_records("almost_seq.bin", records, sizeof(AlmostSeq), &count);
    
    int found = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(records[i].table_name, table_name) == 0 &&
            strcmp(records[i].column_name, column_name) == 0) {
            records[i].current_number = new_number;
            found = 1;
            break;
        }
    }
    
    if (found) {
        FILE* file = fopen("almost_seq.bin", "wb");
        if (file != NULL) {
            fwrite(records, sizeof(AlmostSeq), count, file);
            fclose(file);
            printf("Запись обновлена!\n");
        } else {
            printf("Ошибка открытия файла для записи!\n");
        }
    } else {
        printf("Запись не найдена!\n");
    }
}

void delete_seq() {
    char table_name[50], column_name[50];
    
    printf("Введите название таблицы для удаления: ");
    scanf("%49s", table_name);
    printf("Введите название колонки: ");
    scanf("%49s", column_name);
    
    AlmostSeq records[100];
    int count;
    read_all_records("almost_seq.bin", records, sizeof(AlmostSeq), &count);
    
    FILE* file = fopen("almost_seq.bin", "wb");
    if (file == NULL) {
        printf("Ошибка открытия файла!\n");
        return;
    }
    
    int deleted = 0;
    for (int i = 0; i < count; i++) {
        if (!(strcmp(records[i].table_name, table_name) == 0 &&
              strcmp(records[i].column_name, column_name) == 0)) {
            fwrite(&records[i], sizeof(AlmostSeq), 1, file);
        } else {
            deleted = 1;
        }
    }
    fclose(file);
    
    if (deleted) {
        printf("Запись удалена!\n");
    } else {
        printf("Запись не найдена!\n");
    }
}

// ФУНКЦИИ ДЛЯ ALMOST_RESTR
void create_restr() {
    AlmostRestr restr;
    printf("Введите название таблицы: ");
    scanf("%49s", restr.table_name);
    printf("Введите количество байт: ");
    scanf("%d", &restr.byte_count);
    printf("Введите ограничение (unique/primary key/check): ");
    scanf("%19s", restr.restriction);
    
    if (add_record("almost_restr.bin", &restr, sizeof(AlmostRestr))) {
        printf("Ограничение создано!\n");
    } else {
        printf("Ошибка создания ограничения!\n");
    }
}

void view_all_restr() {
    AlmostRestr records[100];
    int count;
    
    read_all_records("almost_restr.bin", records, sizeof(AlmostRestr), &count);
    
    printf("\n=== Все ограничения ===\n");
    for (int i = 0; i < count; i++) {
        printf("%d. Таблица: %s, Байт: %d, Ограничение: %s\n", 
               i+1, records[i].table_name, records[i].byte_count, records[i].restriction);
    }
    printf("Всего ограничений: %d\n", count);
}

// ФУНКЦИИ ДЛЯ ALMOST_RELATE
void create_relate() {
    AlmostRelate relate;
    printf("Введите таблицу 1: ");
    scanf("%49s", relate.table1_name);
    printf("Введите колонку 1: ");
    scanf("%49s", relate.column1_name);
    printf("Введите таблицу 2: ");
    scanf("%49s", relate.table2_name);
    printf("Введите колонку 2: ");
    scanf("%49s", relate.column2_name);
    
    if (add_record("almost_relate.bin", &relate, sizeof(AlmostRelate))) {
        printf("Отношение создано!\n");
    } else {
        printf("Ошибка создания отношения!\n");
    }
}

void view_all_relate() {
    AlmostRelate records[100];
    int count;
    
    read_all_records("almost_relate.bin", records, sizeof(AlmostRelate), &count);
    
    printf("\n=== Все отношения ===\n");
    for (int i = 0; i < count; i++) {
        printf("%d. %s.%s -> %s.%s\n", 
               i+1, records[i].table1_name, records[i].column1_name,
               records[i].table2_name, records[i].column2_name);
    }
    printf("Всего отношений: %d\n", count);
}

// МЕНЮ
void seq_menu() {
    int choice;
    do {
        printf("\n=== ALMOST_SEQ - Управление последовательностями ===\n");
        printf("1. Создать запись\n");
        printf("2. Просмотреть все записи\n");
        printf("3. Обновить номер\n");
        printf("4. Удалить запись\n");
        printf("0. Назад\n");
        printf("Выберите действие: ");
        scanf("%d", &choice);
        
        switch(choice) {
            case 1: create_seq(); break;
            case 2: view_all_seq(); break;
            case 3: update_seq(); break;
            case 4: delete_seq(); break;
            case 0: printf("Возврат в главное меню...\n"); break;
            default: printf("Неверный выбор!\n");
        }
    } while(choice != 0);
}

void restr_menu() {
    int choice;
    do {
        printf("\n=== ALMOST_RESTR - Управление ограничениями ===\n");
        printf("1. Создать ограничение\n");
        printf("2. Просмотреть все ограничения\n");
        printf("0. Назад\n");
        printf("Выберите действие: ");
        scanf("%d", &choice);
        
        switch(choice) {
            case 1: create_restr(); break;
            case 2: view_all_restr(); break;
            case 0: printf("Возврат в главное меню...\n"); break;
            default: printf("Неверный выбор!\n");
        }
    } while(choice != 0);
}

void relate_menu() {
    int choice;
    do {
        printf("\n=== ALMOST_RELATE - Управление отношениями ===\n");
        printf("1. Создать отношение\n");
        printf("2. Просмотреть все отношения\n");
        printf("0. Назад\n");
        printf("Выберите действие: ");
        scanf("%d", &choice);
        
        switch(choice) {
            case 1: create_relate(); break;
            case 2: view_all_relate(); break;
            case 0: printf("Возврат в главное меню...\n"); break;
            default: printf("Неверный выбор!\n");
        }
    } while(choice != 0);
}

void show_main_menu() {
    printf("\n=== СИСТЕМА УПРАВЛЕНИЯ СЛУЖЕБНЫМИ ТАБЛИЦАМИ ===\n");
    printf("1. almost_seq - Управление последовательностями\n");
    printf("2. almost_restr - Управление ограничениями\n");
    printf("3. almost_relate - Управление отношениями\n");
    printf("4. Просмотр всех данных\n");
    printf("0. Выход\n");
    printf("Выберите действие: ");
}

void view_all_data() {
    printf("\n=== ВСЕ ДАННЫЕ ИЗ СЛУЖЕБНЫХ ТАБЛИЦ ===\n");
    view_all_seq();
    view_all_restr();
    view_all_relate();
}

int main() {
     SetConsoleOutputCP(65001); // для русских символов в Windows
    
    // Инициализация файлов
    initialize_file("almost_seq.bin", sizeof(AlmostSeq));
    initialize_file("almost_restr.bin", sizeof(AlmostRestr));
    initialize_file("almost_relate.bin", sizeof(AlmostRelate));
    
    int choice;
    do {
        show_main_menu();
        scanf("%d", &choice);
        
        switch(choice) {
            case 1: seq_menu(); break;
            case 2: restr_menu(); break;
            case 3: relate_menu(); break;
            case 4: view_all_data(); break;
            case 0: printf("Выход из программы...\n"); break;
            default: printf("Неверный выбор!\n");
        }
    } while(choice != 0);
    
    return 0;
}