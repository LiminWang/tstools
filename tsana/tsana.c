//=============================================================================
// Name: tsana.c
// Purpose: analyse certain character with ts file
// To build: gcc -std=c99 -o tsana.exe tsana.c
// Copyright (C) 2009 by ZHOU Cheng. All right reserved.
//=============================================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // for strcmp, etc
#include <stdint.h> // for uint?_t, etc

#include "list.h"
#include "url.h"

#define FALSE                           0
#define TRUE                            1

#define BIT0                            (1 << 0)
#define BIT1                            (1 << 1)
#define BIT2                            (1 << 2)
#define BIT3                            (1 << 3)
#define BIT4                            (1 << 4)
#define BIT5                            (1 << 5)
#define BIT6                            (1 << 6)
#define BIT7                            (1 << 7)

#define TABLE_ID_PAT                    0x00
#define TABLE_ID_CAT                    0x01
#define TABLE_ID_PMT                    0x02

#define MIN_USER_PID                    0x0020
#define MAX_USER_PID                    0x1FFE

//=============================================================================
// const definition:
//=============================================================================
const char *PAT_PID = "PAT_PID";
const char *PMT_PID = "PMT_PID";
const char *SIT_PID = "SIT_PID";
const char *PCR_PID = "PCR_PID";
const char *VID_PID = "VID_PID";
const char *VID_PCR = "VID_PCR"; // video with PCR
const char *AUD_PID = "AUD_PID";
const char *AUD_PCR = "AUD_PCR"; // audio with PCR
const char *NUL_PID = "NUL_PID";
const char *UNO_PID = "UNO_PID";

//=============================================================================
// enum definition:
//=============================================================================
enum
{
        MODE_DEBUG,
        MODE_PSI,
        MODE_PCR,
        MODE_CC
};

enum
{
        STATE_NEXT_PKG_PCR,
        STATE_NEXT_PKG_CC,
        STATE_NEXT_PMT,
        STATE_NEXT_PAT,
        STATE_EXIT
};

//=============================================================================
// struct definition:
//=============================================================================
struct TS
{
        uint32_t sync_byte:8;
        uint32_t transport_error_indicator:1;
        uint32_t payload_unit_start_indicator:1;
        uint32_t transport_priority:1;
        uint32_t PID:13;
        uint32_t transport_scrambling_control:2;
        uint32_t adaption_field_control:2;
        uint32_t continuity_counter:4;
};

struct AF
{
        uint32_t adaption_field_length:8;
        uint32_t discontinuity_indicator:1;
        uint32_t random_access_indicator:1;
        uint32_t elementary_stream_priority_indicator:1;
        uint32_t PCR_flag:1;
        uint32_t OPCR_flag:1;
        uint32_t splicing_pointer_flag:1;
        uint32_t transport_private_data_flag:1;
        uint32_t adaption_field_extension_flag:1;
        uint64_t program_clock_reference_base:33;
        uint32_t reserved:6;
        uint32_t program_clock_reference_extension:9;
};

struct PSI
{
        uint32_t table_id:8; // TABLE_ID_XXX
        uint32_t sectin_syntax_indicator:1;
        uint32_t pad0:1; // '0'
        uint32_t reserved0:2;
        uint32_t section_length:12;
        union
        {
                uint32_t idx:16;
                uint32_t transport_stream_id:16;
                uint32_t program_number:16;
        }idx;
        uint32_t reserved1:2;
        uint32_t version_number:5;
        uint32_t current_next_indicator:1;
        uint32_t section_number:8;
        uint32_t last_section_number:8;

#if 0
        if(ts->PID == PAT_PID)
        {
                loop
                {
                        // program_number:16;
                        // reserved:3;
                        if(program_number == 0x0000)
                        {
                                // network_PID:13;
                        }
                        else
                        {
                                // PMT_PID:13;
                        }
                }
        }
        else if(ts->PID belongs to PMT_PIDs)
        {
                // reserved:3;
                // PCR_PID:13;
                // reserved:4;
                // program_info_length:12;
                {
                        descriptor();
                }
                loop
                {
                        // stream_type:8;
                        // reserved:3;
                        // PMT_PID:13;
                        // reserved:4;
                        // ES_info_length:12
                        {
                                descriptor();
                        }
                }
        }
#endif

        uint32_t CRC3:8; // most significant byte
        uint32_t CRC2:8;
        uint32_t CRC1:8;
        uint32_t CRC0:8; // last significant byte
};

struct PIDS
{
        struct NODE *next;
        struct NODE *prev;

        uint32_t PID:13;
        const char *type; // PID type

