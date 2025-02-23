#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "hardware/pwm.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "pico/cyw43_arch.h"
#include "lwip/apps/httpd.h"

// Para exibir IP no display ou console:
#include "lwip/netif.h"
#include "lwip/ip_addr.h"

#include "inc/ssd1306.h"
#include "ws2812.pio.h"

// ---------------------------------------------------------------------
// DEFINES
// ---------------------------------------------------------------------
#define I2C_PORT         i2c1
#define SDA_PIN          14
#define SCL_PIN          15
#define OLED_ADDR        0x3C

// Joystick
#define JOYSTICK_X       27  // ADC0
#define JOYSTICK_Y       26  // ADC1
#define BTN_B            6
#define BTN_A            5

// LED para indicar status Wi-Fi
#define LED_WIFI         12

// Neopixel
#define NEOPIXEL_PIN     7
#define NUM_LEDS         25

// Buzzers
#define BUZZER_A         21
#define BUZZER_B         10
#define VICTORY_FREQ     1000
#define DEFEAT_FREQ      300
#define SOUND_DURATION   500  // 500ms

// Limiar Joystick
#define THRESHOLD_UP     1000
#define THRESHOLD_DOWN   2100

#define DEADZONE         200

// Estrutura do display
ssd1306_t disp;

// Flag indicando se já temos uma letra recebida via Wi-Fi
bool has_letter = false;

// Letra atual e opções
char current_letter;
char options[3];
int selected_option = 0;

// Vetor do WS2812 (25 LEDs)
uint32_t led_matrix[NUM_LEDS];

// Mapeamento WS2812 5x5
static const uint8_t LEDmap[5][5] = {
    {24, 23, 22, 21, 20},
    {15, 16, 17, 18, 19},
    {14, 13, 12, 11, 10},
    { 5,  6,  7,  8,  9},
    { 4,  3,  2,  1,  0}
};

// Braille
const int braille_leds[6] = {
    17, // LEDmap[1][2]
    12, // LEDmap[2][2]
    7,  // LEDmap[3][2]
    18, // LEDmap[1][3]
    11, // LEDmap[2][3]
    8   // LEDmap[3][3]
};

// Mapa Braille (A-Z)
const uint8_t braille_map[26][6] = {
    {1, 0, 0, 0, 0, 0}, // A
    {1, 1, 0, 0, 0, 0}, // B
    {1, 0, 0, 1, 0, 0}, // C
    {1, 0, 0, 1, 1, 0}, // D
    {1, 0, 0, 0, 1, 0}, // E
    {1, 1, 0, 1, 0, 0}, // F
    {1, 1, 0, 1, 1, 0}, // G
    {1, 1, 0, 0, 1, 0}, // H
    {0, 1, 0, 1, 0, 0}, // I
    {0, 1, 0, 1, 1, 0}, // J
    {1, 0, 1, 0, 0, 0}, // K
    {1, 1, 1, 0, 0, 0}, // L
    {1, 0, 1, 1, 0, 0}, // M
    {1, 0, 1, 1, 1, 0}, // N
    {1, 0, 1, 0, 1, 0}, // O
    {1, 1, 1, 1, 0, 0}, // P
    {1, 1, 1, 1, 1, 0}, // Q
    {1, 1, 1, 0, 1, 0}, // R
    {0, 1, 1, 1, 0, 0}, // S
    {0, 1, 1, 1, 1, 0}, // T
    {1, 0, 1, 0, 0, 1}, // U
    {1, 1, 1, 0, 0, 1}, // V
    {0, 1, 0, 1, 1, 1}, // W
    {1, 0, 1, 1, 0, 1}, // X
    {1, 0, 1, 1, 1, 1}, // Y
    {1, 0, 1, 0, 1, 1}  // Z
};

// ---------------------------------------------------------------------
// Estrutura e variáveis do Buzzer (não-bloqueante)
// ---------------------------------------------------------------------
typedef struct {
    bool active;
    absolute_time_t end_time;
    uint slice;
    uint channel;
} BuzzerState;

BuzzerState buzzerA_state = { false };
BuzzerState buzzerB_state = { false };

// ---------------------------------------------------------------------
// Para sabermos se estamos exibindo a tela "Correto!/Errado!"
// e, portanto, não deixar o joystick funcionar
// ---------------------------------------------------------------------
static bool showing_feedback = false;

// ---------------------------------------------------------------------
// Funções de inicialização
// ---------------------------------------------------------------------
void init_led_wifi() {
    gpio_init(LED_WIFI);
    gpio_set_dir(LED_WIFI, GPIO_OUT);
    gpio_put(LED_WIFI, false);
}

void init_oled() {
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    ssd1306_init(&disp, 128, 64, false, OLED_ADDR, I2C_PORT);
    ssd1306_config(&disp);
    ssd1306_fill(&disp, false);
    ssd1306_draw_string(&disp, "Display ON", 5, 5);
    ssd1306_send_data(&disp);
    printf("Display OLED inicializado.\n");
}

