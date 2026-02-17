#include "include/cart.h"
#include "include/cpu.h"
#include "include/mmu.h"
#include "include/ppu.h"

int main(int argc, char const *argv[]){
    if(argc > 2){
        perror("Wrong usage of arguments, needed <file.gb>");
        exit(EXIT_FAILURE);
    }

    printf("%s\n", argv[1]);

    read_cart(argv[1]);
    
    return 0;
}