        // for check continuity_counter
        uint32_t CC:4;
        uint32_t delta_CC:4; // 0 or 1
        int is_CC_sync;
};

struct TRACK
{
        struct NODE *next;
        struct NODE *prev;

        uint32_t PID:13;
        int stream_type;
        const char *type; // PID type
};

struct PROG
{
        struct NODE *next;
        struct NODE *prev;

        uint32_t program_number:16;
        uint32_t PMT_PID:13;
        uint32_t PCR_PID:13;

        struct LIST *track;
        int parsed;
};

struct OBJ
{
        char file[FILENAME_MAX]; // input file name without postfix

        char file_i[FILENAME_MAX];
        URL *url;
        uint32_t addr; // addr in input file

        char file_o[FILENAME_MAX];
        FILE *fd_o;

        uint32_t ts_size; // 188 or 204
        uint8_t line[204]; // one TS package
        uint8_t *p; // point to current data in line

        struct TS *ts;
        struct AF *af;
        struct PSI *psi;
        uint16_t left_length;

        struct LIST *prog;
        struct LIST *pids;

        int mode;
        int state;
};

//=============================================================================
// Variables definition:
//=============================================================================
struct OBJ *obj = NULL;
struct TS *ts;
struct AF *af;
struct PSI *psi;
struct PIDS *pids;

//=============================================================================
// Sub-function declare:
//=============================================================================
void state_next_pat(struct OBJ *obj);
void state_next_pmt(struct OBJ *obj);
void state_next_pkg_cc(struct OBJ *obj);
void state_next_pkg_pcr(struct OBJ *obj);

struct OBJ *create(int argc, char *argv[]);
int delete(struct OBJ *obj);

void *malloc_mem(int size, char *memo);
char *printb(uint32_t x, int bit_cnt);
void show_help();
void show_version();

void sync_input(struct OBJ *obj);
int get_one_pkg(struct OBJ *obj);
void parse_TS(struct OBJ *obj);
void show_TS(struct OBJ *obj);
void parse_AF(struct OBJ *obj); // adaption_fields
void parse_PSI(struct OBJ *obj);
void show_PSI(struct OBJ *obj);
void parse_PAT_load(struct OBJ *obj);
int is_PMT_PID(struct OBJ *obj);
int is_unparsed_PROG(struct OBJ *obj);
int is_all_PROG_parsed(struct OBJ *obj);
void parse_PMT_load(struct OBJ *obj);
void show_PMT(struct OBJ *obj);
const char *PID_type(uint8_t st);
void pids_add(struct LIST *list, struct PIDS *pids);
struct PIDS *pids_match(struct LIST *list, uint16_t pid);
void show_pids(struct OBJ *obj);

//=============================================================================
// The main function:
//=============================================================================
int main(int argc, char *argv[])
{
        obj = create(argc, argv);
        ts = obj->ts;
        af = obj->af;
        psi = obj->psi;

        sync_input(obj);
        while(STATE_EXIT != obj->state && get_one_pkg(obj))
        {
                parse_TS(obj);
                switch(obj->state)
                {
                        case STATE_NEXT_PAT:
                                state_next_pat(obj);
                                break;
                        case STATE_NEXT_PMT:
                                state_next_pmt(obj);
                                break;
                        case STATE_NEXT_PKG_CC:
                                state_next_pkg_cc(obj);
                                break;
                        case STATE_NEXT_PKG_PCR:
                                state_next_pkg_pcr(obj);
                                break;
                        default:
                                printf("Wrong state!\n");
                                obj->state = STATE_EXIT;
                                break;
                }
                obj->addr += obj->ts_size;
        }

        if(     STATE_NEXT_PAT == obj->state ||
                STATE_NEXT_PMT == obj->state
        )
        {
                printf("PSI parsing unfinished!\n");
                show_pids(obj);
        }
        delete(obj);
        exit(EXIT_SUCCESS);
}

//=============================================================================
// Subfunctions definition:
//=============================================================================
void state_next_pat(struct OBJ *obj)
{
        if(0x0000 == ts->PID)
        {
                parse_PSI(obj);
                parse_PAT_load(obj);
                obj->state = STATE_NEXT_PMT;
        }
}

