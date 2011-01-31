/* vim: set tabstop=8 shiftwidth=8: */
//=============================================================================
// Name: tsana.c
// Purpose: analyse character of ts stream
// To build: gcc -std=c99 -o tsana.exe tsana.c
// Copyright (C) 2009 by ZHOU Cheng. All right reserved.
//=============================================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // for strcmp, etc
#include <time.h> // for localtime(), etc
#include <stdint.h> // for uint?_t, etc

#include "error.h"
#include "if.h"
#include "ts.h" // has "list.h" already

#define PKT_BBUF                        (256) // 188 or 204
#define PKT_TBUF                        (PKT_BBUF * 3 + 10)

#define ANY_PID                         0x2000 // any PID of [0x0000,0x1FFF]
#define ANY_PROG                        0x0000 // any prog of [0x0001,0xFFFF]

#define PCR_US                          (27) // 27 clk means 1(us)
#define PCR_MS                          (27 * 1000) // uint: do NOT use 1e3 

//=============================================================================
// struct definition
//=============================================================================
typedef struct
{
        int mode;
        int state;

        int is_outpsi; // output txt psi packet to stdout
        int is_prepsi; // get psi information from file first
        int is_mono; // use colour when print
        uint16_t aim_pid;
        uint16_t aim_prog;
        uint64_t aim_interval; // for rate calc

        uint32_t ts_size;
        uint8_t bbuf[PKT_BBUF];
        char tbuf[PKT_TBUF];

        int ts_id;
        ts_rslt_t *rslt;
}
obj_t;

//=============================================================================
// enum definition
//=============================================================================
enum
{
        MODE_PID,
        MODE_PSI,
        MODE_PCR,
        MODE_PTSDTS,
        MODE_SYS_RATE,
        MODE_PSI_RATE,
        MODE_PROG_RATE,
        MODE_PES,
        MODE_ES,
        MODE_ERROR,
        MODE_EXIT
};

enum
{
        STATE_PARSE_EACH,
        STATE_PARSE_PSI,
        STATE_EXIT
};

enum
{
        GOT_WRONG_PKT,
        GOT_RIGHT_PKT,
        GOT_EOF
};

//=============================================================================
// variable definition
//=============================================================================
static obj_t *obj = NULL;

//=============================================================================
// sub-function declaration
//=============================================================================
static void state_parse_psi(obj_t *obj);
static void state_parse_each(obj_t *obj);

static obj_t *create(int argc, char *argv[]);
static int delete(obj_t *obj);

static void show_help();
static void show_version();

static int get_one_pkt(obj_t *obj);

static void show_pid_list(obj_t *obj);
static void show_prog(LIST *list);
static void show_track(LIST *list);

static void show_pcr(obj_t *obj);
static void show_sys_rate(obj_t *obj);
static void show_psi_rate(obj_t *obj);
static void show_prog_rate(obj_t *obj);
static void show_ptsdts(obj_t *obj);
static void show_pes(obj_t *obj);
static void show_es(obj_t *obj);
static void show_error(obj_t *obj);

static void print_atp_title(obj_t *obj); // atp: address_time_PID
static void print_atp_value(obj_t *obj); // atp: address_time_PID

//=============================================================================
// the main function
//=============================================================================
int main(int argc, char *argv[])
{
        int get_rslt;
        ts_rslt_t *rslt;

        obj = create(argc, argv);
        rslt = obj->rslt;
        rslt->aim_interval = obj->aim_interval;

        while(STATE_EXIT != obj->state && GOT_EOF != (get_rslt = get_one_pkt(obj)))
        {
                if(GOT_WRONG_PKT == get_rslt)
                {
                        break;
                }
                if(0 != tsParseTS(obj->ts_id, obj->bbuf, obj->ts_size))
                {
                        break;
                }
                tsParseOther(obj->ts_id);
                switch(obj->state)
                {
                        case STATE_PARSE_PSI:
                                state_parse_psi(obj);
                                break;
                        case STATE_PARSE_EACH:
                                state_parse_each(obj);
                                break;
                        case STATE_EXIT:
                                break;
                        default:
                                fprintf(stderr, "Wrong state(%d)!\n", obj->state);
                                obj->state = STATE_EXIT;
                                break;
                }
        }

        if(STATE_PARSE_PSI == obj->state)
        {
                fprintf(stderr, "PSI parsing unfinished!\n");
                //show_pids(rslt->pid_list);
                show_prog(&(rslt->prog_list));
        }

        delete(obj);
        exit(EXIT_SUCCESS);
}

