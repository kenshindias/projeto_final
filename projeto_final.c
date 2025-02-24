#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Bibliotecas de hardware e do SDK do Pico
#include "hardware/pwm.h"
#include "hardware/watchdog.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

// Pilha TCP/IP (lwIP)
#include "lwip/apps/httpd.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"

// Biblioteca do display OLED
#include "inc/ssd1306.h"

// Programa PIO para controle dos LEDs WS2812 (Neopixel)
#include "ws2812.pio.h"


/* -------------------------------------------------------
 *                 DEFINES E CONSTANTES
 * -------------------------------------------------------
 *  Aqui definimos os pinos e configurações importantes
 *  para cada periférico/utilização no projeto.
 */

#define I2C_PORT         i2c1
#define SDA_PIN          14
#define SCL_PIN          15
#define OLED_ADDR        0x3C

// Joystick
#define JOYSTICK_X       27  // ADC0
#define JOYSTICK_Y       26  // ADC1
#define BTN_B            6
#define BTN_A            5

// LED para indicar status do Wi-Fi
#define LED_WIFI         12

// Neopixel
#define NEOPIXEL_PIN     7
#define NUM_LEDS         25

// Buzzers
#define BUZZER_A         21
#define BUZZER_B         10
#define VICTORY_FREQ     1000
#define DEFEAT_FREQ      300
#define SOUND_DURATION   500  // Duração do som em milissegundos

// Limiar de valores do Joystick
#define THRESHOLD_UP     1000
#define THRESHOLD_DOWN   2100
#define DEADZONE         200

// Estrutura para manipular o display OLED
ssd1306_t disp;

/* -------------------------------------------------------
 *           VARIÁVEIS GLOBAIS E ESTRUTURAS
 * -------------------------------------------------------
 *  Aqui guardamos variáveis de estado importantes do
 *  programa, como a letra a ser exibida e o buffer do WS2812.
 */

// Indica se já recebemos uma letra via Wi-Fi
bool has_letter = false;

// Letra atual (recebida via Wi-Fi) e opções de resposta
char current_letter;
char options[3];
int selected_option = 0;

// Buffer para os 25 LEDs WS2812 (Neopixel)
uint32_t led_matrix[NUM_LEDS];

// Mapeamento físico dos 25 LEDs WS2812 em matriz 5x5
static const uint8_t LEDmap[5][5] = {
    {24, 23, 22, 21, 20},
    {15, 16, 17, 18, 19},
    {14, 13, 12, 11, 10},
    { 5,  6,  7,  8,  9},
    { 4,  3,  2,  1,  0}
};

// Posições dos 6 LEDs que representarão os pontos em Braille
// dentro da matriz WS2812
const int braille_leds[6] = {
    17, // Corresponde a LEDmap[1][2]
    12, // Corresponde a LEDmap[2][2]
    7,  // Corresponde a LEDmap[3][2]
    18, // Corresponde a LEDmap[1][3]
    11, // Corresponde a LEDmap[2][3]
    8   // Corresponde a LEDmap[3][3]
};

// Mapa Braille (A-Z): cada linha indica se cada um dos 6 pontos
// deve estar aceso (1) ou apagado (0)
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


/* -------------------------------------------------------
 *       ESTRUTURA PARA BUZZER NÃO-BLOQUEANTE
 * -------------------------------------------------------
 *  Define o estado atual de um buzzer (ativa, até quando,
 *  qual slice de PWM etc.) para tocá-lo sem travar o código.
 */

typedef struct {
    bool active;
    absolute_time_t end_time;
    uint slice;
    uint channel;
} BuzzerState;

// Criação de duas instâncias de buzzer para sons diferentes
BuzzerState buzzerA_state = { false };
BuzzerState buzzerB_state = { false };

// Indica se estamos exibindo "Correto!/Errado!" (feedback),
// travando o joystick nesse período
static bool showing_feedback = false;


/* -------------------------------------------------------
 *          FUNÇÕES DE INICIALIZAÇÃO DE HARDWARE
 * -------------------------------------------------------
 */

// Inicializa o LED que indica status do Wi-Fi
void init_led_wifi() {
    gpio_init(LED_WIFI);
    gpio_set_dir(LED_WIFI, GPIO_OUT);
    gpio_put(LED_WIFI, false);
}

