#include "include/cart.h"

FILE* cartridge_pointer;

header read_header(){
    header read_header = (header)malloc(sizeof(struct header));
    if(read_header == NULL){
        perror("Error allocating header!");
        exit(EXIT_FAILURE);
    }

    
}