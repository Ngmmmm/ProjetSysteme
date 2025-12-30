#include "../include/erraid.h"
#include "../include/utils.h"
#include "../include/response_handlers.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <endian.h>
#include <time.h>


void deserialize_and_print_cmd(int fd, int is_root) {
    char type[3]; 
    if (read(fd, type, 2) != 2) return;
    type[2] = '\0';

    if (strcmp(type, "SI") == 0) {
        uint32_t argc_be;
        read(fd, &argc_be, 4);
        uint32_t argc = be32toh(argc_be);

        for (uint32_t i = 0; i < argc; i++) {
            char *arg = read_string_from_pipe(fd);
            if (arg) {
                print_str(arg); 
                if (i < argc - 1) print_str(" ");
                free(arg);
            }
        }
    } 
    else if (strcmp(type, "SQ") == 0) {
        uint32_t sub_be;
        read(fd, &sub_be, 4);
        uint32_t subcount = be32toh(sub_be);

        //On n'affiche ( que si c pas la racine
        if (!is_root) print_str("( ");
        
        for (uint32_t i = 0; i < subcount; i++) {
            //Les enfants sont jamais la racine donc 0
            deserialize_and_print_cmd(fd, 0); 
            
            if (i < subcount - 1) print_str(" ; ");
        }
        if (!is_root) print_str(" )");
    }
}


void handle_list_reponse(int fd) {
    uint32_t nb_tasks_be; 
    if (read(fd, &nb_tasks_be, 4) != 4) return;
    uint32_t nb_tasks = be32toh(nb_tasks_be);

    if (nb_tasks == 0) return;

    char buf[64];

    for (uint32_t i = 0; i < nb_tasks; i++) {
        uint64_t id_be, min_be;
        uint32_t hour_be;
        uint8_t day;

        read(fd, &id_be, 8); 
        read(fd, &min_be, 8);
        read(fd, &hour_be, 4);
        read(fd, &day, 1);

        snprintf(buf, sizeof(buf), "%lu: ", be64toh(id_be));
        print_str(buf);

        print_timing_complex(be64toh(min_be), be32toh(hour_be), day);
        print_str(" ");

        deserialize_and_print_cmd(fd, 1);
        print_str("\n");
    }
}

void handle_runs_reponse(int fd) {

    uint32_t nb_runs_be;
    if (read(fd, &nb_runs_be, 4) != 4) return;
    uint32_t nb_runs = be32toh(nb_runs_be);

    char buf[128];
    char date_buf[64];

    for (uint32_t i = 0; i < nb_runs; i++) { 
        int64_t time_val_be;
        uint16_t exit_code_be;

        read(fd, &time_val_be, 8);
        read(fd, &exit_code_be, 2);

        time_t timestamp = (time_t)be64toh(time_val_be);
        uint16_t code = be16toh(exit_code_be);

        struct tm *tm_info = localtime(&timestamp);
        strftime(date_buf, sizeof(date_buf), "%Y-%m-%d %H:%M:%S", tm_info);

        snprintf(buf, sizeof(buf), "%s %d", date_buf, code);
        write_line(buf);
    }
}


void handle_output_reponse(int fd) {
    uint32_t len_be;
    // Lecture de la taille (4 octets)
    if (read(fd, &len_be, 4) != 4) return;
    uint32_t len = be32toh(len_be);
    // Copie du contenu brut vers la sortie standard
    char content_buf[1024];
    uint32_t remaining = len;
    while (remaining > 0) {
        int to_read = (remaining > sizeof(content_buf)) ? sizeof(content_buf) : remaining;
        int n = read(fd, content_buf, to_read);
        if (n <= 0) break;
        write(STDOUT_FILENO, content_buf, n);
        remaining -= n;
    }
}
