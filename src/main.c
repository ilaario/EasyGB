#include "include/cpu.h"
#include "include/mmu.h"
#include "include/ppu.h"
#include "include/debug.h"

cartridge cart;
bus mbus;
cpu mcpu;

int main(int argc, char const *argv[]){
    dbg_init();

    if(argc != 2){
        perror("Wrong usage of arguments, needed <file.gb>");
        exit(EXIT_FAILURE);
    }

    printf("%s\n", argv[1]);
    dbg_log("Booting ROM: %s", argv[1]);

    cart = read_cart(argv[1]);
    mbus = bus_init(cart);
    mcpu = cpu_init(mbus);

    // snapshot_bus(mbus);

    while (true) {
        cpu_step(mcpu);
    }
    
    return 0;
}