// Inicializa a comunicação I2C e o display OLED
void init_oled() {
    // Inicialização do I2C
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    // Inicialização da biblioteca ssd1306
    ssd1306_init(&disp, 128, 64, false, OLED_ADDR, I2C_PORT);
    ssd1306_config(&disp);

    // Exemplo: apaga a tela e exibe mensagem inicial
    ssd1306_fill(&disp, false);
    ssd1306_draw_string(&disp, "Display ON", 5, 5);
    ssd1306_send_data(&disp);
    printf("Display OLED inicializado.\n");
}

// Inicializa o PIO para controlar os LEDs WS2812 (Neopixel)
void init_neopixel() {
    PIO pio = pio0;
    int sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, NEOPIXEL_PIN, 800000, false);

    // Inicia todos os LEDs apagados
    for (int i = 0; i < NUM_LEDS; i++) {
        led_matrix[i] = 0x000000;       // Cor em formato 0xRRGGBB
        pio_sm_put_blocking(pio, sm, 0 << 8);
    }
    printf("Matriz WS2812B inicializada (apagada).\n");
}

// Inicializa o ADC para leitura do joystick
void adc_init_joystick() {
    adc_init();
    adc_gpio_init(JOYSTICK_X); // ADC0
    adc_gpio_init(JOYSTICK_Y); // ADC1
}


/* -------------------------------------------------------
 *     FUNÇÕES DE CONTROLE DO BUZZER (NÃO-BLOQUEANTE)
 * -------------------------------------------------------
 *  Essas funções permitem tocar um som temporário no buzzer
 *  sem travar o resto do programa.
 */

// Inicia o buzzer com PWM na frequência e duração desejada
void start_buzzer(BuzzerState *state, uint gpio, uint freq, uint duration_ms) {
    state->active = true;
    state->end_time = make_timeout_time_ms(duration_ms);

    uint slice = pwm_gpio_to_slice_num(gpio);
    uint channel = pwm_gpio_to_channel(gpio);
    state->slice = slice;
    state->channel = channel;

    // Configura PWM de acordo com a frequência especificada
    pwm_config config = pwm_get_default_config();
    uint32_t sys_clk = 125000000; // 125 MHz
    float div = 100.0f;
    uint32_t wrap = (sys_clk / (uint32_t)(div * freq)) - 1;

    pwm_config_set_wrap(&config, wrap);
    pwm_config_set_clkdiv(&config, div);
    pwm_init(slice, &config, true);

    // Define duty cycle em ~50%
    pwm_set_chan_level(slice, channel, wrap / 2);
}

// Verifica se passou o tempo de tocar o buzzer
// e desliga se necessário
void update_buzzer(BuzzerState *state) {
    if (!state->active) return;
    if (time_reached(state->end_time)) {
        pwm_set_chan_level(state->slice, state->channel, 0); // desliga o PWM
        state->active = false;
    }
}


/* -------------------------------------------------------
 *         FUNÇÕES DE EXIBIÇÃO (WS2812 / OLED)
 * -------------------------------------------------------
 */

// Ajusta a cor de um pixel específico do array led_matrix
void set_pixel(int index, uint32_t color) {
    if (index >= 0 && index < NUM_LEDS) {
        led_matrix[index] = color;
    }
}

// Atualiza os LEDs físicos WS2812 conforme o array led_matrix
void update_neopixel() {
    PIO pio = pio0;
    int sm = 0;
    for (int i = 0; i < NUM_LEDS; i++) {
        pio_sm_put_blocking(pio, sm, led_matrix[i] << 8);
    }
}

// Exibe a letra em Braille na matriz WS2812
void display_braille(char letter) {
    if (!has_letter) return;

    // Apaga todos os LEDs
    for (int i = 0; i < NUM_LEDS; i++) {
        led_matrix[i] = 0x000000;
    }

    // Índice (A=0, B=1, ..., Z=25)
    int idx = letter - 'A';
    if (idx < 0 || idx > 25) return;

    // Acende os pontos que formam a letra
    for (int i = 0; i < 6; i++) {
        if (braille_map[idx][i]) {
            set_pixel(braille_leds[i], 0x00FF00); // verde
        }
    }
    update_neopixel();
}

// Gera três opções de resposta (incluindo a correta)
void generate_options(char correct) {
    // Primeira opção é a correta
    options[0] = correct;
    // Duas opções aleatórias
    options[1] = 'A' + (rand() % 26);
    options[2] = 'A' + (rand() % 26);

    // Embaralha as opções (Fisher-Yates simples)
    for (int i = 2; i > 0; i--) {
        int j = rand() % (i + 1);
        char temp = options[i];
        options[i] = options[j];
        options[j] = temp;
    }
    // Por padrão, a opção selecionada é a primeira (índice 0)
    selected_option = 0;
}

