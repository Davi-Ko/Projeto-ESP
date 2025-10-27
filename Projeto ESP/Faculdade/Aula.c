#define F_CPU 16000000UL    //frequência de trabalho 
#include <avr/io.h>         //definições do componente especificado 
#include <avr/interrupt.h>  //define algumas macros para as interrupções 
#include <util/delay.h>     //biblioteca para o uso das rotinas de delay 

//Protótipo da função de interrupção, obrigatório para sua utilização 
ISR(PCINT0_vect);
ISR(PCINT1_vect);


//Declara variável global ?contador? 
volatile int contador = 0;
volatile int hot = 0;
volatile int a = 0;
volatile int b = 0;
volatile int c = 0;
volatile int d = 0;
volatile int aux = 0;

int main() {

    DDRB = 0b00000001; //PB1 como saída (transistor do display) 
    PORTB = 0b00000001; //Liga o display de 7 segmentos 
    DDRC = 0b00000000; //PORTC como entrada 
    PORTC = 0b00001100; //habilita pull-ups apenas nos botões  
    DDRD = 0b11111111; //PORTD definido como saída 
    PORTD = 0b00000000; //apaga todos os leds 
    PCICR = 0b00000011; //hab. interrupção por mudança de sinal no PORTC 
    PCMSK0 = 0b00000100;
    PCMSK1 = 0b00001100; //hab. os pinos PCINT10 e 11 para gerar interrupção 
    sei(); //habilita a chave geral das interrupções 

    while (1) {

        //Envia o valor decodificado para o display de 7 segmentos 
        if (contador > 0){
            if (hot == 1){
                PORTD |= (1 << 0);
                _delay_ms(10);
                PORTD &= ~(1 << 0);

                PORTD |= (1 << 1);
                _delay_ms(10);
                PORTD &= ~(1 << 1);

                PORTD |= (1 << 2);
                _delay_ms(10);
                PORTD &= ~(1 << 2);

                PORTD |= (1 << 3);
                _delay_ms(10);
                PORTD &= ~(1 << 3);
            }
            else{
                PORTD |= (1 << 3);
                _delay_ms(10);
                PORTD &= ~(1 << 3);
                
                PORTD |= (1 << 2);
                _delay_ms(10);
                PORTD &= ~(1 << 2);
                
                PORTD |= (1 << 1);
                _delay_ms(10);
                PORTD &= ~(1 << 1);
                                
                PORTD |= (1 << 0);
                _delay_ms(10);
                PORTD &= ~(1 << 0);
            }
            contador --;
        }            

    }
}
//----------------------------------------------------------------------------- 

ISR(PCINT0_vect) {
    //Testa qual o pino que foi acionado 
    if (!(PINB & (1 << 2))) {
        hot=1;
    }

    else{
        hot=0;
    }
    _delay_ms(10);
}

ISR(PCINT1_vect) {
    //Testa qual o pino que foi acionado 
    if (!(PINC & (1 << 2))) {
        contador=128;
    }

    else if (!(PINC & (1 << 3))) {
        contador=128;
    }
    _delay_ms(10);
}


//=============================================================================