//=============================================================================
// sub-function definition
//=============================================================================
static void state_parse_psi(obj_t *obj)
{
        ts_rslt_t *rslt = obj->rslt;

        if(obj->is_outpsi)
        {
                puts(obj->tbuf);
        }

        if(rslt->is_psi_parsed)
        {
                switch(obj->mode)
                {
                        case MODE_PSI:
                                show_prog(&(obj->rslt->prog_list));
                                obj->state = STATE_EXIT;
                                break;
                        case MODE_PCR:
                                print_atp_title(obj);
                                fprintf(stdout, "PCR, BASE, EXT, interval(ms), jitter(us), \n");
                                obj->state = STATE_PARSE_EACH;
                                break;
                        case MODE_PTSDTS:
                                print_atp_title(obj);
                                fprintf(stdout, "PTS, PTS_interval(ms), PTS-PCR(ms), DTS, DTS_interval(ms), DTS-PCR(ms), \n");
                                obj->state = STATE_PARSE_EACH;
                                break;
                        case MODE_ERROR:
                                print_atp_title(obj);
                                fprintf(stdout, "level, detail, \n");
                                obj->state = STATE_PARSE_EACH;
                                break;
                        case MODE_PID:
                        case MODE_SYS_RATE:
                        case MODE_PSI_RATE:
                        case MODE_PROG_RATE:
                        case MODE_PES:
                        case MODE_ES:
                                obj->state = STATE_PARSE_EACH;
                                break;
                        case MODE_EXIT:
                        default:
                                obj->state = STATE_EXIT;
                                break;
                }
        }
        return;
}

static void state_parse_each(obj_t *obj)
{
        switch(obj->mode)
        {
                case MODE_PID:
                        show_pid_list(obj);
                        break;
                case MODE_PCR:
                        show_pcr(obj);
                        break;
                case MODE_SYS_RATE:
                        show_sys_rate(obj);
                        break;
                case MODE_PSI_RATE:
                        show_psi_rate(obj);
                        break;
                case MODE_PROG_RATE:
                        show_prog_rate(obj);
                        break;
                case MODE_PTSDTS:
                        show_ptsdts(obj);
                        break;
                case MODE_PES:
                        show_pes(obj);
                        break;
                case MODE_ES:
                        show_es(obj);
                        break;
                case MODE_ERROR:
                        show_error(obj);
                        break;
                default:
                        fprintf(stderr, "wrong mode(%d)!\n", obj->mode);
                        break;
        }
        return;
}