// Exibe, no display OLED, as opções atuais e marca a selecionada
void display_options() {
    ssd1306_fill(&disp, false);
    ssd1306_draw_string(&disp, "Letra:", 5, 0);

    for (int i = 0; i < 3; i++) {
        char buf[20];
        if (i == selected_option) {
            // Usamos '#' para indicar visualmente a opção selecionada
            snprintf(buf, sizeof(buf), "# %c", options[i]);
        } else {
            snprintf(buf, sizeof(buf), "   %c", options[i]);
        }
        ssd1306_draw_string(&disp, buf, 10, 16 + i * 14);
    }
    ssd1306_send_data(&disp);
}


/* -------------------------------------------------------
 *         ROTINA DE INTERRUPÇÃO DOS BOTÕES
 * -------------------------------------------------------
 *  Quando um botão é pressionado, esta função é chamada.
 *  O botão A (BTN_A) retorna da tela de "Correto!/Errado!".
 *  O botão B (BTN_B) avalia se a opção escolhida está certa.
 */

void my_gpio_callback(uint gpio, uint32_t events) {
    // Botão A
    if (gpio == BTN_A && (events & GPIO_IRQ_EDGE_FALL)) {
        // Se estamos mostrando feedback, o botão A retorna às opções
        if (showing_feedback) {
            display_options();       // Mostra novamente as opções
            has_letter = true;       // Reativa o joystick
            showing_feedback = false;
        }
    }
    // Botão B -> Verifica resposta (Correto ou Errado)
    else if (gpio == BTN_B && (events & GPIO_IRQ_EDGE_FALL)) {
        if (!has_letter) return;

        // Testa se a opção atual é a correta
        if (options[selected_option] == current_letter) {
            // Se correta, reproduz som de vitória no buzzerA
            start_buzzer(&buzzerA_state, BUZZER_A, VICTORY_FREQ, SOUND_DURATION);
            ssd1306_fill(&disp, false);
            ssd1306_draw_string(&disp, "Correto!", 35, 25);
        } else {
            // Se errada, reproduz som de derrota no buzzerB
            start_buzzer(&buzzerB_state, BUZZER_B, DEFEAT_FREQ, SOUND_DURATION);
            ssd1306_fill(&disp, false);
            ssd1306_draw_string(&disp, "Errado!", 35, 25);
        }
        ssd1306_send_data(&disp);

        // Ativamos a tela de feedback (joystick fica travado)
        showing_feedback = true;
        has_letter = false;
    }
}


/* -------------------------------------------------------
 *  LEITURA DO JOYSTICK (NO LOOP PRINCIPAL)
 * -------------------------------------------------------
 *  Verifica se o joystick foi movido para cima ou para baixo
 *  e muda a opção selecionada de acordo.
 *  Utiliza um intervalo de tempo para evitar mudanças muito
 *  rápidas (debounce).
 */

void read_joystick_and_select() {
    // Seleciona a entrada ADC0 para ler X do joystick
    adc_select_input(0);
    uint16_t raw_x = adc_read();

    // Tempo mínimo entre movimentos (debounce)
    static absolute_time_t last_move; 
    const uint64_t MOVE_INTERVAL = 600000; // 600 microsegundos = 0,6 ms

    // Verifica se ainda não passou tempo suficiente para outro movimento
    if (absolute_time_diff_us(last_move, get_absolute_time()) < MOVE_INTERVAL) {
        return;
    }

    // Se o valor estiver abaixo do limiar, consideramos como "cima"
    if (raw_x < THRESHOLD_UP) {
        selected_option = (selected_option + 1) % 3;
        display_options();
        last_move = get_absolute_time();
    }
    // Se acima do outro limiar, consideramos como "baixo"
    else if (raw_x > THRESHOLD_DOWN) {
        // (selected_option + 2) % 3 é o mesmo que (selected_option - 1) % 3,
        // porém sem risco de ficar negativo
        selected_option = (selected_option + 2) % 3;
        display_options();
        last_move = get_absolute_time();
    }
}


/* -------------------------------------------------------
 *     HANDLER PARA CGI (INTERAÇÃO VIA REQUISIÇÃO WEB)
 * -------------------------------------------------------
 *  Sempre que uma requisição HTTP /send.cgi chegar, esta função
 *  é chamada para tratar parâmetros via GET/POST. Se o parâmetro
 *  "letra" for recebido, exibimos essa letra na matriz e
 *  geramos as opções correspondentes.
 */