void state_next_pmt(struct OBJ *obj)
{
        if(!is_PMT_PID(obj))
        {
                return;
        }
        parse_PSI(obj);
        if(TABLE_ID_PMT != psi->table_id)
        {
                // network_PID
                psi->idx.program_number = 0x0000;
        }
        if(is_unparsed_PROG(obj))
        {
                parse_PMT_load(obj);
        }
        if(is_all_PROG_parsed(obj))
        {
                switch(obj->mode)
                {
                        case MODE_PSI:
                                show_pids(obj);
                                obj->state = STATE_EXIT;
                                break;
                        case MODE_CC:
                                sync_input(obj);
                                obj->addr -= obj->ts_size;
                                printf("address(X),address(d),   PID,wait,find,lost\n");
                                obj->state = STATE_NEXT_PKG_CC;
                                break;
                        case MODE_PCR:
                                sync_input(obj);
                                obj->addr -= obj->ts_size;
                                printf("address(X),address(d),   PID,          PCR,  PCR_BASE,PCR_EXT\n");
                                obj->state = STATE_NEXT_PKG_PCR;
                                break;
                        default:
                                obj->state = STATE_EXIT;
                                break;
                }
        }
}

void state_next_pkg_cc(struct OBJ *obj)
{
        pids = pids_match(obj->pids, ts->PID);
        if(NULL == pids)
        {
                if(MIN_USER_PID <= ts->PID && ts->PID <= MAX_USER_PID)
                {
                        printf("0x%08X", obj->addr);
                        printf(",%10u", obj->addr);
                        printf(",0x%04X", ts->PID);
                        printf(", Unknown PID!\n");
                }
                else
                {
                        // reserved PID
                }
        }
        else if(pids->is_CC_sync)
        {
                pids->CC += pids->delta_CC;
                if(pids->CC != ts->continuity_counter)
                {
                        int lost;

                        lost = (int)ts->continuity_counter;
                        lost -= (int)pids->CC;
                        if(lost < 0)
                        {
                                lost += 16;
                        }
                        printf("0x%08X", obj->addr);
                        printf(",%10u", obj->addr);
                        printf(",0x%04X", ts->PID);
                        printf(",  %2u,  %2u,  %2d\n",
                               pids->CC,
                               ts->continuity_counter,
                               lost);
                        pids->CC = ts->continuity_counter;
                }
        }
        else
        {
                pids->CC = ts->continuity_counter;
                pids->is_CC_sync = TRUE;
        }
}

void state_next_pkg_pcr(struct OBJ *obj)
{
        if(     (BIT1 & ts->adaption_field_control) &&
                (0x00 != af->adaption_field_length) &&
                af->PCR_flag
        )
        {
                uint64_t pcr;
                pcr = af->program_clock_reference_base;
                pcr *= 300;
                pcr += af->program_clock_reference_extension;

                printf("0x%08X", obj->addr);
                printf(",%10u", obj->addr);
                printf(",0x%04X", ts->PID);
                printf(",%13llu,%10llu,    %3u\n",
                       pcr,
                       af->program_clock_reference_base,
                       af->program_clock_reference_extension);
        }
}

struct OBJ *create(int argc, char *argv[])
{
        int i;
        int dat;
        struct OBJ *obj;
        struct PIDS *pids;

        obj = (struct OBJ *)malloc_mem(sizeof(struct OBJ), "creat object");

        obj->ts = (struct TS *)malloc_mem(sizeof(struct TS), "creat TS struct");
        obj->af = (struct AF *)malloc_mem(sizeof(struct AF), "creat AF struct");
        obj->psi = (struct PSI *)malloc_mem(sizeof(struct PSI), "creat PSI struct");

        obj->ts_size = 188;
        obj->mode = MODE_PSI;
        strcpy(obj->file_i, "unknown.ts");

        obj->prog = list_init();
        obj->pids = list_init();

        // add PAT PID
        pids = (struct PIDS *)malloc(sizeof(struct PIDS));
        if(NULL == pids)
        {
                printf("Malloc memory failure!\n");
                exit(EXIT_FAILURE);
        }
        pids->PID = 0x0000;
        pids->type = PAT_PID;
        pids->CC = 0;
        pids->delta_CC = 1;
        pids->is_CC_sync = FALSE;
        pids_add(obj->pids, pids);

        // add NUL PID
        pids = (struct PIDS *)malloc(sizeof(struct PIDS));
        if(NULL == pids)
        {
                printf("Malloc memory failure!\n");
                exit(EXIT_FAILURE);
        }
        pids->PID = 0x1FFF;
        pids->type = NUL_PID;
        pids->CC = 0;
        pids->delta_CC = 0;
        pids->is_CC_sync = TRUE;
        pids_add(obj->pids, pids);

