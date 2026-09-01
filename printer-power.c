/*
 * printer_power.c
 *
 * Безопасное управление питанием 3D-принтера:
 *
 *   GPIO button -> Orange Pi -> relay
 *
 * При этом состояние OctoPrint используется для блокировки
 * физического выключения во время печати.
 *
 * Требования:
 *   - wiringPi
 *   - libcurl
 *
 * Компиляция:
 *   gcc printer_power.c -o printer_power -lwiringPi -lcurl
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <wiringPi.h>


/* ============================================================
 * НАСТРОЙКИ
 * ============================================================ */

/*
 * Номера wiringPi.
 */

#define BUTTON_PIN  8
#define SENSE_PIN   1
#define OUT_PIN     2


/*
 * Кнопка подключена к 3.3 V.
 * При нажатии GPIO = HIGH.
 */

#define BUTTON_ACTIVE HIGH


/*
 * Минимальное время стабильного состояния кнопки.
 *
 * Это защита от:
 *   - дребезга контактов;
 *   - коротких импульсов;
 *   - электромагнитных наводок.
 *
 * 100 ms обычно достаточно.
 */

#define DEBOUNCE_MS 100


/*
 * Время удержания для ВЫКЛЮЧЕНИЯ.
 *
 * Включение происходит после обычного нажатия.
 * Выключение требует удерживать кнопку 2 секунды.
 *
 * Можно увеличить до 3000-5000 ms, если хотите
 * максимально исключить случайное выключение.
 */

#define OFF_HOLD_MS 2000


/*
 * Как часто опрашивать OctoPrint.
 */

#define OCTOPRINT_POLL_MS 1000


/*
 * Таймаут HTTP-запроса.
 *
 * Если OctoPrint завис/не отвечает, запрос не должен
 * блокировать программу надолго.
 */

#define HTTP_TIMEOUT_MS 500


/*
 * OctoPrint.
 *
 * Если OctoPrint работает на этом же Orange Pi,
 * localhost — лучший вариант.
 */

#define OCTOPRINT_URL \
"http://127.0.0.1/api/printer"


/*
 * API key OctoPrint.
 *
 * ВАЖНО:
 * сюда нужно вставить ваш API key.
 *
 * Лучше потом вынести его в отдельный файл конфигурации
 * с правами 600.
 */

#define OCTOPRINT_API_KEY "g9-YMECTBKO5JYsB4pB9hv_FZZLvmS1AHT2qMm8_V4c"


/* ============================================================
 * ГЛОБАЛЬНЫЕ СОСТОЯНИЯ
 * ============================================================ */


/*
 * Последнее достоверно полученное состояние OctoPrint.
 *
 * Важная идея:
 *
 *   octoprint_known = 0
 *
 * означает:
 * "Мы НЕ ЗНАЕМ, что сейчас происходит."
 *
 * В таком состоянии выключение запрещено.
 */

static volatile int octoprint_known = 0;


/*
 * Последнее известное состояние печати.
 *
 * 1 = активное задание
 * 0 = активного задания нет
 */

static volatile int octoprint_printing = 0;


/*
 * Время последнего успешного ответа OctoPrint.
 */

static unsigned long octoprint_last_ok = 0;


/* ============================================================
 * CURL BUFFER
 * ============================================================ */

struct Memory {
    char *data;
    size_t size;
};


/*
 * CURL вызывает эту функцию для получения HTTP-ответа.
 */

static size_t write_callback(void *contents,
                             size_t size,
                             size_t nmemb,
                             void *userp)
{
    size_t realsize = size * nmemb;

    struct Memory *mem = (struct Memory *)userp;

    char *ptr = realloc(mem->data, mem->size + realsize + 1);

    if (ptr == NULL) {
        return 0;
    }

    mem->data = ptr;

    memcpy(&(mem->data[mem->size]), contents, realsize);

    mem->size += realsize;

    mem->data[mem->size] = '\0';

    return realsize;
}


/* ============================================================
 * ПРОВЕРКА OCTOPRINT
 * ============================================================ */


