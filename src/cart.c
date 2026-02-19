#include "include/cart.h"

FILE* cartridge_pointer;

int ram_banks[] = {0, 0, 1, 4, 16, 8};

cartridge read_cart(const char* filepath){
    cartridge_pointer = (FILE*)fopen(filepath, "rb");
    if(cartridge_pointer == NULL){
        perror("[ERROR] Error reading cart file!");
        exit(EXIT_FAILURE);
    }

    cartridge read_cart = (cartridge)malloc(sizeof(struct cartridge));
    if(read_cart == NULL){
        perror("[ERROR] Error allocating cartridge!");
        exit(EXIT_FAILURE);
    }

    read_cart -> head = read_header();

    fseek(cartridge_pointer, 0L, SEEK_END);
    size_t file_size = ftell(cartridge_pointer);
    printf("[INFO] File size: %zu\n", file_size);
    rewind(cartridge_pointer);

    read_cart -> raw_cart = (uint8_t*)malloc(file_size * sizeof(uint8_t));
    if(read_cart -> raw_cart == NULL) {
        perror("[ERROR] Error allocating raw cart!");
        exit(EXIT_FAILURE);
    }

    size_t read_bytes = fread(read_cart -> raw_cart, 1, file_size, cartridge_pointer);
    if(read_bytes != file_size){
        perror("[ERROR] ROM truncated or corrupted!");
        printf("{DEBUG} Read %zu, instead of %zu\n", read_bytes, file_size);
        exit(EXIT_FAILURE);
    }

    

    return read_cart;
}

header read_header(){
    header read_header = (header)malloc(sizeof(struct header));
    if(read_header == NULL){
        perror("[ERROR] Error allocating header!");
        exit(EXIT_FAILURE);
    }

    size_t read_bytes = fread(read_header -> raw_header, 1, 0x0150, cartridge_pointer);
    printf("[INFO] Bytes read from header = 0x%zx\n", read_bytes);
    if(read_bytes < 0x0150){
        perror("[ERROR] Invalid cartridge header!");
        exit(EXIT_FAILURE);
    }

    printf("[INFO] Reading Entry Point -> ");
    for(int i = 0x0100; i < 0x0104; i++){
        read_header -> entry_point[i - 0x0100] = read_header -> raw_header[i];
    }
    printf("Done!\n");
    
    printf("[INFO] Reading Nintendo logo -> ");
    for(int i = 0x0104; i < 0x0134; i++){
        read_header -> logo[i - 0x0104] = read_header -> raw_header[i];
    }
    printf("Done!\n");

    printf("[INFO] Reading Game name: ");
    for(int i = 0x0134; i < 0x0145; i++){
        read_header -> title[i - 0x0134] = read_header -> raw_header[i];
        printf("%c", read_header -> title[i - 0x0134]);
    }
    printf("\n");

    read_header -> cgb_flag = read_header -> raw_header[0x0143];
    if(read_header -> cgb_flag == 0x00) printf("[INFO] Found DMG only mode\n");
    else if(read_header -> cgb_flag == 0x80) printf("[INFO] Found DMG + compat CGB mode\n");
    else if(read_header -> cgb_flag == 0xC0) printf("[INFO] Found CGB only mode\n");

    read_header -> sgb_flag = read_header -> raw_header[0x0146];
    printf("[INFO] Found SGB Flag: 0x%X\n", read_header -> sgb_flag);

    read_header -> cart_type = read_header -> raw_header[0x0147];
    printf("[INFO] Found Cartridge type: 0x%X\n", read_header -> cart_type);

    read_header -> rom_size = 32 * (1 << read_header -> raw_header[0x0148]);
    printf("[INFO] Found ROM size: %dKiB\n", read_header -> rom_size);

    if(read_header -> raw_header[0x0149] == 0) printf("[INFO] No RAM\n");
    else if(read_header -> raw_header[0x0149] == 1) printf("[INFO] RAM is unused\n");
    else {
        read_header -> ram_size = 8 * (ram_banks[read_header -> raw_header[0x0149]]);
        printf("[INFO] Found RAM size: %dKiB\n", read_header -> ram_size);
    }

    uint8_t checksum = 0;
    for (uint16_t address = 0x0134; address <= 0x014C; address++) {
        checksum = checksum - read_header -> raw_header[address] - 1;
    }

    checksum = checksum & 0xFF;

    if(checksum == read_header -> raw_header[0x014D]) {
        printf("[INFO] Checksum valid! Starting emulation...\n");
    } else {
        printf("[INFO] Checksum mismatch! Emulation will not start!\n");
        exit(EXIT_FAILURE);
    }

    return read_header;
}