static obj_t *create(int argc, char *argv[])
{
        int i;
        int dat;
        obj_t *obj;

        obj = (obj_t *)malloc(sizeof(obj_t));
        if(NULL == obj)
        {
                DBG(ERR_MALLOC_FAILED);
                return NULL;
        }

        obj->mode = MODE_PID;
        obj->state = STATE_PARSE_PSI;
        obj->is_outpsi = 0;
        obj->is_prepsi = 0;
        obj->is_mono = 0;
        obj->aim_pid = ANY_PID;
        obj->aim_prog = ANY_PROG;
        obj->aim_interval = 1000 * PCR_MS;

        for(i = 1; i < argc; i++)
        {
                if('-' == argv[i][0])
                {
                        if(0 == strcmp(argv[i], "-pid-list"))
                        {
                                obj->mode = MODE_PID;
                        }
                        else if(0 == strcmp(argv[i], "-psi-tree"))
                        {
                                obj->mode = MODE_PSI;
                        }
                        else if(0 == strcmp(argv[i], "-outpsi"))
                        {
                                obj->is_outpsi = 1;
                                obj->mode = MODE_EXIT;
                        }
                        else if(0 == strcmp(argv[i], "-prepsi"))
                        {
                                obj->is_prepsi = 1;
                        }
                        else if(0 == strcmp(argv[i], "-mono"))
                        {
                                obj->is_mono = 1;
                        }
                        else if(0 == strcmp(argv[i], "-pcr"))
                        {
                                obj->mode = MODE_PCR;
                        }
                        else if(0 == strcmp(argv[i], "-sys-rate"))
                        {
                                obj->mode = MODE_SYS_RATE;
                        }
                        else if(0 == strcmp(argv[i], "-psi-rate"))
                        {
                                obj->mode = MODE_PSI_RATE;
                        }
                        else if(0 == strcmp(argv[i], "-prog-rate"))
                        {
                                obj->mode = MODE_PROG_RATE;
                        }
                        else if(0 == strcmp(argv[i], "-err"))
                        {
                                obj->mode = MODE_ERROR;
                        }
                        else if(0 == strcmp(argv[i], "-pid"))
                        {
                                i++;
                                if(i >= argc)
                                {
                                        fprintf(stderr, "no parameter for '-pid'!\n");
                                        exit(EXIT_FAILURE);
                                }
                                sscanf(argv[i], "%i" , &dat);
                                if(0x0000 <= dat && dat <= 0x1FFF)
                                {
                                        obj->aim_pid = (uint16_t)dat;
                                }
                                else
                                {
                                        fprintf(stderr,
                                                "bad variable for '-pid': 0x%04X, ignore!\n",
                                                dat);
                                }
                        }
                        else if(0 == strcmp(argv[i], "-prog"))
                        {
                                i++;
                                if(i >= argc)
                                {
                                        fprintf(stderr, "no parameter for '-prog'!\n");
                                        exit(EXIT_FAILURE);
                                }
                                sscanf(argv[i], "%i" , &dat);
                                if(0x0001 <= dat && dat <= 0xFFFF)
                                {
                                        obj->aim_prog = dat;
                                }
                                else
                                {
                                        fprintf(stderr,
                                                "bad variable for '-prog': %u, ignore!\n",
                                                dat);
                                }
                        }
                        else if(0 == strcmp(argv[i], "-interval"))
                        {
                                i++;
                                if(i >= argc)
                                {
                                        fprintf(stderr, "no parameter for '-interval'!\n");
                                        exit(EXIT_FAILURE);
                                }
                                sscanf(argv[i], "%i" , &dat);
                                if(1 <= dat && dat <= 10000) // 1ms ~ 10s
                                {
                                        obj->aim_interval = dat * PCR_MS;
                                }
                                else
                                {
                                        fprintf(stderr,
                                                "bad variable for '-interval': %u, use 1000ms instead!\n",
                                                dat);
                                }
                        }
                        else if(0 == strcmp(argv[i], "-pes"))
                        {
                                obj->mode = MODE_PES;
                        }
                        else if(0 == strcmp(argv[i], "-es"))
                        {
                                obj->mode = MODE_ES;
                        }
                        else if(0 == strcmp(argv[i], "-ptsdts"))
                        {
                                obj->mode = MODE_PTSDTS;
                        }
                        else if(0 == strcmp(argv[i], "-h") ||
                                0 == strcmp(argv[i], "--help")
                        )
                        {
                                show_help();
                                exit(EXIT_SUCCESS);
                        }
                        else if(0 == strcmp(argv[i], "-v") ||
                                0 == strcmp(argv[i], "--version")
                        )
                        {
                                show_version();
                                exit(EXIT_SUCCESS);
                        }
                        else
                        {
                                fprintf(stderr, "wrong parameter: '%s'\n", argv[i]);
                                exit(EXIT_FAILURE);
                        }
                }
                else
                {
                        fprintf(stderr, "Wrong parameter: %s\n", argv[i]);
                        exit(EXIT_FAILURE);
                }
        }

        obj->ts_id = tsCreate(&(obj->rslt));

        return obj;
}