/*
 * Получает /api/printer и определяет:
 *
 *   printing
 *   paused
 *   pausing
 *   cancelling
 *
 * Мы намеренно не используем только строку state.text.
 *
 * Вместо этого ищем flags.
 *
 * Это соответствует структуре API OctoPrint.
 */

static int octoprint_get_printing(int *active)
{
    CURL *curl;
    CURLcode res;

    struct Memory chunk;

    chunk.data = malloc(1);
    chunk.size = 0;

    if (chunk.data == NULL) {
        return -1;
    }

    chunk.data[0] = '\0';

    curl = curl_easy_init();

    if (!curl) {
        free(chunk.data);
        return -1;
    }


    /*
     * Заголовки HTTP.
     */

    struct curl_slist *headers = NULL;

    char api_header[512];

    snprintf(api_header,
             sizeof(api_header),
             "X-Api-Key: %s",
             OCTOPRINT_API_KEY);

    headers = curl_slist_append(headers, api_header);
    headers = curl_slist_append(headers, "Accept: application/json");


    curl_easy_setopt(curl,
                     CURLOPT_URL,
                     OCTOPRINT_URL);

    curl_easy_setopt(curl,
                     CURLOPT_HTTPHEADER,
                     headers);

    curl_easy_setopt(curl,
                     CURLOPT_WRITEFUNCTION,
                     write_callback);

    curl_easy_setopt(curl,
                     CURLOPT_WRITEDATA,
                     &chunk);

    curl_easy_setopt(curl,
                     CURLOPT_CONNECTTIMEOUT_MS,
                     HTTP_TIMEOUT_MS);

    curl_easy_setopt(curl,
                     CURLOPT_TIMEOUT_MS,
                     HTTP_TIMEOUT_MS);

    /*
     * Не следуем редиректам.
     */

    curl_easy_setopt(curl,
                     CURLOPT_FOLLOWLOCATION,
                     0L);


    res = curl_easy_perform(curl);


    long http_code = 0;

    curl_easy_getinfo(curl,
                      CURLINFO_RESPONSE_CODE,
                      &http_code);


    curl_slist_free_all(headers);

    curl_easy_cleanup(curl);


    /*
     * Любая ошибка HTTP/сети означает:
     *
     *   состояние OctoPrint неизвестно.
     *
     * Это принципиально важно для безопасности.
     */

    if (res != CURLE_OK ||
        http_code != 200) {

        free(chunk.data);

    return -1;
        }


        /*
         * Ищем флаги.
         *
         * Нам нужно определить:
         *
         * printing = true
         * paused = true
         * pausing = true
         * cancelling = true
         *
         * Любой из них означает:
         *
         * НЕ РАЗРЕШАТЬ ВЫКЛЮЧЕНИЕ.
         */

        int printing = 0;
        int paused = 0;
        int pausing = 0;
        int cancelling = 0;


        if (strstr(chunk.data, "\"printing\":true") != NULL ||
            strstr(chunk.data, "\"printing\": true") != NULL) {
            printing = 1;
            }

            if (strstr(chunk.data,
                "\"paused\":true") != NULL) {
                paused = 1;
                }

                if (strstr(chunk.data,
                    "\"pausing\":true") != NULL) {
                    pausing = 1;
                    }

                    if (strstr(chunk.data,
                        "\"cancelling\":true") != NULL) {
                        cancelling = 1;
                        }


                        /*
                         * Активное задание.
                         */

                        if (printing ||
                            paused ||
                            pausing ||
                            cancelling) {

                            *active = 1;
                            }
                            else {
                                *active = 0;
                            }


                            free(chunk.data);

                            return 0;
}


/* ============================================================
 * ОБНОВЛЕНИЕ СОСТОЯНИЯ OCTOPRINT
 * ============================================================ */

static void update_octoprint_state(void)
{
    int active;

    int result = octoprint_get_printing(&active);


    if (result == 0) {

        /*
         * Получили достоверный ответ.
         */

        octoprint_known = 1;

        octoprint_printing = active;

        octoprint_last_ok = millis();

    }
    else {

        /*
         * OctoPrint недоступен.
         *
         * НЕ сбрасываем octoprint_printing.
         *
         * Если до этого шла печать, считаем,
         * что она продолжается.
         *
         * Это fail-safe поведение.
         */

        octoprint_known = 0;
    }
}


