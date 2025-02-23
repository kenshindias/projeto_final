
# 🟢 BitBraille - Sistema Interativo de Aprendizado em Braille com Raspberry Pi Pico W

# ✨ Introdução: Um Recurso Educacional para Professores no Ensino de Braille
Este projeto, desenvolvido no âmbito do programa EmbarcaTech: Formação Básica em Software Embarcado, implementa um sistema interativo de aprendizado em Braille utilizando um Raspberry Pi Pico W.

$ ⚠️ Importante: 
Este projeto não foi desenvolvido para pessoas cegas, mas sim como uma ferramenta educacional para professores e educadores. A proposta é auxiliar no ensino do alfabeto Braille, permitindo que professores utilizem a interface visual e tátil para ensinar crianças cegas de forma mais prática e intuitiva.

💡 O sistema combina LEDs WS2812, um display OLED, buzzers, botões e um joystick, criando uma experiência de aprendizado interativa.

# 🚀 Próximo Passo: Reconhecimento de Voz
Vale lembrar que este é um projeto inicial. A ideia futura é integrar um sistema de reconhecimento de voz, onde a letra falada será automaticamente exibida na matriz de LEDs 5x5, tornando o processo de aprendizado ainda mais dinâmico e acessível.

Este projeto busca ser uma ponte entre professores e alunos, proporcionando um ambiente educacional mais inclusivo e eficaz! 🦾📚

---

## 📌 **Funcionalidades**
✅ Exibe letras do alfabeto Braille em uma **matriz 5x5 de LEDs WS2812**.  
✅ Recebe letras via **Wi-Fi** (rede WPA2).  
✅ **Joystick** permite navegar pelas opções de resposta.  
✅ **Botão A (GPIO 5):** Retorna ao menu após feedback.  
✅ **Botão B (GPIO 6):** Verifica se a resposta selecionada está correta.  
✅ **Buzzers** indicam acerto ou erro.  
✅ **Display OLED SSD1306** exibe as opções e status do sistema.  
✅ Implementa **debouncing** para evitar leituras duplicadas.  

---

## 🎥 **Demonstração**
Confira o funcionamento do projeto neste vídeo:

![Demonstração do Projeto BitBraille]()

---

## 🛠 **Componentes Necessários**
- 🖥 **Raspberry Pi Pico W**  
- 🟢 **Matriz WS2812 (NeoPixel) 5x5** (conectada ao **GPIO 7**)  
- 📟 **Display OLED SSD1306 (128x64)** via I2C (**GPIO 14** e **GPIO 15**)  
- 🕹 **Joystick analógico** (X: **GPIO 27**, Y: **GPIO 26**)  
- 🔘 **Botão A** (**GPIO 5**, ligado a GND)  
- 🔘 **Botão B** (**GPIO 6**, ligado a GND)  
- 🎵 **Buzzer A (Vitória)** no **GPIO 21**  
- 🎵 **Buzzer B (Erro)** no **GPIO 10**  
- 📏 **Resistores de pull-up internos** ativados via software  
- 🔌 **Fonte de alimentação 5V** para os WS2812  

---

## 🔌 **Esquema de Conexão**

| **Componente**        | **Pino no Pico**  |
|-----------------------|-------------------|
| Matriz WS2812 (DIN)   | GPIO 7            |
| Display OLED (SDA)    | GPIO 14           |
| Display OLED (SCL)    | GPIO 15           |
| Joystick (Eixo X)     | GPIO 27 (ADC0)    |
| Joystick (Eixo Y)     | GPIO 26 (ADC1)    |
| Botão A               | GPIO 5            |
| Botão B               | GPIO 6            |
| Buzzer A (Vitória)    | GPIO 21           |
| Buzzer B (Erro)       | GPIO 10           |
| VCC (WS2812 e OLED)   | 5V                |
| GND (Geral)           | GND               |

---

## ⚙ **Configuração do Ambiente**
Antes de compilar e rodar o código no Raspberry Pi Pico W, configure o ambiente de desenvolvimento:

### **1️⃣ Instalar o SDK do Raspberry Pi Pico**
Se ainda não tiver o SDK do Pico configurado, siga as instruções [oficiais aqui](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf).

**No Linux:**
```bash
git clone https://github.com/raspberrypi/pico-sdk.git --branch master
cd pico-sdk
git submodule update --init
export PICO_SDK_PATH=$(pwd)
```

**No Windows (PowerShell):**
```powershell
git clone https://github.com/raspberrypi/pico-sdk.git --branch master
cd pico-sdk
git submodule update --init
$env:PICO_SDK_PATH = Get-Location
```

---

### **2️⃣ Compilar o Projeto**