static int delete(obj_t *obj)
{
        if(NULL == obj)
        {
                return 0;
        }
        else
        {
                tsDelete(obj->ts_id);
                free(obj);

                return 1;
        }
}

static void show_help()
{
        puts("'tsana' get TS packet from stdin, analyse, then send the result to stdout.");
        puts("");
        puts("Usage: tsana [OPTION]...");
        puts("");
        puts("Options:");
        puts(" -pid-list        show PID list information, default option");
        puts(" -psi-tree        show PSI tree information");
        puts(" -outpsi          output PSI packet");
        puts(" -pid <pid>       set cared <pid>, default: ANY PID");
        puts(" -prog <prog>     set cared <prog>, default: ANY program");
        puts(" -interval <iv>   set cared <iv>(ms) for bit-rate calculate, default: 1000");
        puts(" -pcr             output PCR information of cared <pid>");
        puts(" -sys-rate        output system bit-rate");
        puts(" -psi-rate        output psi/si bit-rate");
        puts(" -prog-rate       output bit-rate of cared <prog>");
        puts(" -ptsdts          output PTS and DTS information of cared <pid>");
        puts(" -pes             output PES data of cared <pid>");
        puts(" -es              output ES data of cared <pid>");
        puts(" -err             output all errors found");
        puts(" -mono            disable colour effect, default: use colour to help read");
#if 0
        puts(" -prepsi <file>   get PSI information from <file> first");
#endif
        puts("");
        puts(" -h, --help       display this information");
        puts(" -v, --version    display my version");
        puts("");
        puts("Examples:");
        puts("  \"bincat xxx.ts | tsana -err\" -- report all error information");
        puts("");
        puts("Report bugs to <zhoucheng@tsinghua.org.cn>.");
        return;
}

static void show_version()
{
        puts("tsana 1.0.0");
        puts("");
        puts("Copyright (C) 2009,2010 ZHOU Cheng.");
        puts("This is free software; contact author for additional information.");
        puts("There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR");
        puts("A PARTICULAR PURPOSE.");
        puts("");
        puts("Written by ZHOU Cheng.");
        return;
}

static int get_one_pkt(obj_t *obj)
{
        char *rslt;
        int size;

        rslt = fgets(obj->tbuf, PKT_TBUF, stdin);
        if(NULL == rslt)
        {
                return GOT_EOF;
        }
        //puts(obj->tbuf);

        size = t2b(obj->bbuf, obj->tbuf);
        if((size != 188) && (size != 204))
        {
                fprintf(stderr, "Bad packet size:%d\n%s\n", size, obj->tbuf);
                return GOT_WRONG_PKT;
        }

        obj->ts_size = size;
        return GOT_RIGHT_PKT;
}

static void show_pid_list(obj_t *obj)
{
        NODE *node;
        ts_pid_t *pids;
        ts_rslt_t *rslt = obj->rslt;
        LIST *list = &(rslt->pid_list);

        if(!(rslt->has_rate))
        {
                return;
        }

        fprintf(stdout, FYELLOW " PID  " NONE ", percent, count, dCC, track," FYELLOW "     abbr" NONE ", detail\n");

        for(node = list->head; node; node = node->next)
        {
                pids = (ts_pid_t *)node;
                fprintf(stdout, FYELLOW "0x%04X" NONE ", %7.4f, %5u,  %u ,     %c," FYELLOW " %s" NONE ", %s\n",
                        pids->PID,
                        pids->lcnt * 100.0 / rslt->last_sys_cnt,
                        pids->lcnt,
                        pids->dCC,
                        (pids->track) ? '*' : ' ',
                        pids->sdes,
                        pids->ldes);
        }
        obj->state = STATE_EXIT;

        return;
}