void init_neopixel() {
    PIO pio = pio0;
    int sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, NEOPIXEL_PIN, 800000, false);

    // Inicia todos apagados
    for (int i = 0; i < NUM_LEDS; i++) {
        led_matrix[i] = 0x000000;
        pio_sm_put_blocking(pio, sm, 0 << 8);
    }
    printf("Matriz WS2812B inicializada (apagada).\n");
}

void adc_init_joystick() {
    adc_init();
    adc_gpio_init(JOYSTICK_X); // ADC0
    adc_gpio_init(JOYSTICK_Y); // ADC1
}

// ---------------------------------------------------------------------
// Funções do Buzzer não-bloqueante
// ---------------------------------------------------------------------
void start_buzzer(BuzzerState *state, uint gpio, uint freq, uint duration_ms) {
    state->active = true;
    state->end_time = make_timeout_time_ms(duration_ms);

    uint slice = pwm_gpio_to_slice_num(gpio);
    uint channel = pwm_gpio_to_channel(gpio);
    state->slice = slice;
    state->channel = channel;

    // Configura PWM
    pwm_config config = pwm_get_default_config();
    uint32_t sys_clk = 125000000; // 125 MHz
    float div = 100.0f;
    uint32_t wrap = (sys_clk / (uint32_t)(div * freq)) - 1;

    pwm_config_set_wrap(&config, wrap);
    pwm_config_set_clkdiv(&config, div);
    pwm_init(slice, &config, true);

    // 50% de duty cycle
    pwm_set_chan_level(slice, channel, wrap / 2);
}

void update_buzzer(BuzzerState *state) {
    if (!state->active) return;
    if (time_reached(state->end_time)) {
        pwm_set_chan_level(state->slice, state->channel, 0); // desliga
        state->active = false;
    }
}

// ---------------------------------------------------------------------
// WS2812 e Display
// ---------------------------------------------------------------------
void set_pixel(int index, uint32_t color) {
    if (index >= 0 && index < NUM_LEDS) {
        led_matrix[index] = color;
    }
}

void update_neopixel() {
    PIO pio = pio0;
    int sm = 0;
    for (int i = 0; i < NUM_LEDS; i++) {
        pio_sm_put_blocking(pio, sm, led_matrix[i] << 8);
    }
}

void display_braille(char letter) {
    if (!has_letter) return;
    // Apaga tudo
    for (int i = 0; i < NUM_LEDS; i++) {
        led_matrix[i] = 0x000000;
    }
    int idx = letter - 'A';
    if (idx < 0 || idx > 25) return;

    // Acende pontos
    for (int i = 0; i < 6; i++) {
        if (braille_map[idx][i]) {
            set_pixel(braille_leds[i], 0x00FF00); // verde
        }
    }
    update_neopixel();
}

void generate_options(char correct) {
    options[0] = correct;
    options[1] = 'A' + (rand() % 26);
    options[2] = 'A' + (rand() % 26);

    // Embaralha
    for (int i = 2; i > 0; i--) {
        int j = rand() % (i + 1);
        char temp = options[i];
        options[i] = options[j];
        options[j] = temp;
    }
    selected_option = 0;
}

void display_options() {
    ssd1306_fill(&disp, false);
    ssd1306_draw_string(&disp, "Letra:", 5, 0);

    for (int i = 0; i < 3; i++) {
        char buf[20];
        if (i == selected_option)
            snprintf(buf, sizeof(buf), "# %c", options[i]);
        else
            snprintf(buf, sizeof(buf), "   %c", options[i]);

        ssd1306_draw_string(&disp, buf, 10, 16 + i * 14);
    }
    ssd1306_send_data(&disp);
}

// ---------------------------------------------------------------------
// Callback GPIO
// ---------------------------------------------------------------------
void my_gpio_callback(uint gpio, uint32_t events) {
    // Botão A
    if (gpio == BTN_A && (events & GPIO_IRQ_EDGE_FALL)) {
        // Se estamos na tela "Correto!/Errado!", A volta às opções
        if (showing_feedback) {
            display_options();       // Mostra as mesmas opções
            has_letter = true;       // Reativa joystick
            showing_feedback = false;
        }
    }
    // Botão B -> Verifica se está correto ou errado
    else if (gpio == BTN_B && (events & GPIO_IRQ_EDGE_FALL)) {
        if (!has_letter) return;

        if (options[selected_option] == current_letter) {
            // Vitória: buzzer A
            start_buzzer(&buzzerA_state, BUZZER_A, VICTORY_FREQ, SOUND_DURATION);
            ssd1306_fill(&disp, false);
            ssd1306_draw_string(&disp, "Correto!", 35, 25);
        } else {
            // Erro: buzzer B
            // ATENÇÃO: use BUZZER_B como segundo parâmetro (GPIO),
            //          e DEFEAT_FREQ como frequência
            start_buzzer(&buzzerB_state, BUZZER_B, DEFEAT_FREQ, SOUND_DURATION);
            ssd1306_fill(&disp, false);
            ssd1306_draw_string(&disp, "Errado!", 35, 25);
        }
        ssd1306_send_data(&disp);

        // Entra na tela de feedback => joystick travado
        showing_feedback = true;
        has_letter = false;
    }
}

