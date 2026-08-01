 /*

 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD

 *

 * SPDX-License-Identifier: MIT

 */

#include <Arduino.h>

#include <M5StamPLC.h>

#include <JC_Button.h>

#include "dashboard_ui.h"


#define RELAY_ON_TIME   1000

#define RUNNING_TIMEOUT 50000

#define START_DEBOUNCE_MS 200


#define STICK1_PIN 0

#define STICK2_PIN 1

#define STICK3_PIN 2

#define STICK4_PIN 3

#define BUTTON_PIN 4

#define START_PIN 5


DashboardUI dashboard_ui;

M5StamPLC_AC stamplc_ac;


typedef enum machineStatus

{

    STATUS_IDLE,

    STATUS_RELAY_ON,

    STATUS_SCREEN,

    STATUS_WAIT_TIMER,

    STATUS_RUNNING,

    STATUS_NUMCOUNT,

    STATUS_CONFIG_MAPPING

} MachineStatus;


//Button myButton(BUTTON_PIN, 25, false, true);

MachineStatus machineStatus;

unsigned long lastStatusChange;

int relayToStickMap[4] = {STICK1_PIN, STICK2_PIN, STICK3_PIN, STICK4_PIN}; // Mapa padrão inicial

int configStep = 0;

bool waitingForStickRelease = false;


/* Acumulador de linha da serial (recepcao nao-bloqueante) */

char serialLine[32];

uint8_t serialLineLen = 0;


/* Botao de inicio: deteccao de borda */

bool lastStartInput = false;

unsigned long lastStartSend = 0;


/* Sinaliza que acabamos de entrar em um estado (acoes de entrada) */

bool statusEntryPending = false;


const char* configPrompts[4] = {

    "Joystick para cima",     // Relay 0

    "Joystick para esquerda", // Relay 1 (Sentido anti-horário)

    "Joystick para baixo",    // Relay 2

    "Joystick para direita"   // Relay 3

};


void update_time_and_date()

{

    static uint32_t time_count = 0;

    char string_buffer[100];


    if (millis() - time_count > 1000) {

        struct tm time;


        /* Get time */

        M5StamPLC.getRtcTime(&time);

        snprintf(string_buffer, sizeof(string_buffer), "%02d:%02d:%02d", time.tm_hour, time.tm_min, time.tm_sec);

        dashboard_ui.statusTime = string_buffer;


        /* Get date */

        snprintf(string_buffer, sizeof(string_buffer), "%04d.%02d.%02d", time.tm_year + 1900, time.tm_mon + 1,

                 time.tm_mday);

        dashboard_ui.statusDate = string_buffer;


        time_count = millis();

    }

}


void update_button_events()

{

    /* If button clicked, log out */

   

    if (M5StamPLC.BtnB.wasClicked()) {

        dashboard_ui.console_log("Modo Config Iniciado");

        machineStatus = STATUS_CONFIG_MAPPING;

        markStatusChanged();

        configStep = 0;

        waitingForStickRelease = false;

        dashboard_ui.console_log(configPrompts[configStep]);

    }


    /* Play button press and release tone */

    if (M5StamPLC.BtnA.wasPressed() || M5StamPLC.BtnB.wasPressed() || M5StamPLC.BtnC.wasPressed()) {

        M5StamPLC.tone(600, 20);

    } else if (M5StamPLC.BtnA.wasReleased() || M5StamPLC.BtnB.wasReleased() || M5StamPLC.BtnC.wasReleased()) {

        M5StamPLC.tone(800, 20);

    }

}


void update_plc_io_state()

{

    static uint32_t time_count = 0;


    if (millis() - time_count > 50) {

        for (int i = 0; i < 8; i++) {

            dashboard_ui.inputStateList[i] = M5StamPLC.readPlcInput(i);

        }

        for (int i = 0; i < 4; i++) {

            dashboard_ui.relayStateList[i] = M5StamPLC.readPlcRelay(i);

        }

        time_count = millis();

    }

}


/* Registra a troca de estado. Toda transicao deve chamar esta funcao para que
 * as acoes de entrada de cada estado disparem exatamente uma vez. */

void markStatusChanged()
{
    lastStatusChange   = millis();
    statusEntryPending = true;
}


/* Trata um comando completo vindo do tablet.
 * Chamado por pumpSerial() em QUALQUER estado da maquina. */