static void show_prog(LIST *list)
{
        uint16_t i;
        NODE *node;
        ts_prog_t *prog;

        for(node = list->head; node; node = node->next)
        {
                prog = (ts_prog_t *)node;
                fprintf(stdout, "+ program " FYELLOW "%d" NONE "(0x%04X"
                        "), PMT_PID = " FYELLOW "0x%04X" NONE "\n",
                        prog->program_number,
                        prog->program_number,
                        prog->PMT_PID);
                fprintf(stdout, "    PCR_PID = " FYELLOW "0x%04X" NONE "\n",
                        prog->PCR_PID);
                fprintf(stdout, "    program_info:" FYELLOW);
                for(i = 0; i < prog->program_info_len; i++)
                {
                        if(0x00 == (i & 0x0F))
                        {
                                fprintf(stdout, "\n");
                                fprintf(stdout, "        ");
                        }
                        fprintf(stdout, "%02X ", prog->program_info[i]);
                }
                fprintf(stdout, NONE "\n");
                show_track(&(prog->track_list));
        }
        return;
}

static void show_track(LIST *list)
{
        uint16_t i;
        NODE *node;
        ts_track_t *track;

        //fprintf(stdout, "track_list(%d-item):\n\n", list_count(list));

        for(node = list->head; node; node = node->next)
        {
                track = (ts_track_t *)node;
                fprintf(stdout, "    track\n");
                fprintf(stdout, "        stream_type = " FYELLOW "0x%02X" NONE ", %s, %s\n",
                        track->stream_type,
                        track->sdes,
                        track->ldes);
                fprintf(stdout, "        elementary_PID = " FYELLOW "0x%04X" NONE "\n",
                        track->PID);
                fprintf(stdout, "        ES_info:" FYELLOW);
                for(i = 0; i < track->es_info_len; i++)
                {
                        if(0x00 == (i & 0x0F))
                        {
                                fprintf(stdout, "\n");
                                fprintf(stdout, "            ");
                        }
                        fprintf(stdout, "%02X ", track->es_info[i]);
                }
                fprintf(stdout, NONE "\n");
        }
        return;
}

static void show_pcr(obj_t *obj)
{
        ts_rslt_t *rslt = obj->rslt;

        if(ANY_PID != obj->aim_pid && rslt->pid != obj->aim_pid)
        {
                return;
        }
        if(!(rslt->has_PCR))
        {
                return;
        }

        print_atp_value(obj);
        fprintf(stdout, "%llu, %llu, %3u, %+7.3f, %+6.3f \n",
                rslt->PCR,
                rslt->PCR_base,
                rslt->PCR_ext,
                (double)(rslt->PCR_interval) / PCR_MS,
                (double)(rslt->PCR_jitter) / PCR_US);
        return;
}

