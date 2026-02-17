#include "include/cart.h"
#include "include/cpu.h"
#include "include/mmu.h"
#include "include/ppu.h"

int main(int argc, char const *argv[]){
    if(argc > 2){
        perror("Wrong usage of arguments, needed <file.gb>");
        exit(EXIT_FAILURE);
    }

    
    return 0;
}