        if(1 == argc)
        {
                // no parameter
                printf("tsana: no input files, try --help\n");
                //show_help();
                exit(EXIT_SUCCESS);
        }
        i = 1;
        while (i < argc)
        {
                if ('-' == argv[i][0])
                {
                        if (0 == strcmp(argv[i], "-o"))
                        {
                                strcpy(obj->file_o, argv[++i]);
                        }
                        else if (0 == strcmp(argv[i], "-n"))
                        {
                                sscanf(argv[++i], "%i" , &dat);
                                obj->ts_size = (204 == dat) ? 204 : 108;
                        }
                        else if (0 == strcmp(argv[i], "-psi"))
                        {
                                obj->mode = MODE_PSI;
                        }
                        else if (0 == strcmp(argv[i], "-cc"))
                        {
                                obj->mode = MODE_CC;
                        }
                        else if (0 == strcmp(argv[i], "-pcr"))
                        {
                                obj->mode = MODE_PCR;
                        }
                        else if (0 == strcmp(argv[i], "-debug"))
                        {
                                obj->mode = MODE_DEBUG;
                        }
                        else if (0 == strcmp(argv[i], "-pes"))
                        {
                                sscanf(argv[++i], "%i" , &dat);
                                //obj->pid = (uint16_t)(dat & 0x1FFF);
                        }
                        else if (0 == strcmp(argv[i], "-flt"))
                        {
                                sscanf(argv[++i], "%i" , &dat);
                                //obj->pid = (uint16_t)(dat & 0x1FFF);
                        }
                        else if (0 == strcmp(argv[i], "--help"))
                        {
                                show_help();
                                exit(EXIT_SUCCESS);
                        }
                        else if (0 == strcmp(argv[i], "--version"))
                        {
                                show_version();
                                exit(EXIT_SUCCESS);
                        }
                        else
                        {
                                printf("Wrong parameter: %s\n", argv[i]);
                                exit(EXIT_FAILURE);
                        }
                }
                else
                {
                        strcpy(obj->file_i, argv[i]);
                }
                i++;
        }

        // make output file name with input file name
        if('\0' == obj->file_o[0])
        {
                // search last '.'
                i = strlen(obj->file_i) - 1;
                while(i >= 0)
                {
                        if('.' == obj->file_i[i]) break;
                        i--;
                }
                // fill file_o[] and file[]
                obj->file[i--] = '\0';
                obj->file_o[i--] = '\0';
                while(i >= 0)
                {
                        obj->file[i] = obj->file_i[i];
                        obj->file_o[i] = obj->file_i[i];
                        i--;
                }
                strcat(obj->file_o, "2.ts");
        }

        if(NULL == (obj->url = url_open(obj->file_i, "rb")))
        {
                exit(EXIT_FAILURE);
        }

        obj->state = STATE_NEXT_PAT;

        return obj;
}

int delete(struct OBJ *obj)
{
        if(NULL == obj)
        {
                return 0;
        }
        else
        {
                url_close(obj->url);

                list_free(obj->prog);
                list_free(obj->pids);

                free(obj);

                return 1;
        }
}

void *malloc_mem(int size, char *memo)
{
        void *ptr = NULL;

        ptr = (void *)malloc(size);
        if(NULL == ptr)
        {
                printf("Can not malloc %d-byte memory for %s!\n", size, memo);
                exit(EXIT_FAILURE);
        }
        return ptr;
}

void show_help()
{
        printf("Usage: tsana [options] URL [options]\n");
        printf("URL:\n");
        printf("  0.             [E:\\|\\]path\\filename\n");
        printf("  1.             [file://][E:][/]path/filename\n");
        printf("  2.             udp://@[IP]:port\n");
        printf("Options:\n");
        printf("  -n <num>       Size of TS package, default: 188\n");
        printf("  -o <file>      Output file name, default: *2.ts\n");
        printf("  -psi           Show PSI information\n");
        printf("  -cc            Check Continuity Counter\n");
        printf("  -pcr           Show all PCR value\n");
        printf("  -debug         Show all errors found\n");
        printf("  -flt <pid>     Filter package with <pid>\n");
        printf("  -pes <pid>     Output PES file with <pid>\n");
        printf("  --help         Display this information\n");
        printf("  --version      Display my version\n");
}

void show_version()
{
        printf("tsana 0.1.0 (by MingW), %s %s\n", __TIME__, __DATE__);
        printf("Copyright (C) 2009 ZHOU Cheng.\n");
        printf("This is free software; contact author for additional information.\n");
        printf("There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR\n");
        printf("A PARTICULAR PURPOSE.\n");
}