static void show_sys_rate(obj_t *obj)
{
        ts_rslt_t *rslt = obj->rslt;
        NODE *node;
        ts_pid_t *pid_item;

        if(!(rslt->has_rate))
        {
                return;
        }

        if(obj->is_mono)
        {
                fprintf(stdout,
                        "0x%llX"
                        ", "
                        "0x%04X"
                        ", ",
                        rslt->addr,
                        rslt->pid);
        }
        else
        {
                fprintf(stdout,
                        FYELLOW "0x%llX" NONE
                        ", "
                        FYELLOW "0x%04X" NONE
                        ", ",
                        rslt->addr,
                        rslt->pid);
        }

        if(obj->is_mono)
        {
                fprintf(stdout,
                        "sys" ", %9.6f, "
                        "psi-si" ", %9.6f, "
                        "empty" ", %9.6f, ",
                        rslt->last_sys_cnt * 188.0 * 8 * 27 / (rslt->last_interval),
                        rslt->last_psi_cnt * 188.0 * 8 * 27 / (rslt->last_interval),
                        rslt->last_nul_cnt * 188.0 * 8 * 27 / (rslt->last_interval));
        }
        else
        {
                fprintf(stdout,
                        FYELLOW "sys" NONE ", %9.6f, "
                        FYELLOW "psi-si" NONE ", %9.6f, "
                        FYELLOW "empty" NONE ", %9.6f, ",
                        rslt->last_sys_cnt * 188.0 * 8 * 27 / (rslt->last_interval),
                        rslt->last_psi_cnt * 188.0 * 8 * 27 / (rslt->last_interval),
                        rslt->last_nul_cnt * 188.0 * 8 * 27 / (rslt->last_interval));
        }
#if 1
        // traverse pid_list
        // if it belongs to this program, output its bitrate
        for(node = rslt->pid_list.head; node; node = node->next)
        {
                pid_item = (ts_pid_t *)node;
                if(ANY_PROG == obj->aim_prog ||
                   (pid_item->prog && (pid_item->prog->program_number == obj->aim_prog)))
                {
#if 0
                        if(obj->is_mono)
                        {
                                fprintf(stdout, "0x%04X" ", %9.6f, ",
                                        pid_item->PID,
                                        pid_item->lcnt * 188.0 * 8 * 27 / (rslt->last_interval));
                        }
                        else
                        {
                                fprintf(stdout, FYELLOW "0x%04X" NONE ", %9.6f, ",
                                        pid_item->PID,
                                        pid_item->lcnt * 188.0 * 8 * 27 / (rslt->last_interval));
                        }
#endif
                }
        }
#endif
        fprintf(stdout, "\n");
        return;
}

static void show_psi_rate(obj_t *obj)
{
        ts_rslt_t *rslt = obj->rslt;
        NODE *node;
        ts_pid_t *pid_item;

        if(!(rslt->has_rate))
        {
                return;
        }

        if(obj->is_mono)
        {
                fprintf(stdout,
                        "0x%llX"
                        ", "
                        "0x%04X"
                        ", ",
                        rslt->addr,
                        rslt->pid);
        }
        else
        {
                fprintf(stdout,
                        FYELLOW "0x%llX" NONE
                        ", "
                        FYELLOW "0x%04X" NONE
                        ", ",
                        rslt->addr,
                        rslt->pid);
        }
        // traverse pid_list
        // if it belongs to this program, output its bitrate
        for(node = rslt->pid_list.head; node; node = node->next)
        {
                pid_item = (ts_pid_t *)node;
                if(ANY_PROG == obj->aim_prog ||
                   (pid_item->prog && (pid_item->prog->program_number == obj->aim_prog)))
                {
                        if(obj->is_mono)
                        {
                                fprintf(stdout, "0x%04X" ", %9.6f, ",
                                        pid_item->PID,
                                        pid_item->lcnt * 188.0 * 8 * 27 / (rslt->last_interval));
                        }
                        else
                        {
                                fprintf(stdout, FYELLOW "0x%04X" NONE ", %9.6f, ",
                                        pid_item->PID,
                                        pid_item->lcnt * 188.0 * 8 * 27 / (rslt->last_interval));
                        }
                }
        }
        fprintf(stdout, "\n");
        return;
}