const char *cgi_handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]) {
    for (int i = 0; i < iNumParams; i++) {
        if (strcmp(pcParam[i], "letra") == 0 && strlen(pcValue[i]) == 1) {
            // Ativa a exibição da letra
            has_letter = true;
            current_letter = (char) toupper(pcValue[i][0]);
            printf("Letra recebida: %c\n", current_letter);

            // Exibe a letra em Braille, gera opções e mostra no display
            display_braille(current_letter);
            generate_options(current_letter);
            display_options();

            // Sai do modo de feedback, se necessário
            showing_feedback = false;
            break;
        }
    }
    return "/index.shtml"; // Redireciona para a página principal
}

// Registra o handler CGI no servidor HTTP
void cgi_init(void) {
    static const tCGI cgi_handlers[] = {
        {"/send.cgi", cgi_handler}
    };
    http_set_cgi_handlers(cgi_handlers, sizeof(cgi_handlers) / sizeof(tCGI));
}


/* -------------------------------------------------------
 *                       FUNÇÃO MAIN
 * -------------------------------------------------------
 *  Ponto de entrada do programa. Inicializa hardware,
 *  conecta ao Wi-Fi, inicia o servidor HTTP e entra
 *  no loop principal, gerenciando buzzer e joystick.
 */

int main() {
    // Inicializa o sistema de entrada/saída padrão
    stdio_init_all();

    // Inicializações de hardware e periféricos
    init_led_wifi();
    init_oled();
    init_neopixel();
    adc_init_joystick();

    // Configura os botões (pinos como entrada com pull-up)
    gpio_init(BTN_A);
    gpio_set_dir(BTN_A, GPIO_IN);
    gpio_pull_up(BTN_A);

    gpio_init(BTN_B);
    gpio_set_dir(BTN_B, GPIO_IN);
    gpio_pull_up(BTN_B);

    // Configura os buzzers como pinos de PWM
    gpio_set_function(BUZZER_A, GPIO_FUNC_PWM);
    gpio_set_function(BUZZER_B, GPIO_FUNC_PWM);

    // Ativa interrupções de borda de descida (fall) para os botões
    gpio_set_irq_enabled_with_callback(BTN_A, GPIO_IRQ_EDGE_FALL, true, &my_gpio_callback);
    gpio_set_irq_enabled(BTN_B, GPIO_IRQ_EDGE_FALL, true);

    // Mensagem no display enquanto busca Wi-Fi
    ssd1306_fill(&disp, false);
    ssd1306_draw_string(&disp, "Procurando", 28, 20);
    ssd1306_draw_string(&disp, "   WIFI...", 28, 30);
    ssd1306_send_data(&disp);

    // Inicializa Wi-Fi (CYW43)
    if (cyw43_arch_init()) {
        printf("Falha ao inicializar Wi-Fi.\n");
        return 1;
    }
    cyw43_arch_enable_sta_mode();
    gpio_put(LED_WIFI, false);

    // Tenta conectar ao AP específico
    while (cyw43_arch_wifi_connect_timeout_ms("Kenshin", "AjIDias1994*", CYW43_AUTH_WPA2_AES_PSK, 30000) != 0) {
        printf("Tentando conectar...\n");
    }
    printf("Conectado ao Wi-Fi!\n");
    gpio_put(LED_WIFI, true);

    // Exibe "WIFI ON!" e o IP adquirido
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

    // Exibe a tela inicial do jogo
    ssd1306_fill(&disp, false);
    ssd1306_draw_string(&disp, "BitBraile", 30, 25);
    ssd1306_send_data(&disp);
    sleep_ms(1000);

    // Inicia o servidor HTTP e registra o CGI
    httpd_init();
    cgi_init();
    printf("Servidor HTTP iniciado.\n");

    // Loop principal
    while (1) {
        // Atualiza o estado dos buzzers (não-bloqueante)
        update_buzzer(&buzzerA_state);
        update_buzzer(&buzzerB_state);

        // Lê o joystick apenas se já tivermos uma letra ativa
        // e não estivermos na tela de feedback
        if (has_letter && !showing_feedback) {
            read_joystick_and_select();
        }

        // Aguarda um pouco (para não sobrecarregar o processador)
        tight_loop_contents();
        sleep_ms(50);
    }

    return 0;
}