char *printb(uint32_t x, int bit_cnt)
{
        static char str[64];

        char *p = str;
        uint32_t mask;

        if(bit_cnt < 1 || bit_cnt > 32)
        {
                *p++ = 'e';
                *p++ = 'r';
                *p++ = 'r';
                *p++ = 'o';
                *p++ = 'r';
        }
        else
        {
                mask = (1 << (bit_cnt - 1));
                while(mask)
                {
                        *p++ = (x & mask) ? '1' : '0';
                        mask >>= 1;
                }
        }
        *p = '\0';

        return str;
}

void sync_input(struct OBJ *obj)
{
        int sync_byte;
        URL *url = obj->url;

        obj->addr = 0;
        url_seek(url, 0, SEEK_SET);
        do
        {
                if(EOF == (sync_byte = url_getc(obj->url)))
                {
                        break;
                }
                else if(0x47 == sync_byte)
                {
                        url_seek(url, obj->ts_size - 1, SEEK_CUR);
                        if(EOF == (sync_byte = url_getc(obj->url)))
                        {
                                break;
                        }
                        else if(0x47 == sync_byte)
                        {
                                // sync, go back
                                url_seek(url, -(obj->ts_size + 1), SEEK_CUR);
                                break;
                        }
                        else
                        {
                                // not real sync byte
                                url_seek(url, -(obj->ts_size), SEEK_CUR);
                        }
                }
                else
                {
                        (obj->addr)++;
                }
        }while(1);

        if(0 != obj->addr)
        {
                printf("Find first sync byte at 0x%X in %s.\n",
                       obj->addr, obj->file_i);
        }
}

int get_one_pkg(struct OBJ *obj)
{
        uint32_t size;

        size = url_read(obj->line, 1, obj->ts_size, obj->url);
        obj->p = obj->line;

        return (size == obj->ts_size);
}

void parse_TS(struct OBJ *obj)
{
        uint8_t dat;
        struct TS *ts = obj->ts;

        dat = *(obj->p)++;
        ts->sync_byte = dat;
        if(0x47 != ts->sync_byte)
        {
                printf("Wrong package head at 0x%X!\n", obj->addr);
                exit(EXIT_FAILURE);
        }

        dat = *(obj->p)++;
        ts->transport_error_indicator = (dat & BIT7) >> 7;
        ts->payload_unit_start_indicator = (dat & BIT6) >> 6;
        ts->transport_priority = (dat & BIT5) >> 5;
        ts->PID = dat & 0x1F;

        dat = *(obj->p)++;
        ts->PID <<= 8;
        ts->PID |= dat;

        dat = *(obj->p)++;
        ts->transport_scrambling_control = (dat & (BIT7 | BIT6)) >> 6;
        ts->adaption_field_control = (dat & (BIT5 | BIT4)) >> 4;;
        ts->continuity_counter = dat & 0x0F;

        if(BIT1 & ts->adaption_field_control)
        {
                parse_AF(obj);
        }

        if(BIT0 & ts->adaption_field_control)
        {
                // data_byte
                (obj->p)++;
        }
}

void show_TS(struct OBJ *obj)
{
        struct TS *ts = obj->ts;

        printf("TS:\n");
        printf("  0x%02X  : sync_byte\n", ts->sync_byte);
        printf("  %c     : transport_error_indicator\n", (ts->transport_error_indicator) ? '1' : '0');
        printf("  %c     : payload_unit_start_indicator\n", (ts->payload_unit_start_indicator) ? '1' : '0');
        printf("  %c     : transport_priority\n", (ts->transport_priority) ? '1' : '0');
        printf("  0x%04X: PID\n", ts->PID);
        printf("  0b%s  : transport_scrambling_control\n", printb(ts->transport_scrambling_control, 2));
        printf("  0b%s  : adaption_field_control\n", printb(ts->adaption_field_control, 2));
        printf("  0x%X   : continuity_counter\n", ts->continuity_counter);
}