static void show_prog_rate(obj_t *obj)
{
        ts_rslt_t *rslt = obj->rslt;
        NODE *node;
        ts_pid_t *pid_item;

        if(!(rslt->has_rate))
        {
                return;
        }

        if(obj->is_mono)
        {
                fprintf(stdout,
                        "0x%llX"
                        ", "
                        "0x%04X"
                        ", ",
                        rslt->addr,
                        rslt->pid);
        }
        else
        {
                fprintf(stdout,
                        FYELLOW "0x%llX" NONE
                        ", "
                        FYELLOW "0x%04X" NONE
                        ", ",
                        rslt->addr,
                        rslt->pid);
        }
        // traverse pid_list
        // if it belongs to this program, output its bitrate
        for(node = rslt->pid_list.head; node; node = node->next)
        {
                pid_item = (ts_pid_t *)node;
                if(ANY_PROG == obj->aim_prog ||
                   (pid_item->prog && (pid_item->prog->program_number == obj->aim_prog)))
                {
                        if(obj->is_mono)
                        {
                                fprintf(stdout, "0x%04X" ", %9.6f, ",
                                        pid_item->PID,
                                        pid_item->lcnt * 188.0 * 8 * 27 / (rslt->last_interval));
                        }
                        else
                        {
                                fprintf(stdout, FYELLOW "0x%04X" NONE ", %9.6f, ",
                                        pid_item->PID,
                                        pid_item->lcnt * 188.0 * 8 * 27 / (rslt->last_interval));
                        }
                }
        }
        fprintf(stdout, "\n");
        return;
}

static void show_ptsdts(obj_t *obj)
{
        ts_rslt_t *rslt = obj->rslt;

        if(ANY_PID != obj->aim_pid && rslt->pid != obj->aim_pid)
        {
                return;
        }
        if(!(rslt->has_PTS))
        {
                return;
        }

        print_atp_value(obj);
        fprintf(stdout, "%llu, %+8.3f, %+.3f, ",
                rslt->PTS,
                (double)(rslt->PTS_interval) / (90), // ms
                (double)(rslt->PTS_minus_STC) / (90)); // ms

        if(rslt->has_DTS)
        {
                fprintf(stdout, "%llu, %+8.3f, %+.3f, \n",
                        rslt->DTS,
                        (double)(rslt->DTS_interval) / (90), // ms
                        (double)(rslt->DTS_minus_STC) / (90)); // ms
        }
        else
        {
                fprintf(stdout, ", , , \n");
        }
        return;
}

static void show_pes(obj_t *obj)
{
        ts_rslt_t *rslt = obj->rslt;

        if(ANY_PID != obj->aim_pid && rslt->pid != obj->aim_pid)
        {
                return;
        }
        if(0 != rslt->PES_len)
        {
                b2t(obj->tbuf, rslt->PES_buf, rslt->PES_len, ' ');
                puts(obj->tbuf);
        }
        return;
}

static void show_es(obj_t *obj)
{
        ts_rslt_t *rslt = obj->rslt;

        if(ANY_PID != obj->aim_pid && rslt->pid != obj->aim_pid)
        {
                return;
        }
        if(0 != rslt->ES_len)
        {
                b2t(obj->tbuf, rslt->ES_buf, rslt->ES_len, ' ');
                puts(obj->tbuf);
        }
        return;
}