void handleCommand(const char* cmd)
{
    if (strcmp(cmd, "coin") == 0) {
        if (machineStatus == STATUS_IDLE) {
            dashboard_ui.console_log("Coin inserted!");
            relayOn();
            dashboard_ui.console_log("Relay On");
            machineStatus = STATUS_RELAY_ON;
            markStatusChanged();
        } else {
            /* Credito fora do IDLE: ignorado, mas visivel no console. */
            dashboard_ui.console_log("Coin ignorado (ocupado)");
        }
        return;
    }

    /* Comando desconhecido: registra para diagnostico */
    String msg = "RX? ";
    msg += cmd;
    dashboard_ui.console_log(msg.c_str());
}


/* Le tudo que houver na serial SEM bloquear, monta linhas terminadas por
 * '\n' ou '\r' e despacha para handleCommand().
 *
 * Precisa ser chamado a cada iteracao do loop(), em todos os estados: a fila
 * de recepcao do USB CDC tem apenas 256 bytes e, quando enche, a ISR descarta
 * o resto do pacote silenciosamente (HWCDC.cpp, xQueueSendFromISR). Era essa
 * a causa das mensagens "perdidas" do tablet. */

void pumpSerial()
{
    static bool discardingLine = false;

    while (Serial.available()) {
        char c = (char)Serial.read();

        if (c == '\n' || c == '\r') {
            if (discardingLine) {
                discardingLine = false;
            } else {
                /* Remove espacos a direita (equivale ao antigo String::trim) */
                while (serialLineLen > 0 && (serialLine[serialLineLen - 1] == ' ' ||
                                             serialLine[serialLineLen - 1] == '\t')) {
                    serialLineLen--;
                }
                if (serialLineLen > 0) {
                    serialLine[serialLineLen] = '\0';
                    handleCommand(serialLine);
                }
            }
            serialLineLen = 0;
            continue;
        }

        if (discardingLine) {
            continue;
        }

        /* Ignora espacos a esquerda */
        if (serialLineLen == 0 && (c == ' ' || c == '\t')) {
            continue;
        }

        if (serialLineLen < sizeof(serialLine) - 1) {
            serialLine[serialLineLen++] = c;
        } else {
            /* Linha maior que o buffer: descarta ate o proximo terminador,
             * para que o resto nao contamine o comando seguinte. */
            discardingLine = true;
            serialLineLen  = 0;
        }
    }
}


/* Botao de inicio: envia "0" UMA vez por acionamento (borda de subida).
 *
 * A versao anterior enviava "0" a cada iteracao enquanto o botao estivesse
 * pressionado. Isso enchia o buffer de transmissao (256 bytes), fazendo o
 * HWCDC::write bloquear ~1ms por tentativa, e travava o loop exatamente no
 * instante em que o tablet responderia. */

void updateStartButton()
{
    bool startInput = M5StamPLC.readPlcInput(START_PIN);

    if (startInput && !lastStartInput && (millis() - lastStartSend > START_DEBOUNCE_MS)) {
        dashboard_ui.console_log("Botao de inicio apertado");
        Serial.println("0");
        lastStartSend = millis();
    }

    lastStartInput = startInput;
}


void task_write_relay_regularly(void* param)

{

    delay(2000);


    bool relay_state = false;

    while (1) {

        relay_state = !relay_state;

        for (int i = 0; i < 4; i++) {

            M5StamPLC.writePlcRelay(i, relay_state);

            delay(500);

        }

        delay(1000);

    }

    vTaskDelete(NULL);

}


void setup()

{

    Serial.begin(115200);

    Serial.println("StamPLC connected to PC!");

   

    /* Init M5StamPLC */

    M5StamPLC.begin();


    /* Init M5StamPLC-AC */

    while (!stamplc_ac.begin()) {

        printf("M5StamPLC-AC init failed, retry in 1s...\n");

        delay(1000);

    }


    /* Init dashboard UI */

    dashboard_ui.init(&M5StamPLC.Display);


    /* Estado inicial explicito */

    machineStatus = STATUS_IDLE;

    markStatusChanged();


    /* Create a task to write relay regularly */

    //xTaskCreate(task_write_relay_regularly, "relay", 2048, NULL, 15, NULL);

}


void loop()