// ---------------------------------------------------------------------
// Joystick no Loop
// ---------------------------------------------------------------------
void read_joystick_and_select() {
    adc_select_input(0);
    uint16_t raw_x = adc_read();

    static absolute_time_t last_move; 
    const uint64_t MOVE_INTERVAL = 600000; // 600ms

    if (absolute_time_diff_us(last_move, get_absolute_time()) < MOVE_INTERVAL) {
        return;
    }

    // Sobe
    if (raw_x < THRESHOLD_UP) {
        selected_option = (selected_option + 1) % 3;
        display_options();
        last_move = get_absolute_time();
    }
    // Desce
    else if (raw_x > THRESHOLD_DOWN) {
        selected_option = (selected_option + 2) % 3; // -1 mod 3
        display_options();
        last_move = get_absolute_time();
    }
}

// ---------------------------------------------------------------------
// CGI
// ---------------------------------------------------------------------
const char *cgi_handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]) {
    for (int i = 0; i < iNumParams; i++) {
        if (strcmp(pcParam[i], "letra") == 0 && strlen(pcValue[i]) == 1) {
            // Recebemos uma letra, ativa joystick
            has_letter = true;
            current_letter = (char) toupper(pcValue[i][0]);
            printf("Letra recebida: %c\n", current_letter);

            display_braille(current_letter);
            generate_options(current_letter);
            display_options();

            // Se estivervamos em feedback, saímos
            showing_feedback = false;

            break;
        }
    }
    return "/index.shtml";
}

void cgi_init(void) {
    static const tCGI cgi_handlers[] = {
        {"/send.cgi", cgi_handler}
    };
    http_set_cgi_handlers(cgi_handlers, sizeof(cgi_handlers) / sizeof(tCGI));
}

// ---------------------------------------------------------------------
// MAIN
// ---------------------------------------------------------------------
int main() {
    stdio_init_all();

    init_led_wifi();
    init_oled();
    init_neopixel();
    adc_init_joystick();

    // Botoes
    gpio_init(BTN_A);
    gpio_set_dir(BTN_A, GPIO_IN);
    gpio_pull_up(BTN_A);

    gpio_init(BTN_B);
    gpio_set_dir(BTN_B, GPIO_IN);
    gpio_pull_up(BTN_B);

    // Buzzers
    gpio_set_function(BUZZER_A, GPIO_FUNC_PWM);
    gpio_set_function(BUZZER_B, GPIO_FUNC_PWM);

    // Interrupções
    gpio_set_irq_enabled_with_callback(BTN_A, GPIO_IRQ_EDGE_FALL, true, &my_gpio_callback);
    gpio_set_irq_enabled(BTN_B, GPIO_IRQ_EDGE_FALL, true);

    // Tela inicial: Procurando WIFI...
    ssd1306_fill(&disp, false);
    ssd1306_draw_string(&disp, "Procurando", 28, 20);
    ssd1306_draw_string(&disp, "   WIFI...", 28, 30);
    ssd1306_send_data(&disp);

    // Inicializa Wi-Fi
    if (cyw43_arch_init()) {
        printf("Falha ao inicializar Wi-Fi.\n");
        return 1;
    }
    cyw43_arch_enable_sta_mode();
    gpio_put(LED_WIFI, false);

    // Tenta conectar
    while (cyw43_arch_wifi_connect_timeout_ms("Kenshin", "AjIDias1994*", CYW43_AUTH_WPA2_AES_PSK, 30000) != 0) {
        printf("Tentando conectar...\n");
    }
    printf("Conectado ao Wi-Fi!\n");
    gpio_put(LED_WIFI, true);

    // --- Após conectar, exibe WIFI ON! e IP ---
    {
        ssd1306_fill(&disp, false);
        ssd1306_draw_string(&disp, "WIFI ON!", 40, 10);

        // Captura IP e exibe no display
        char ip_str[32];
        snprintf(ip_str, sizeof(ip_str), "%s", ip4addr_ntoa(netif_ip4_addr(netif_default)));
        ssd1306_draw_string(&disp, ip_str, 10, 25);

        ssd1306_send_data(&disp);
        sleep_ms(2000); 
    }

    // Exibe "BitBraile"
    ssd1306_fill(&disp, false);
    ssd1306_draw_string(&disp, "BitBraile", 30, 25);
    ssd1306_send_data(&disp);
    sleep_ms(1000);

    // Inicia servidor HTTP
    httpd_init();
    cgi_init();
    printf("Servidor HTTP iniciado.\n");

    // Loop principal
    while (1) {
        // Verifica se acabou o tempo de algum buzzer
        update_buzzer(&buzzerA_state);
        update_buzzer(&buzzerB_state);

        // Só mexemos o joystick se has_letter == true e não estamos no feedback
        if (has_letter && !showing_feedback) {
            read_joystick_and_select();
        }

        tight_loop_contents();
        sleep_ms(50);
    }
    return 0;
}