static void show_error(obj_t *obj)
{
        ts_rslt_t *rslt = obj->rslt;
        ts_error_t *err = &(rslt->err);

        if(ANY_PID != obj->aim_pid && rslt->pid != obj->aim_pid)
        {
                return;
        }

        // First priority: necessary for de-codability (basic monitoring)
        if(err->TS_sync_loss)
        {
                print_atp_value(obj);
                fprintf(stdout, "TR-101-290_1.1, TS_sync_loss\n");
                if(err->Sync_byte_error > 10)
                {
                        fprintf(stdout, "\nToo many continual Sync_byte_error packet, EXIT!\n");
                        exit(EXIT_FAILURE);
                }
                return;
        }
        if(err->Sync_byte_error == 1)
        {
                print_atp_value(obj);
                fprintf(stdout, "TR-101-290_1.2, Sync_byte_error\n");
        }
        if(err->PAT_error)
        {
                print_atp_value(obj);
                fprintf(stdout, "TR-101-290_1.3, PAT_error\n");
                err->PAT_error = 0;
        }
        if(err->Continuity_count_error)
        {
                print_atp_value(obj);
                fprintf(stdout, "TR-101-290_1.4, Continuity_count_error(%X-%X=%2u)\n",
                        rslt->CC_find, rslt->CC_wait, rslt->CC_lost);
        }
        if(err->PMT_error)
        {
                print_atp_value(obj);
                fprintf(stdout, "TR-101-290_1.5, PMT_error\n");
                err->PMT_error = 0;
        }
        if(err->PID_error)
        {
                print_atp_value(obj);
                fprintf(stdout, "TR-101-290_1.6, PID_error\n");
                err->PID_error = 0;
        }

        // Second priority: recommended for continuous or periodic monitoring
        if(err->Transport_error)
        {
                print_atp_value(obj);
                fprintf(stdout, "TR-101-290_2.1, Transport_error\n");
        }
        if(err->CRC_error)
        {
                print_atp_value(obj);
                fprintf(stdout, "TR-101-290_2.2, CRC_error\n");
                err->CRC_error = 0;
        }
        if(err->PCR_error)
        {
                print_atp_value(obj);
                fprintf(stdout, "TR-101-290_2.3, PCR_error\n");
                err->PCR_error = 0;
        }
        if(err->PCR_repetition_error)
        {
                print_atp_value(obj);
                fprintf(stdout, "TR-101-290_2.3, PCR_repetition_error(%+7.3f ms)\n",
                        (double)(rslt->PCR_interval) / PCR_MS);
                err->PCR_repetition_error = 0;
        }
        if(err->PCR_accuracy_error)
        {
                print_atp_value(obj);
                fprintf(stdout, "TR-101-290_2.4, PCR_accuracy_error(%+6.3f ms)\n",
                        (double)(rslt->PCR_jitter) / PCR_US);
                err->PCR_accuracy_error = 0;
        }
        if(err->PTS_error)
        {
                print_atp_value(obj);
                fprintf(stdout, "TR-101-290_2.5, PTS_error\n");
                err->PTS_error = 0;
        }
        if(err->CAT_error)
        {
                print_atp_value(obj);
                fprintf(stdout, "TR-101-290_2.6, CAT_error\n");
                err->CAT_error = 0;
        }

        // Third priority: application dependant monitoring
        // ...

        return;
}

static void print_atp_title(obj_t *obj)
{
        if(obj->is_mono)
        {
                fprintf(stdout,
                        "yyyy-mm-dd hh:mm:ss"
                        ", "
                        "address"
                        ", address, STC, STC_base, "
                        "PID"
                        ", ");
        }
        else
        {
                fprintf(stdout,
                        FYELLOW "yyyy-mm-dd hh:mm:ss" NONE
                        ", "
                        FYELLOW "address" NONE
                        ", address, STC, STC_base, "
                        FYELLOW "PID" NONE
                        ", ");
        }
        return;
}

static void print_atp_value(obj_t *obj)
{
        ts_rslt_t *rslt;
        time_t tp;
        struct tm *lt; // local time
        char stime[32];

        rslt = obj->rslt;

        time(&tp);
        lt = localtime(&tp);
        strftime(stime, 32, "%Y-%m-%d %H:%M:%S", lt);

        if(obj->is_mono)
        {
                fprintf(stdout,
                        "%s"
                        ", "
                        "0x%llX"
                        ", %lld, %llu, %llu, "
                        "0x%04X"
                        ", ",
                        stime,
                        rslt->addr, rslt->addr,
                        rslt->STC, rslt->STC_base,
                        rslt->pid);
        }
        else
        {
                fprintf(stdout,
                        FYELLOW "%s" NONE
                        ", "
                        FYELLOW "0x%llX" NONE
                        ", %lld, %llu, %llu, "
                        FYELLOW "0x%04X" NONE
                        ", ",
                        stime,
                        rslt->addr, rslt->addr,
                        rslt->STC, rslt->STC_base,
                        rslt->pid);
        }
        return;
}

//=============================================================================
// THE END.
//=============================================================================