/* ============================================================
 * GENERIC HTTP REQUEST
 * ============================================================ */

static int octoprint_connection_command(const char *json)
{
    CURL *curl;
    CURLcode res;

    curl = curl_easy_init();

    if (!curl)
        return -1;

    struct curl_slist *headers = NULL;

    char api_header[512];

    snprintf(api_header,
             sizeof(api_header),
             "X-Api-Key: %s",
             OCTOPRINT_API_KEY);

    headers = curl_slist_append(
        headers,
        api_header
    );

    headers = curl_slist_append(
        headers,
        "Content-Type: application/json"
    );

    curl_easy_setopt(
        curl,
        CURLOPT_URL,
        "http://127.0.0.1/api/connection"
    );

    curl_easy_setopt(
        curl,
        CURLOPT_HTTPHEADER,
        headers
    );

    curl_easy_setopt(
        curl,
        CURLOPT_POST,
        1L
    );

    curl_easy_setopt(
        curl,
        CURLOPT_POSTFIELDS,
        json
    );

    curl_easy_setopt(
        curl,
        CURLOPT_CONNECTTIMEOUT_MS,
        1000L
    );

    curl_easy_setopt(
        curl,
        CURLOPT_TIMEOUT_MS,
        2000L
    );

    res = curl_easy_perform(curl);

    long http_code = 0;

    curl_easy_getinfo(
        curl,
        CURLINFO_RESPONSE_CODE,
        &http_code
    );

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        return -1;

    /*
     * OctoPrint возвращает 204 No Content
     * при успешной команде.
     */

    if (http_code != 204)
        return -1;

    return 0;
}


/* ============================================================
 * КОМАНДЫ DISCONNECT CONNECT
 * ============================================================ */

static int octoprint_disconnect(void)
{
    return octoprint_connection_command(
        "{\"command\":\"disconnect\"}"
    );
}

static int octoprint_connect(void)
{
    return octoprint_connection_command(
        "{\"command\":\"connect\"}"
    );
}


/* ============================================================
 * РАБОТА С RELAY
 * ============================================================ */


/*
 * Возвращает состояние питания:
 *
 *   0 = OFF
 *   1 = ON
 */

static int power_state(void)
{
    return digitalRead(OUT_PIN) ? 1 : 0;
}


/*
 * Установить питание.
 */

static void set_power(int state)
{
    digitalWrite(OUT_PIN,
                 state ? HIGH : LOW);
}


/*
 * Синхронизация sense_pin с relay.
 */

static void update_sense(void)
{
    digitalWrite(SENSE_PIN,
                 digitalRead(OUT_PIN));
}


/* ============================================================
 * MAIN
 * ============================================================ */

