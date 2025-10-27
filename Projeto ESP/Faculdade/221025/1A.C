// ...existing code...
#include <avr/io.h>
#include <avr/interrupt.h>
#define cpl_bit(y,bit) (y^=(1<<bit)) //troca o estado lógico do bit x da variável Y
#define LED2 PD5
#define LED1 PD3

// ...existing code...
unsigned int contador = 0;

// Configurações do motor e botão
#define STEPPER_MASK 0x0F      // PB0..PB3
#define BUTTON_PIN PC2         // botão em PC2
#define STEPS_PER_REV 200      // ajuste se seu motor tiver outro valor

volatile uint8_t step_index = 0;
volatile uint16_t steps_to_move = 0;

const uint8_t step_seq[4] = {0x01, 0x02, 0x04, 0x08}; // full-step 4 fases

ISR(TIMER1_OVF_vect){
    TCNT1 = 49911; //Recarrega o registrador para gerar 1s novamente
    cpl_bit(PORTD, LED2); //Inverte o estado do LED
}

ISR(TIMER0_OVF_vect){
    // Timer0 ~10ms (com prescaler 1024 e TCNT0 = 100)
    static uint8_t last_sample = 1;
    static uint8_t stable_count = 0;
    static uint8_t debounced_state = 1; // 1 = released (pull-up)
    static uint8_t step_timer = 0;

    contador++;
    TCNT0 = 100; //Recarrega o registrador para gerar 10ms novamente

    // Debounce simples
    uint8_t sample = (PINC & (1<<BUTTON_PIN)) ? 1 : 0;
    if(sample != last_sample){
        stable_count = 0;
    } else if(stable_count < 5){
        stable_count++;
    }
    if(stable_count == 5){
        if(sample == 0 && debounced_state == 1){
            // borda de pressão detectada -> solicitar 90° no sentido horário
            steps_to_move = (STEPS_PER_REV / 4); // 90°
        }
        debounced_state = sample;
    }
    last_sample = sample;

    // Controle do motor de passo: executa um passo a cada 10ms
    if(steps_to_move > 0){
        if(step_timer++ >= 1){ // 1 * 10ms = 10ms por passo (ajuste aqui)
            step_timer = 0;
            step_index = (step_index + 1) & 0x03; // sentido horário
            PORTB = (PORTB & ~STEPPER_MASK) | step_seq[step_index];
            steps_to_move--;
            if(steps_to_move == 0){
                // desenergiza bobinas ao terminar (opcional)
                PORTB &= ~STEPPER_MASK;
            }
        }
    }

    if(contador == 50){
        contador = 0;
        cpl_bit(PORTD, LED1);
    }
}

int main() {
    // LEDs
    DDRD = 0b00101000; // PD3 e PD5 como saída
    PORTD = 0b00000000;

    // Motor de passo PB0..PB3 como saída
    DDRB |= STEPPER_MASK;
    PORTB &= ~STEPPER_MASK;

    // Botão PC2 entrada com pull-up
    DDRC &= ~(1<<BUTTON_PIN);
    PORTC |= (1<<BUTTON_PIN);

    // Timer1: 1s (prescaler 1024)
    TCCR1B = 0b00000101; // CS12:0 = 101
    TCNT1 = 49911;
    TIMSK1 = (1<<TOIE1); // habilita interrupção de overflow Timer1

    // Timer0: ~10ms ticks (prescaler 1024)
    TCCR0B = 0b00000101; // CS02:0 = 101
    TCNT0 = 100;
    TIMSK0 = (1<<TOIE0); // habilita interrupção de overflow Timer0

    sei(); // habilita interrupções globais

    while (1) {
        // loop principal vazio; toda lógica está nas ISRs
    }
}
// ...existing code...