void parse_AF(struct OBJ *obj)
{
        uint8_t dat;
        struct AF *af = obj->af;

        dat = *(obj->p)++;
        af->adaption_field_length = dat;
        if(0x00 == af->adaption_field_length)
        {
                return;
        }

        dat = *(obj->p)++;
        af->discontinuity_indicator = (dat & BIT7) >> 7;
        af->random_access_indicator = (dat & BIT6) >> 6;
        af->elementary_stream_priority_indicator = (dat & BIT5) >> 5;
        af->PCR_flag = (dat & BIT4) >> 4;
        af->OPCR_flag = (dat & BIT3) >> 3;
        af->splicing_pointer_flag = (dat & BIT2) >> 2;
        af->transport_private_data_flag = (dat & BIT1) >> 1;
        af->adaption_field_extension_flag = (dat & BIT0) >> 0;

        if(af->PCR_flag)
        {
                dat = *(obj->p)++;
                af->program_clock_reference_base = dat;

                dat = *(obj->p)++;
                af->program_clock_reference_base <<= 8;
                af->program_clock_reference_base |= dat;

                dat = *(obj->p)++;
                af->program_clock_reference_base <<= 8;
                af->program_clock_reference_base |= dat;

                dat = *(obj->p)++;
                af->program_clock_reference_base <<= 8;
                af->program_clock_reference_base |= dat;

                dat = *(obj->p)++;
                af->program_clock_reference_base <<= 1;
                af->program_clock_reference_base |= ((dat & BIT7) >> 7);
                af->program_clock_reference_extension = ((dat & BIT0) >> 0);

                dat = *(obj->p)++;
                af->program_clock_reference_extension <<= 8;
                af->program_clock_reference_extension |= dat;
        }
}

void parse_PSI(struct OBJ *obj)
{
        uint8_t dat;
        struct PSI *psi = obj->psi;

        dat = *(obj->p)++;
        psi->table_id = dat;

        dat = *(obj->p)++;
        psi->sectin_syntax_indicator = (dat & BIT7) >> 7;
        psi->section_length = dat & 0x0F;

        dat = *(obj->p)++;
        psi->section_length <<= 8;
        psi->section_length |= dat;
        obj->left_length = psi->section_length;

        dat = *(obj->p)++; obj->left_length--;
        psi->idx.idx = dat;

        dat = *(obj->p)++; obj->left_length--;
        psi->idx.idx <<= 8;
        psi->idx.idx |= dat;

        dat = *(obj->p)++; obj->left_length--;
        psi->version_number = (dat & 0x3E) >> 1;
        psi->current_next_indicator = dat & BIT0;

        dat = *(obj->p)++; obj->left_length--;
        psi->section_number = dat;

        dat = *(obj->p)++; obj->left_length--;
        psi->last_section_number = dat;
}

void show_PSI(struct OBJ *obj)
{
        uint32_t idx;
        struct PSI *psi = obj->psi;

        printf("0x0000: PAT_PID\n");
        //printf("    0x%04X: section_length\n", psi->section_length);
        printf("    0x%04X: transport_stream_id\n", psi->idx.idx);

        idx = 0;
#if 0
        while(idx < psi->program_cnt)
        {
                if(0x0000 == psi->program[idx].program_number)
                {
                        printf("    0x%04X: network_program_number\n",
                               psi->program[idx].program_number);
                        printf("        0x%04X: network_PID\n",
                               psi->program[idx].PID);
                }
                else
                {
                        printf("    0x%04X: program_number\n",
                               psi->program[idx].program_number);
                        printf("        0x%04X: PMT_PID\n",
                               psi->program[idx].PID);
                }
                idx++;
        }
#endif
}

void parse_PAT_load(struct OBJ *obj)
{
        uint8_t dat;
        struct PSI *psi = obj->psi;
        struct PROG *prog;
        struct PIDS *pids;

        while(obj->left_length > 4)
        {
                // add program
                prog = (struct PROG *)malloc(sizeof(struct PROG));
                if(NULL == prog)
                {
                        printf("Malloc memory failure!\n");
                        exit(EXIT_FAILURE);
                }
                prog->track = list_init();
                prog->parsed = FALSE;

                dat = *(obj->p)++; obj->left_length--;
                prog->program_number = dat;

                dat = *(obj->p)++; obj->left_length--;
                prog->program_number <<= 8;
                prog->program_number |= dat;

                dat = *(obj->p)++; obj->left_length--;
                prog->PMT_PID = dat & 0x1F;

                dat = *(obj->p)++; obj->left_length--;
                prog->PMT_PID <<= 8;
                prog->PMT_PID |= dat;

                list_add(obj->prog, (struct NODE *)prog);

                // add PMT PID
                pids = (struct PIDS *)malloc(sizeof(struct PIDS));
                if(NULL == pids)
                {
                        printf("Malloc memory failure!\n");
                        exit(EXIT_FAILURE);
                }
                pids->PID = prog->PMT_PID;
                pids->type = PMT_PID;
                pids->CC = 0;
                pids->delta_CC = 1;
                pids->is_CC_sync = FALSE;
                pids_add(obj->pids, pids);
        }

        dat = *(obj->p)++; obj->left_length--;
        psi->CRC3 = dat;

        dat = *(obj->p)++; obj->left_length--;
        psi->CRC2 = dat;

        dat = *(obj->p)++; obj->left_length--;
        psi->CRC1 = dat;

        dat = *(obj->p)++; obj->left_length--;
        psi->CRC0 = dat;

        if(0 != obj->left_length)
        {
                printf("PSI load length error!\n");
        }

        //printf("PROG Count: %u\n", list_count(obj->prog));
        //printf("PIDS Count: %u\n", list_count(obj->pids));
}