int main(void)
{
    printf("Starting printer power controller...\n");


    /*
     * Инициализация wiringPi.
     */

    if (wiringPiSetup() < 0) {

        fprintf(stderr,
                "wiringPiSetup() failed\n");

        return 1;
    }


    /*
     * Кнопка.
     */

    pinMode(BUTTON_PIN, INPUT);

    pullUpDnControl(BUTTON_PIN, PUD_DOWN);


    /*
     * Relay.
     */

    pinMode(OUT_PIN, OUTPUT);


    /*
     * Sense.
     */

    pinMode(SENSE_PIN, OUTPUT);


    /*
     * Первоначальный sense.
     */

    update_sense();


    /*
     * CURL.
     */

    curl_global_init(CURL_GLOBAL_DEFAULT);


    /*
     * --------------------------------------------------------
     * Состояние кнопки
     * --------------------------------------------------------
     */

    int raw_button = LOW;
    int stable_button = LOW;

    unsigned long button_change_time = millis();

    unsigned long button_press_time = 0;


    /*
     * Время последнего опроса OctoPrint.
     */

    unsigned long last_octoprint_poll = 0;


    /*
     * Пока OctoPrint не подтвердил состояние,
     * выключение запрещено.
     *
     * Это начальное безопасное состояние.
     */

    octoprint_known = 0;
    octoprint_printing = 0;


    printf("Controller started.\n");
    printf("Power: %s\n",
           power_state() ? "ON" : "OFF");


    /* ========================================================
     * ГЛАВНЫЙ ЦИКЛ
     * ======================================================== */

    while (1)
    {
        unsigned long now = millis();


        /* ====================================================
         * 1. СИНХРОНИЗАЦИЯ OCTOPRINT
         * ==================================================== */

        if ((now - last_octoprint_poll) >=
            OCTOPRINT_POLL_MS) {

            last_octoprint_poll = now;

        update_octoprint_state();


        /*
         * Отладочный вывод.
         */

        if (octoprint_known) {

            printf("OctoPrint: %s\n",
                   octoprint_printing
                   ? "PRINT/LOCK"
                   : "IDLE");
        }
        else {

            printf("OctoPrint: UNKNOWN / LOCK\n");
        }
            }


            /* ====================================================
             * 2. SENSE ДЛЯ PSU CONTROL
             * ==================================================== */

            update_sense();


            /* ====================================================
             * 3. СЧИТЫВАНИЕ КНОПКИ
             * ==================================================== */

            int current_raw =
            digitalRead(BUTTON_PIN);


            /*
             * Обнаружено изменение сырого GPIO.
             */

            if (current_raw != raw_button) {

                raw_button = current_raw;

                button_change_time = now;
            }


            /*
             * Состояние должно оставаться стабильным
             * DEBOUNCE_MS.
             */

            if ((now - button_change_time) >=
                DEBOUNCE_MS) {


                /*
                 * Стабильное состояние изменилось.
                 */

                if (stable_button != raw_button) {

                    stable_button = raw_button;


                    /* ============================================
                     * НАЖАТИЕ
                     * ============================================ */

                    if (stable_button ==
                        BUTTON_ACTIVE) {

                        button_press_time = now;

                    printf("Button pressed\n");
                        }


                        /* ============================================
                         * ОТПУСКАНИЕ
                         * ============================================ */

                        else {

                            unsigned long press_duration =
                            now - button_press_time;


                            /*
                             * ----------------------------------------
                             * КНОПКА ОТПУЩЕНА
                             * ----------------------------------------
                             */

                            if (power_state() == 0) {

                                /*
                                 * Принтер выключен.
                                 *
                                 * Любое подтверждённое нажатие
                                 * включает питание.
                                 */

                                printf("Button: POWER ON\n");
                                set_power(1);
                                delay(2000);
                                octoprint_connect();
                                update_sense();
                            }

                            else {

                                /*
                                 * Принтер включён.
                                 *
                                 * Для выключения требуется
                                 * длительное удержание.
                                 */

                                if (press_duration >=
                                    OFF_HOLD_MS) {

                                    /*
                                     * Самая важная проверка.
                                     *
                                     * Если OctoPrint неизвестен —
                                     * НЕ выключаем.
                                     */

                                    if (!octoprint_known) {

                                        printf(
                                            "OFF BLOCKED: "
                                            "OctoPrint state unknown\n"
                                        );
                                    }

                                    /*
                                     * Если есть активное задание —
                                     * НЕ выключаем.
                                     */

                                    else if (octoprint_printing) {

                                        printf(
                                            "OFF BLOCKED: "
                                            "print active\n"
                                        );
                                    }

                                    /*
                                     * Только теперь разрешаем OFF.
                                     */

                                    else {

                                        printf(
                                            "Button: POWER OFF\n"
                                        );
                                        octoprint_disconnect();
                                        delay(100);
                                        set_power(0);

                                        update_sense();
                                    }

                                    }
                                    else {

                                        /*
                                         * Короткое нажатие при включённом
                                         * принтере ничего не делает.
                                         *
                                         * Это дополнительная защита.
                                         */

                                        printf(
                                            "Short press ignored "
                                            "(hold %lu ms for OFF)\n",
                                               press_duration
                                        );
                                    }
                            }
                        }
                }
                }


                /*
                 * Небольшая задержка.
                 *
                 * 10 ms = достаточно быстрое реагирование,
                 * но без бессмысленной нагрузки CPU.
                 */

                delay(10);
    }


    curl_global_cleanup();

    return 0;
}