{

  /* Drena a serial em TODOS os estados, a cada iteracao. Nunca bloqueia. */

  pumpSerial();


  M5StamPLC.update();


  //update_time_and_date();

  update_button_events();

  update_plc_io_state();


  /* Render dashboard UI */

  dashboard_ui.render();


  //myButton.read();

  //checkInputs();


  updateStartButton();


  switch(machineStatus) {

  case STATUS_IDLE:

    /* "coin" chega por pumpSerial()/handleCommand(), que rodam em todos os
     * estados. Nao ha leitura de serial aqui. */

    break;


  case STATUS_RELAY_ON:

    if (millis() - lastStatusChange >= RELAY_ON_TIME) {

      relayOff();

      dashboard_ui.console_log("Relay Off");

      machineStatus = STATUS_SCREEN;

      markStatusChanged();

    }

    break;


  case STATUS_SCREEN:
    // Envia "ready" uma unica vez, na entrada do estado
    if (statusEntryPending) {
        Serial.println("ready");
        statusEntryPending = false;
    }

    // Mantem a pausa de 2s para a transicao de tela do tablet
    if (millis() - lastStatusChange >= 2000) {
        dashboard_ui.console_log("Aguardando inicio");
        machineStatus = STATUS_WAIT_TIMER;
        markStatusChanged();
    }
    break;

  case STATUS_WAIT_TIMER:

    if (M5StamPLC.readPlcInput(STICK1_PIN) ||
        M5StamPLC.readPlcInput(STICK2_PIN) ||
        M5StamPLC.readPlcInput(STICK3_PIN) ||
        M5StamPLC.readPlcInput(STICK4_PIN)) {
      
      // Envio unico, sem delay() e sem descartar o buffer de recepcao
      Serial.println("start");

      dashboard_ui.console_log("Contagem iniciada");
      machineStatus = STATUS_RUNNING;
      markStatusChanged();
    }
    break;

  case STATUS_RUNNING:

    // Loga apenas na entrada do estado, nao a cada iteracao
    if (statusEntryPending) {
      dashboard_ui.console_log("Time running");
      statusEntryPending = false;
    }

    mapSticksToRelays();
    
    if (M5StamPLC.readPlcInput(BUTTON_PIN) ||
        millis() - lastStatusChange > RUNNING_TIMEOUT) {
      
      // Envio unico, sem delay() bloqueante
      Serial.println("claw");
      
      dashboard_ui.console_log("Win/Lose");
      machineStatus = STATUS_IDLE;
      markStatusChanged();
    }
    break;


  case STATUS_CONFIG_MAPPING:

    // Variável estática para memorizar qual pino foi apertado até que seja solto

    static int pendingPin = -1;


    if (!waitingForStickRelease) {

      // 1. Aguardando o usuário PRESSIONAR o joystick

      if (M5StamPLC.readPlcInput(STICK1_PIN)) pendingPin = STICK1_PIN;

      else if (M5StamPLC.readPlcInput(STICK2_PIN)) pendingPin = STICK2_PIN;

      else if (M5StamPLC.readPlcInput(STICK3_PIN)) pendingPin = STICK3_PIN;

      else if (M5StamPLC.readPlcInput(STICK4_PIN)) pendingPin = STICK4_PIN;

     

      if (pendingPin != -1) {

        waitingForStickRelease = true; // Joystick pressionado, agora trava e espera soltar

      }

    } else {

      // 2. Aguardando o usuário SOLTAR o joystick para confirmar a gravação

      if (!M5StamPLC.readPlcInput(STICK1_PIN) &&

          !M5StamPLC.readPlcInput(STICK2_PIN) &&

          !M5StamPLC.readPlcInput(STICK3_PIN) &&

          !M5StamPLC.readPlcInput(STICK4_PIN)) {

       

        // Pequeno delay anti-bounce para ignorar o ruído da mola voltando ao centro

        delay(50);

       

        // Agora sim, com o joystick solto, efetivamos a configuração

        relayToStickMap[configStep] = pendingPin;

        String msg = "Relay " + String(configStep) + " OK!";

        dashboard_ui.console_log(msg.c_str());

       

        // Prepara para a próxima etapa

        pendingPin = -1;

        waitingForStickRelease = false;

        configStep++;

       

        if (configStep < 4) {

          dashboard_ui.console_log(configPrompts[configStep]); // Pede o próximo

        } else {

          dashboard_ui.console_log("Mapeamento concluido!");

          machineStatus = STATUS_IDLE; // Volta ao estado normal do jogo

          markStatusChanged();

        }

      }

    }

    break;

  }

}


void mapSticksToRelays() {

  for (int i = 0; i < 4; i++) {

    M5StamPLC.writePlcRelay(i, M5StamPLC.readPlcInput(relayToStickMap[i]));

  }

}


void relayOn() {

  stamplc_ac.writeRelay(true);

  stamplc_ac.setStatusLight(0, 1, 0);

}


void relayOff() {

  stamplc_ac.writeRelay(false);

  stamplc_ac.setStatusLight(1, 0, 0);

} 