void show_pids(struct OBJ *obj)
{
        struct PIDS *pids;
        struct NODE *node;

        printf("PID LIST(%d-item):\n\n", list_count(obj->pids));
        printf("-PID--, CC, dCC, -type--\n");

        node = obj->pids->head;
        while(node)
        {
                pids = (struct PIDS *)node;
                printf("0x%04X, %2u,  %u , %s\n",
                       pids->PID,
                       pids->CC,
                       pids->delta_CC,
                       pids->type);
                node = node->next;
        }
}

struct PIDS *pids_match(struct LIST *list, uint16_t pid)
{
        struct NODE *node;
        struct PIDS *item;

        node = list->head;
        while(node)
        {
                item = (struct PIDS *)node;
                if(pid == item->PID)
                {
                        return item;
                }
                node = node->next;
        }

        return NULL;
}

void pids_add(struct LIST *list, struct PIDS *pids)
{
        struct NODE *node;
        struct PIDS *item;

        node = list->head;
        while(node)
        {
                item = (struct PIDS *)node;
                if(pids->PID == item->PID)
                {
                        if(PCR_PID == item->type)
                        {
                                if(VID_PID == pids->type)
                                {
                                        item->type = VID_PCR;
                                }
                                else if(AUD_PID == pids->type)
                                {
                                        item->type = AUD_PCR;
                                }
                                else
                                {
                                        // error
                                }
                                item->delta_CC = 1;
                                item->is_CC_sync = FALSE;
                        }
                        else
                        {
                                // same PID, omit...
                        }
                        return;
                }
                node = node->next;
        }

        list_add(list, (struct NODE *)pids);
}

int is_PMT_PID(struct OBJ *obj)
{
        struct TS *ts;
        struct NODE *node;
        struct PIDS *pids;

        ts = obj->ts;
        node = obj->pids->head;
        while(node)
        {
                pids = (struct PIDS *)node;
                if(ts->PID == pids->PID)
                {
                        if(PMT_PID == pids->type)
                        {
                                return TRUE;
                        }
                        else
                        {
                                return FALSE;
                        }
                }
                node = node->next;
        }
        return FALSE;
}

int is_unparsed_PROG(struct OBJ *obj)
{
        struct TS *ts;
        struct PSI *psi;
        struct NODE *node;
        struct PROG *prog;

        ts = obj->ts;
        psi = obj->psi;

        node = obj->prog->head;
        while(node)
        {
                prog = (struct PROG *)node;
                if(psi->idx.program_number == prog->program_number)
                {
                        if(FALSE == prog->parsed)
                        {
                                return TRUE;
                        }
                        else
                        {
                                return FALSE;
                        }
                }
                node = node->next;
        }
        return FALSE;
}

int is_all_PROG_parsed(struct OBJ *obj)
{
        struct TS *ts;
        struct NODE *node;
        struct PROG *prog;

        ts = obj->ts;
        node = obj->prog->head;
        while(node)
        {
                prog = (struct PROG *)node;
                if(     MIN_USER_PID <= prog->PMT_PID &&
                        MAX_USER_PID >= prog->PMT_PID &&
                        FALSE == prog->parsed
                )
                {
                        return FALSE;
                }
                node = node->next;
        }
        return TRUE;
}

const char *PID_type(uint8_t st)
{
        switch(st)
        {
                case 0x1B: // "H.264|ISO/IEC 14496-10 Video"
                case 0x01: // "ISO/IEC 11172 Video"
                case 0x02: // "H.262|ISO/IEC 13818-2 Video"
                        return VID_PID;
                case 0x03: // "ISO/IEC 11172 Audio"
                case 0x04: // "ISO/IEC 13818-3 Audio"
                        return AUD_PID;
                default:
                        return UNO_PID; // unknown
        }
}