1. **Clone este repositório:**
```bash
git clone https://github.com/seu-usuario/BitBraille.git
cd BitBraille
```

2. **Criar a pasta de build:**
```bash
mkdir build
cd build
```

3. **Gerar os arquivos de build:**
```bash
cmake ..
```

4. **Compilar o projeto:**
```bash
make
```

Isso gerará um arquivo **`.uf2`** pronto para ser carregado no Raspberry Pi Pico W.

---

### **3️⃣ Gravar no Raspberry Pi Pico W**

1. Conecte o **Pico W** ao computador segurando o botão **BOOTSEL**.  
2. Ele será detectado como um **dispositivo USB**.  
3. Arraste e solte o arquivo **.uf2** gerado na pasta `/build/`.  
4. O Pico irá **reiniciar automaticamente** e executar o código!  

---

## 🛠 **Como o Código Funciona**

### 💡 **Recepção de Letras via Wi-Fi**
O sistema utiliza a interface **CYW43** do Pico W para se conectar a uma rede Wi-Fi configurada no código.  
Quando uma requisição HTTP do tipo `GET` é enviada para o endpoint `/send.cgi?letra=X`, a letra `X` é processada, e a matriz WS2812 exibe a representação Braille correspondente.

---

### 💡 **Exibição em Braille**
Cada letra é exibida em uma **matriz 5x5 de LEDs WS2812**, seguindo o padrão Braille.  
Os pontos ativos da letra são destacados em **verde** (`0x00FF00`), enquanto os pontos inativos permanecem apagados (`0x000000`).

**Exemplo de mapeamento Braille para a letra "A":**
```c
const uint8_t braille_map[26][6] = {
    {1, 0, 0, 0, 0, 0}, // A
};
```

---

### 💡 **Navegação com Joystick**
O usuário utiliza o **joystick** para selecionar a resposta correta.  
- **Para cima:** Move para a próxima opção.  
- **Para baixo:** Move para a opção anterior.  
- **Botão B:** Confirma a resposta selecionada.  

---

### 💡 **Feedback de Resposta**
- **Resposta Correta:** O **Buzzer A** toca a **frequência de 1000 Hz** por 500 ms, e o display exibe `"Correto!"`.  
- **Resposta Incorreta:** O **Buzzer B** toca a **frequência de 300 Hz** por 500 ms, e o display exibe `"Errado!"`.  

**Código de verificação:**
```c
if (options[selected_option] == current_letter) {
    start_buzzer(&buzzerA_state, BUZZER_A, VICTORY_FREQ, SOUND_DURATION);
    ssd1306_draw_string(&disp, "Correto!", 35, 25);
} else {
    start_buzzer(&buzzerB_state, BUZZER_B, DEFEAT_FREQ, SOUND_DURATION);
    ssd1306_draw_string(&disp, "Errado!", 35, 25);
}
```

---

### 💡 **Conexão Wi-Fi**
O projeto se conecta automaticamente a uma rede Wi-Fi WPA2 configurada no código.  
Altere as credenciais no arquivo `main.c` para sua rede local:

```c
while (cyw43_arch_wifi_connect_timeout_ms("SeuSSID", "SuaSenha123", CYW43_AUTH_WPA2_AES_PSK, 30000) != 0) {
    printf("Tentando conectar...
");
}
```

---

## 🔍 **Possíveis Melhorias Futuras**
🟡 Adicionar suporte para **números e símbolos** em Braille.  
🟡 Implementar um **modo de aprendizado** com dicas sonoras.  
🟡 Suporte a **multijogador** com diferentes níveis de dificuldade.  
🟡 **Aprimorar a interface web** para controle remoto das letras enviadas.  

---

## 🖨 **Saída Esperada**
Quando o projeto estiver rodando, o display OLED mostrará a letra recebida e as três opções de resposta.  
**Exemplo de saída no display OLED:**

```
Letra: X
# A
  B
  X
```

Ao selecionar a resposta correta e pressionar o botão **B**, a mensagem `"Correto!"` será exibida.  
Se a resposta estiver errada, a mensagem `"Errado!"` será mostrada.  

---

## 📜 **Licença**
Este projeto é de código aberto e pode ser utilizado para fins educacionais ou pessoais.  
Sinta-se à vontade para contribuir ou modificar conforme necessário!

---

## 🤝 **Contribuições**
Contribuições são bem-vindas! Se você deseja sugerir melhorias ou reportar problemas, abra uma *issue* ou envie um *pull request*.

**Desenvolvido por:** [@kenshindias](https://github.com/kenshindias)  
**Programa:** [EmbarcaTech - Softex](https://embarcatech.softex.br/)

---

🚀 **Vamos juntos impulsionar a acessibilidade e a inclusão por meio da tecnologia embarcada!** 🦾
