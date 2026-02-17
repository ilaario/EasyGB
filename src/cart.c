#include "include/cart.h"

FILE* cartridge_pointer;

cartridge read_cart(const char* filepath){
    cartridge_pointer = (FILE*)fopen(filepath, "rb");
    if(cartridge_pointer == NULL){
        perror("Error reading cart file!");
        exit(EXIT_FAILURE);
    }

    cartridge read_cart = (cartridge)malloc(sizeof(struct cartridge));
    if(read_cart == NULL){
        perror("Error allocating cartridge!");
        exit(EXIT_FAILURE);
    }

    read_cart -> head = read_header();
    return read_cart;
}

header read_header(){
    header read_header = (header)malloc(sizeof(struct header));
    if(read_header == NULL){
        perror("Error allocating header!");
        exit(EXIT_FAILURE);
    }

    size_t read_bytes = fread(read_header -> raw_header, 1, 0x0150, cartridge_pointer);
    printf("Bytes read from header = 0x%zx\n", read_bytes);
    if(read_bytes < 0x0150){
        perror("Invalid cartridge header!");
        exit(EXIT_FAILURE);
    }

    printf("Reading Entry Point: ");
    for(int i = 0x0100; i < 0x0104; i++){
        read_header -> entry_point[i - 0x0100] = read_header -> raw_header[i];
        printf("0x%X ", read_header -> entry_point[i - 0x0100]);
    }
    printf("\nDone!\n");
    
    printf("Reading Nintendo logo: ");
    for(int i = 0x0104; i < 0x0134; i++){
        read_header -> logo[i - 0x0104] = read_header -> raw_header[i];
        printf("0x%X ", read_header -> logo[i - 0x0104]);
    }
    printf("\nDone!\n");

    printf("Reading Game name: ");
    for(int i = 0x0134; i < 0x0145; i++){
        read_header -> title[i - 0x0134] = read_header -> raw_header[i];
        printf("%c", read_header -> title[i - 0x0134]);
    }
    printf("\nDone!\n");

    read_header -> cgb_flag = read_header -> raw_header[0x0143];
    if(read_header -> cgb_flag == 0x00) printf("Found DMG only mode\n");
    else if(read_header -> cgb_flag == 0x80) printf("Found DMG + compat CGB mode\n");
    else if(read_header -> cgb_flag == 0xC0) printf("Found CGB only mode\n");

    read_header -> sgb_flag = read_header -> raw_header[0x0146];
    printf("Found SGB Flag: 0x%X\n", read_header -> sgb_flag);

    read_header -> cart_type = read_header -> raw_header[0x0147];
    printf("Found Cartridge type: 0x%X\n", read_header -> cart_type);

    read_header -> rom_size = KIB(32) * (1 << read_header -> raw_header[0x0148]);
    printf("[DEBUG] Found ROM value: 0x%x\n", read_header -> raw_header[0x0148]);
    printf("[DEBUG] Found n. ROM banks: %d\n", 1 << read_header -> raw_header[0x0148]);
    printf("[DEBUG] Found ROM size: %dKiB\n", read_header -> rom_size);


    return read_header;
}