void parse_PMT_load(struct OBJ *obj)
{
        uint8_t dat;
        uint16_t info_length;
        struct PSI *psi;
        struct NODE *node;
        struct PROG *prog;
        struct PIDS *pids;
        struct TRACK *track;
        struct TS *ts;

        ts = obj->ts;
        psi = obj->psi;

        node = obj->prog->head;
        while(node)
        {
                prog = (struct PROG *)node;
                if(psi->idx.program_number == prog->program_number)
                {
                        break;
                }
                node = node->next;
        }
        if(!node)
        {
                printf("Wrong PID: 0x%04X\n", ts->PID);
                exit(EXIT_FAILURE);
        }

        prog->parsed = TRUE;
        if(TABLE_ID_PMT != psi->table_id)
        {
                // SIT or other PSI
                return;
        }

        dat = *(obj->p)++; obj->left_length--;
        prog->PCR_PID = dat & 0x1F;

        dat = *(obj->p)++; obj->left_length--;
        prog->PCR_PID <<= 8;
        prog->PCR_PID |= dat;

        // add PCR PID
        pids = (struct PIDS *)malloc(sizeof(struct PIDS));
        if(NULL == pids)
        {
                printf("Malloc memory failure!\n");
                exit(EXIT_FAILURE);
        }
        pids->PID = prog->PCR_PID;
        pids->type = PCR_PID;
        pids->CC = 0;
        pids->delta_CC = 0;
        pids->is_CC_sync = TRUE;
        pids_add(obj->pids, pids);

        // program_info_length
        dat = *(obj->p)++; obj->left_length--;
        info_length = dat & 0x0F;

        dat = *(obj->p)++; obj->left_length--;
        info_length <<= 8;
        info_length |= dat;

        while(info_length-- > 0)
        {
                // omit descriptor here
                dat = *(obj->p)++; obj->left_length--;
        }

        while(obj->left_length > 4)
        {
                // add track
                track = (struct TRACK *)malloc(sizeof(struct TRACK));
                if(NULL == track)
                {
                        printf("Malloc memory failure!\n");
                        exit(EXIT_FAILURE);
                }

                dat = *(obj->p)++; obj->left_length--;
                track->stream_type = dat;

                dat = *(obj->p)++; obj->left_length--;
                track->PID = dat & 0x1F;

                dat = *(obj->p)++; obj->left_length--;
                track->PID <<= 8;
                track->PID |= dat;

                // ES_info_length
                dat = *(obj->p)++; obj->left_length--;
                info_length = dat & 0x0F;

                dat = *(obj->p)++; obj->left_length--;
                info_length <<= 8;
                info_length |= dat;

                while(info_length-- > 0)
                {
                        // omit descriptor here
                        dat = *(obj->p)++; obj->left_length--;
                }

                track->type = PID_type(track->stream_type);
                list_add(prog->track, (struct NODE *)track);

                // add track PID
                pids = (struct PIDS *)malloc(sizeof(struct PIDS));
                if(NULL == pids)
                {
                        printf("Malloc memory failure!\n");
                        exit(EXIT_FAILURE);
                }
                pids->PID = track->PID;
                pids->type = track->type;
                pids->CC = 0;
                pids->delta_CC = 1;
                pids->is_CC_sync = FALSE;
                pids_add(obj->pids, pids);
        }
}

#if 0
void show_PMT(struct OBJ *obj)
{
        uint32_t i;
        uint32_t idx;
        struct PSI *psi = obj->psi;
        struct PMT *pmt;

        for(idx = 0; idx < psi->program_cnt; idx++)
        {
                pmt = obj->pmt[idx];
                if(0x0000 == pmt->program_number)
                {
                        // network information
                        printf("0x%04X: network_PID\n",
                               pmt->PID);
                        //printf("    omit...\n");
                }
                else
                {
                        // normal program
                        printf("0x%04X: PMT_PID\n",
                               pmt->PID);
                        //printf("    0x%04X: section_length\n",
                        //       pmt->section_length);
                        printf("    0x%04X: program_number\n",
                               pmt->program_number);
                        printf("    0x%04X: PCR_PID\n",
                               pmt->PCR_PID);
                        //printf("    0x%04X: program_info_length\n",
                        //       pmt->program_info_length);
                        if(0 != pmt->program_info_length)
                        {
                                //printf("        omit...\n");
                        }

                        i = 0;
                        while(i < pmt->pid_cnt)
                        {
                                printf("    0x%04X: %s\n",
                                       pmt->pid[i].PID,
                                       PID_TYPE_STR[stream_type(pmt->pid[i].stream_type)]);
                                printf("        0x%04X: %s\n",
                                       pmt->pid[i].stream_type,
                                       stream_type_str(pmt->pid[i].stream_type));
                                //printf("        0x%04X: ES_info_length\n",
                                //       pmt->pid[i].ES_info_length);
                                if(0 != pmt->pid[i].ES_info_length)
                                {
                                        //printf("            omit...\n");
                                }

                                i++;
                        }
                }
        }
}
#endif
//=============================================================================
// THE END.
//=============================================================================