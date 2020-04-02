/* init.c - MemTest-86  Version 4.1
 *
 * Released under version 2 of the Gnu Public License.
 * By Chris Brady
 *
 */

#include "stdint.h"
#include "test.h"
#include "defs.h"
#include "config.h"
#include "cpuid.h"
#include "smp.h"
#include "io.h"

extern struct tseq tseq[];
extern short memsz_mode;
extern int num_cpus;
extern int act_cpus;
extern char cpu_mask[];
extern int found_cpus;
extern long bin_mask;

/* Here we store all of the cpuid data */
extern struct cpu_ident cpu_id;

int l1_cache=0, l2_cache=0, l3_cache=0;
int tsc_invariable = 0;

ulong memspeed(ulong src, ulong len, int iter);
ulong memspeed_read(ulong src, ulong len, int iter);
ulong memspeed_write(ulong src, ulong len, int iter);
static void cpu_type(void);
static int cpuspeed(void);
static void get_cache_size();
static void cpu_cache_speed();
void get_cpuid();

static void display_init(void)
{
	int i;
	volatile char *pp;

	serial_echo_init();
        serial_echo_print("[LINE_SCROLL;24r"); /* Set scroll area row 7-23 */
        serial_echo_print("[H[2J");   /* Clear Screen */
        serial_echo_print("[37m[44m");
        serial_echo_print("[0m");
        serial_echo_print("[37m[44m");

	/* Clear top of screen */
	for(i=0, pp=(char *)(SCREEN_ADR); i<80*11; i++) {
		*pp = ' ';
		pp += 2;
	}

	/* Make the name background red */
	for(i=0, pp=(char *)(SCREEN_ADR+1); i<TITLE_WIDTH; i++, pp+=2) {
		*pp = 0x47;
	}

	cprint(0, 0, "      Memtest86 v4.3.7      ");

	/* Do reverse video for the bottom display line */
	for(i=0, pp=(char *)(SCREEN_ADR+1+(24 * 160)); i<80; i++, pp+=2) {
		*pp = 0x71;
	}

        serial_echo_print("[0m");
}

/*
 * Initialize test, setup screen and find out how much memory there is.
 */
void init(void)
{
	int i;

	outb(0x8, 0x3f2);  /* Kill Floppy Motor */

	/* Turn on cache */
	set_cache(1);

	/* Setup the display */
	display_init();
	cprint(1, COL_MID,"Pass   %");
	cprint(2, COL_MID,"Test   %");
	cprint(3, COL_MID,"Test #");
	cprint(4, COL_MID,"Testing: ");
	cprint(5, COL_MID,"Pattern: ");
	cprint(1, 0, "CPU Clk :         ");
	cprint(2, 0, "L1 Cache: Unknown ");
	cprint(3, 0, "L2 Cache: Unknown ");
     	cprint(4, 0, "L3 Cache:  None    ");
     	cprint(5, 0, "Memory  :         ");
     	cprint(6, 0, "------------------------------------------------------------------------------");
	cprint(7, 0, "CPU:");
	cprint(8, 0, "State:");
	cprint(7, 39, "| CPUs_Found:        CPU_Mask:");
	cprint(8, 39, "| CPUs_Started:      CPUs_Active:");
	for (i = 0; i <num_cpus; i++) {
		dprint(7, i+7, i%10, 1, 0);
		if (cpu_mask[i]) {
		    cprint(8, i+7, "S");
		} else {
		    cprint(8, i+7, "H");
		}
	}
	dprint(7, 54, found_cpus, 2, 0);
	dprint(8, 54, act_cpus, 2, 0);
	hprint(7, 70, bin_mask);
	cprint(9, 0, "------------------------------------------------------------------------------");
	for(i=1; i < 6; i++) {
		cprint(i, COL_MID-2, "| ");
	}
	cprint(LINE_INFO, 0,
"Time:  0:00:00   Iterations:      AdrsMode:32Bit   Pass:     0   Errors:    0 ");
	footer();

     	aprint(5, 10, v->test_pages);

	/* Get the cpu and cache information */
	get_cache_size();
	cpu_type();
	cpu_cache_speed();

	/* Record the start time */
        asm __volatile__ ("rdtsc":"=a" (v->startl),"=d" (v->starth));
        v->snapl = v->startl;
        v->snaph = v->starth;
	if (l1_cache == 0) { l1_cache = 66; }
	if (l2_cache == 0) { l1_cache = 666; }
}

/* Get cache sizes for most AMD and Intel CPUs, exceptions for old CPUs are
 * handled in CPU detection */
void get_cache_size()
{
	int i, j, n, size;
	unsigned int v[4];
	unsigned char *dp = (unsigned char *)v;
	struct cpuid4_eax *eax = (struct cpuid4_eax *)&v[0];
	struct cpuid4_ebx *ebx = (struct cpuid4_ebx *)&v[1];
	struct cpuid4_ecx *ecx = (struct cpuid4_ecx *)&v[2];

	btrace(0, __LINE__, "get_cache ", 1, cpu_id.max_cpuid,
		(long)cpu_id.vend_id.char_array[0]);
	switch(cpu_id.vend_id.char_array[0]) {
	/* AMD Processors */
	case 'A':
		l1_cache = cpu_id.cache_info.amd.l1_i_sz;
		l1_cache += cpu_id.cache_info.amd.l1_d_sz;
		l2_cache = cpu_id.cache_info.amd.l2_sz;
		l3_cache = cpu_id.cache_info.amd.l3_sz;
     		l3_cache *= 512;
		break;
	case 'G':
		/* Intel Processors */
		l1_cache = 0;
		l2_cache = 0;
		l3_cache = 0;

		/* Use CPUID(4) if it is available */
		if (cpu_id.max_cpuid > 3) {

		    /* figure out how many cache leaves */
		    n = -1;
		    do {
			++n;
			/* Do cpuid(4) loop to find out num_cache_leaves */
			cpuid_count(4, n, &v[0], &v[1], &v[2], &v[3]);
		    } while ((eax->ctype) != 0);

		    /* loop through all of the leaves */
		    for (i=0; i<n; i++) {
			cpuid_count(4, i, &v[0], &v[1], &v[2], &v[3]);

			/* Check for a valid cache type */
			if (eax->ctype > 0 && eax->ctype < 4) {

			    /* Compute the cache size */
			    size = (ecx->number_of_sets + 1) *
                          	  (ebx->coherency_line_size + 1) *
                          	  (ebx->physical_line_partition + 1) *
                          	  (ebx->ways_of_associativity + 1);
			    size /= 1024;

			    switch (eax->level) {
			    case 1:
				l1_cache += size;
				break;
			    case 2:
				l2_cache += size;
				break;
			    case 3:
				l3_cache += size;
				break;
			    }
			}
		    }
		    return;
		}

		/* No CPUID(4) so we use the older CPUID(2) method */
		/* Get number of times to iterate */
		cpuid(2, &v[0], &v[1], &v[2], &v[3]);
		n = v[0] & 0xff;
                for (i=0 ; i<n ; i++) {
                    cpuid(2, &v[0], &v[1], &v[2], &v[3]);

                    /* If bit 31 is set, this is an unknown format */
                    for (j=0 ; j<3 ; j++) {
                            if (v[j] & (1 << 31)) {
                                    v[j] = 0;
			    }
		    }

                    /* Byte 0 is level count, not a descriptor */
                    for (j = 1 ; j < 16 ; j++) {
			switch(dp[j]) {
			case 0x6:
			case 0xa:
			case 0x66:
				l1_cache += 8;
				break;
			case 0x8:
			case 0xc:
			case 0xd:
			case 0x60:
			case 0x67:
				l1_cache += 16;
				break;
			case 0xe:
				l1_cache += 24;
				break;
			case 0x9:
			case 0x2c:
			case 0x30:
			case 0x68:
				l1_cache += 32;
				break;
			case 0x39:
			case 0x3b:
			case 0x41:
			case 0x79:
				l2_cache += 128;
				break;
			case 0x3a:
				l2_cache += 192;
				break;
			case 0x21:
			case 0x3c:
			case 0x3f:
			case 0x42:
			case 0x7a:
			case 0x82:
				l2_cache += 256;
				break;
			case 0x3d:
				l2_cache += 384;
				break;
			case 0x3e:
			case 0x43:
			case 0x7b:
			case 0x7f:
			case 0x80:
			case 0x83:
			case 0x86:
				l2_cache += 512;
				break;
			case 0x44:
			case 0x78:
			case 0x7c:
			case 0x84:
			case 0x87:
				l2_cache += 1024;
				break;
			case 0x45:
			case 0x7d:
			case 0x85:
				l2_cache += 2048;
				break;
			case 0x48:
				l2_cache += 3072;
				break;
			case 0x4e:
				l2_cache += 6144;
				break;
			case 0x23:
			case 0xd0:
				l3_cache += 512;
				break;
			case 0xd1:
			case 0xd6:
				l3_cache += 1024;
				break;
			case 0x25:
			case 0xd2:
			case 0xd7:
			case 0xdc:
			case 0xe2:
				l3_cache += 2048;
				break;
			case 0x29:
			case 0x46:
			case 0x49:
			case 0xd8:
			case 0xdd:
			case 0xe3:
				l3_cache += 4096;
				break;
			case 0x4a:
				l3_cache += 6144;
				break;
			case 0x47:
			case 0x4b:
			case 0xde:
			case 0xe4:
				l3_cache += 8192;
				break;	
			case 0x4c:
			case 0xea:
				l3_cache += 12288;
				break;	
			case 0x4d:
				l3_cache += 16384;
				break;	
			case 0xeb:
				l3_cache += 18432;
				break;	
			case 0xec:
				l3_cache += 24576;
				break;	
			} /* end switch */
		    } /* end for 1-16 */
		} /* end for 0 - n */
	}
}

// Remove random junk in CPU type strings
char* makeCleanCpuName(char* typeString)
{
	// Remove these keywords
	{
		const char* removeStrings[] = {"(R)", "(TM)","(tm)", "CPU", "Processor", "Technology", "Genuine", "processor", " with Radeon HD Graphics"};
		size_t i;
		for (i = 0; i < sizeof(removeStrings)/sizeof(removeStrings[0]); ++i)
		{
			char* found = strstr(typeString, removeStrings[i]);
			if (found)
			{
				char* afterFound = found + strlen(removeStrings[i]);
				while (*afterFound != 0)
				{
					*found = *afterFound;
					++found; ++afterFound;
				}
				*found = 0;
			}
		}
	}

	// Turn any double created by previous exchanges into single spaces
	{
		char *i, *j;
		for (i=typeString, j=typeString; *i!=0; ++j, ++i)
		{
			if (i!=j)
				*i = *j;

			// If this character is a space skip any subsequent spaces
			if (*i == ' ')
			{
				while (*(j+1) == ' ')
					++j;
			}
		}
	}

	return typeString;
}

/*
 * Find CPU type
 */
void cpu_type(void)
{
	btrace(0, __LINE__, "cpu_type  ", 1, (long)cpu_id.max_xcpuid,
		(long)cpu_id.vend_id.char_array[0]);
	/* If we can get a brand string use it, and we are done */
	if (cpu_id.max_xcpuid >= 0x80000004) {
		makeCleanCpuName(cpu_id.brand_id.char_array);
		cprint(0, COL_MID, cpu_id.brand_id.char_array);
		return;
	}

	/* The brand string is not available so we need to figure out 
	 * CPU what we have */
	switch(cpu_id.vend_id.char_array[0]) {
	/* AMD Processors */
	case 'A':
		switch(cpu_id.vers.bits.family) {
		case 4:
			switch(cpu_id.vers.bits.model) {
			case 3:
				cprint(0, COL_MID, "AMD 486DX2");
				break;
			case 7:
				cprint(0, COL_MID, "AMD 486DX2-WB");
				break;
			case 8:
				cprint(0, COL_MID, "AMD 486DX4");
				break;
			case 9:
				cprint(0, COL_MID, "AMD 486DX4-WB");
				break;
			case 14:
				cprint(0, COL_MID, "AMD 5x86-WT");
				break;
			case 15:
				cprint(0, COL_MID, "AMD 5x86-WB");
				break;
			}
			/* Since we can't get CPU speed or cache info return */
			return;
		case 5:
			switch(cpu_id.vers.bits.model) {
			case 0:
			case 1:
			case 2:
			case 3:
				cprint(0, COL_MID, "AMD K5");
				l1_cache = 8;
				break;
			case 6:
			case 7:
				cprint(0, COL_MID, "AMD K6");
				break;
			case 8:
				cprint(0, COL_MID, "AMD K6-2");
				break;
			case 9:
				cprint(0, COL_MID, "AMD K6-III");
				break;
			case 13: 
				cprint(0, COL_MID, "AMD K6-III+"); 
				break;
			}
			break;
		case 6:

			switch(cpu_id.vers.bits.model) {
			case 1:
				cprint(0, COL_MID, "AMD Athlon (0.25)");
				break;
			case 2:
			case 4:
				cprint(0, COL_MID, "AMD Athlon (0.18)");
				break;
			case 6:
				if (l2_cache == 64) {
					cprint(0, COL_MID, "AMD Duron (0.18)");
				} else {
					cprint(0, COL_MID, "Athlon XP (0.18)");
				}
				break;
			case 8:
			case 10:
				if (l2_cache == 64) {
					cprint(0, COL_MID, "AMD Duron (0.13)");
				} else {
					cprint(0, COL_MID, "Athlon XP (0.13)");
				}
				break;
			case 3:
			case 7:
				cprint(0, COL_MID, "AMD Duron");
				/* Duron stepping 0 CPUID for L2 is broken */
				/* (AMD errata T13)*/
				if (cpu_id.vers.bits.stepping == 0) { /* stepping 0 */
					/* Hard code the right L2 size */
					l2_cache = 64;
				} else {
				}
				break;
			}
			break;

			/* All AMD family values >= 10 have the Brand ID
			 * feature so we don't need to find the CPU type */
		}
		break;

	/* Intel or Transmeta Processors */
	case 'G':
		if ( cpu_id.vend_id.char_array[7] == 'T' ) { /* GenuineTMx86 */
			if (cpu_id.vers.bits.family == 5) {
				cprint(0, COL_MID, "TM 5x00");
			} else if (cpu_id.vers.bits.family == 15) {
				cprint(0, COL_MID, "TM 8x00");
			}
			l1_cache = cpu_id.cache_info.ch[3] + cpu_id.cache_info.ch[7];
			l2_cache = (cpu_id.cache_info.ch[11]*256) + cpu_id.cache_info.ch[10];
		} else {				/* GenuineIntel */
			if (cpu_id.vers.bits.family == 4) {
			switch(cpu_id.vers.bits.model) {
			case 0:
			case 1:
				cprint(0, COL_MID, "Intel 486DX");
				break;
			case 2:
				cprint(0, COL_MID, "Intel 486SX");
				break;
			case 3:
				cprint(0, COL_MID, "Intel 486DX2");
				break;
			case 4:
				cprint(0, COL_MID, "Intel 486SL");
				break;
			case 5:
				cprint(0, COL_MID, "Intel 486SX2");
				break;
			case 7:
				cprint(0, COL_MID, "Intel 486DX2-WB");
				break;
			case 8:
				cprint(0, COL_MID, "Intel 486DX4");
				break;
			case 9:
				cprint(0, COL_MID, "Intel 486DX4-WB");
				break;
			}
			/* Since we can't get CPU speed or cache info return */
			return;
		}


		switch(cpu_id.vers.bits.family) {
		case 5:
			switch(cpu_id.vers.bits.model) {
			case 0:
			case 1:
			case 2:
			case 3:
			case 7:
				cprint(0, COL_MID, "Pentium");
				if (l1_cache == 0) {
					l1_cache = 8;
				}
				break;
			case 4:
			case 8:
				cprint(0, COL_MID, "Pentium-MMX");
				if (l1_cache == 0) {
					l1_cache = 16;
				}
				break;
			}
			break;
		case 6:
			switch(cpu_id.vers.bits.model) {
			case 0:
			case 1:
				cprint(0, COL_MID, "Pentium Pro");
				break;
			case 3:
			case 4:
				cprint(0, COL_MID, "Pentium II");
				break;
			case 5:
				if (l2_cache == 0) {
					cprint(0, COL_MID, "Celeron");
				} else {
					cprint(0, COL_MID, "Pentium II");
				}
				break;
			case 6:
				  if (l2_cache == 128) {
					cprint(0, COL_MID, "Celeron");
				  } else {
					cprint(0, COL_MID, "Pentium II");
				  }
				}
				break;
			case 7:
			case 8:
			case 11:
				if (l2_cache == 128) {
					cprint(0, COL_MID, "Celeron");
				} else {
					cprint(0, COL_MID, "Pentium III");
				}
				break;
			case 9:
				if (l2_cache == 512) {
					cprint(0, COL_MID, "Celeron M (0.13)");
				} else {
					cprint(0, COL_MID, "Pentium M (0.13)");
				}
				break;
     			case 10:
				cprint(0, COL_MID, "Pentium III Xeon");
				break;
			case 12:
				l1_cache = 24;
				cprint(0, COL_MID, "Atom (0.045)");
				break;					
			case 13:
				if (l2_cache == 1024) {
					cprint(0, COL_MID, "Celeron M (0.09)");
				} else {
					cprint(0, COL_MID, "Pentium M (0.09)");
				}
				break;
			case 14:
				cprint(0, COL_MID, "Intel Core");
				break;				
			case 15:
				if (l2_cache == 1024) {
					cprint(0, COL_MID, "Pentium E");
				} else {
					cprint(0, COL_MID, "Intel Core 2");
				}
				break;
			}
			break;
		case 15:
			switch(cpu_id.vers.bits.model) {
			case 0:
			case 1:			
			case 2:
				if (l2_cache == 128) {
					cprint(0, COL_MID, "Celeron");
				} else {
					cprint(0, COL_MID, "Pentium 4");
				}
				break;
			case 3:
			case 4:
				if (l2_cache == 256) {
					cprint(0, COL_MID, "Celeron (0.09)");
				} else {
					cprint(0, COL_MID, "Pentium 4 (0.09)");
				}
				break;
			case 6:
				cprint(0, COL_MID, "Pentium D (65nm)");
				break;
			default:
				cprint(0, COL_MID, "Unknown Intel");
 				break;
			break;
		    }

		}
		break;

	/* VIA/Cyrix/Centaur Processors with CPUID */
	case 'C':
		if ( cpu_id.vend_id.char_array[1] == 'e' ) { /* CentaurHauls */
			l1_cache = cpu_id.cache_info.ch[3] + cpu_id.cache_info.ch[7];
			l2_cache = cpu_id.cache_info.ch[11];
			switch(cpu_id.vers.bits.family){
			case 5:
				cprint(0, COL_MID, "Centaur 5x86");
				break;
			case 6: // VIA C3
				switch(cpu_id.vers.bits.model){
				default:
				    if (cpu_id.vers.bits.stepping < 8) {
					cprint(0, COL_MID, "VIA C3 Samuel2");
				    } else {
					cprint(0, COL_MID, "VIA C3 Eden");
				    }
				break;
				case 10:
					cprint(0, COL_MID, "VIA C7 (C5J)");
					l1_cache = 64;
					l2_cache = 128;
					break;
				case 13:
					cprint(0, COL_MID, "VIA C7 (C5R)");
					l1_cache = 64;
					l2_cache = 128;
					break;
				case 15:
					cprint(0, COL_MID, "VIA Isaiah (CN)");
					l1_cache = 64;
					l2_cache = 128;
					break;
				}
			}
		} else {				/* CyrixInstead */
			switch(cpu_id.vers.bits.family) {
			case 5:
				switch(cpu_id.vers.bits.model) {
				case 0:
					cprint(0, COL_MID, "Cyrix 6x86MX/MII");
					break;
				case 4:
					cprint(0, COL_MID, "Cyrix GXm");
					break;
				}
				return;

			case 6: // VIA C3
				switch(cpu_id.vers.bits.model) {
				case 6:
					cprint(0, COL_MID, "Cyrix III");
					break;
				case 7:
					if (cpu_id.vers.bits.stepping < 8) {
						cprint(0, COL_MID, "VIA C3 Samuel2");
					} else {
						cprint(0, COL_MID, "VIA C3 Ezra-T");
					}
					break;
				case 8:
					cprint(0, COL_MID, "VIA C3 Ezra-T");
					break;
				case 9:
					cprint(0, COL_MID, "VIA C3 Nehemiah");
					break;
				}
				// L1 = L2 = 64 KB from Cyrix III to Nehemiah
				l1_cache = 64;
				l2_cache = 64;
				break;
			}
		}
		break;
	/* Unknown processor */
	default:
		/* Make a guess at the family */
		switch(cpu_id.vers.bits.family) {
		case 5:
			cprint(0, COL_MID, "586");
		case 6:
			cprint(0, COL_MID, "686");
		default:
			cprint(0, COL_MID, "Unidentified Processor");
		}
	}
}

#define STEST_ADDR 0x100000	/* Measure memory speed starting at 1MB */

/* Measure and display CPU and cache sizes and speeds */
void cpu_cache_speed()
{
	ulong speed;

	btrace(0, __LINE__, "cache_spd ", 1, l1_cache, l2_cache);

	/* Print CPU speed */
	if ((speed = cpuspeed()) > 0) {
		if (speed < 999499) {
			speed += 50; /* for rounding */
			cprint(1, 10, "    . MHz");
			dprint(1, 10+1, speed/1000, 3, 1);
			dprint(1, 10+5, (speed/100)%10, 1, 0);
		} else {
			speed += 500; /* for rounding */
			cprint(1, 10, "      MHz");
			dprint(1, 10, speed/1000, 5, 0);
		}
	}

	/* Print out L1 cache info */
	/* To measure L1 cache speed we use a block size that is 1/4th */
	/* of the total L1 cache size since half of it is for instructions */
	if (l1_cache) {
		cprint(2, 0, "L1 Cache:     K  ");
		dprint(2, 11, l1_cache, 3, 0);
		if ((speed=memspeed(STEST_ADDR, (l1_cache/4)*1024, 200)) > 0) {
			cprint(2, 16, "       MB/s");
			dprint(2, 16, speed, 6, 0);
		}
	}

	/* Print out L2 cache info */
	if (l2_cache) {
		cprint(3, 0, "L2 Cache:     K  ");
		dprint(3, 10, l2_cache, 4, 0);

		if ((speed=memspeed(STEST_ADDR, (l2_cache/2)*1024, 150)) > 0) {
			cprint(3, 16, "       MB/s");
			dprint(3, 16, speed, 6, 0);
		}
	}
	/* Print out L3 cache info */
	if (l3_cache) {
		cprint(4, 0, "L3 Cache:     K  ");
    		dprint(4, 10, l3_cache, 4, 0);
    		dprint(4, 10, l3_cache, 4, 0);
    
    		if ((speed=memspeed(STEST_ADDR, (l3_cache/2)*1024, 100)) > 0) {
    			cprint(4, 16, "       MB/s");
    			dprint(4, 16, speed, 6, 0);
    		}
    	}
}

/* Measure and display memory speed, multitasked using all CPUs */
ulong spd[MAX_CPUS];
void get_mem_speed(int me, int ncpus)
{
	int i;
	ulong speed=0;
	ulong start, len;

    	/* Determine memory speed.  To find the memory speed we use 
    	 * A block size that is the sum of all the L1, L2 & L3 caches
	 * in all cpus * 8 */
    	i = (l3_cache + l2_cache*ncpus + l1_cache*ncpus) * 8;

	/* Make sure that we have enough memory to do the test */
	/* If not use all we have */
	if ((1 + (i * 2)) > (v->plim_upper << 2)) {
		i = ((v->plim_upper <<2) - 1) / 2;
	}
	/* Divide up the memory block among the CPUs */
	len = i * 1024 / ncpus;
	start = STEST_ADDR + (len * me);
	btrace(me, __LINE__, "mem_speed ", 1, start, len);
	
	barrier();
	if (me == 0)
		spd[me] = memspeed(start, len, 35);
	barrier();
	if (me == 0) {
#if 0
		for (i=0; i<ncpus; i++) {
			speed += spd[i];
		}
#endif
		speed = spd[me];
		cprint(5, 16, "       MB/s");
		dprint(5, 16, speed, 6, 0);
	}
}

/* #define TICKS 5 * 11832 (count = 6376)*/
/* #define TICKS (65536 - 12752) */
#define TICKS 59659	/* 50 ms */

/* Returns CPU clock in khz */
ulong s_low, s_high;
static int cpuspeed(void)
{
	int loops;
	ulong end_low, end_high;

	if (cpu_id.fid.bits.rdtsc == 0 ) {
		v->clks_msec = -1;
		return(-1);
	}

	/* Setup timer */
	outb((inb(0x61) & ~0x02) | 0x01, 0x61);
	outb(0xb0, 0x43); 
	outb(TICKS & 0xff, 0x42);
	outb(TICKS >> 8, 0x42);

	asm __volatile__ ("rdtsc":"=a" (s_low),"=d" (s_high));

	loops = 0;
	do {
		loops++;
	} while ((inb(0x61) & 0x20) == 0);

	asm __volatile__ (
		"rdtsc\n\t" \
		"subl s_low,%%eax\n\t" \
		"sbbl s_high,%%edx\n\t" \
		:"=a" (end_low), "=d" (end_high)
	);

	/* Make sure we have a credible result */
	if (loops < 4 || end_low < 50000) {
		v->clks_msec = -1;
		return(-1);
	}
	v->clks_msec = end_low/50;
	return(v->clks_msec);
}

/* Measure cache speed by copying a block of memory. */
/* Returned value is kbytes/second */
ulong memspeed(ulong src, ulong len, int iter)
{
	int i;
	ulong dst, wlen;
	ulong st_low, st_high;
	ulong end_low, end_high;
	ulong cal_low, cal_high;
#ifdef FP_ENA
	float res;
#endif

	if (cpu_id.fid.bits.rdtsc == 0 || v->clks_msec == -1 || len == 0) {
		return(-1);
	}

	dst = src + len;
	wlen = len / 4;  /* Length is bytes */

	/* Calibrate the overhead with a zero word copy */
	asm __volatile__ ("rdtsc":"=a" (st_low),"=d" (st_high));
	for (i=0; i<iter; i++) {
		asm __volatile__ (
			"movl %0,%%esi\n\t" \
       		 	"movl %1,%%edi\n\t" \
       		 	"movl %2,%%ecx\n\t" \
       		 	"cld\n\t" \
       		 	"rep\n\t" \
       		 	"movsl\n\t" \
				:: "g" (src), "g" (dst), "g" (0)
			: "esi", "edi", "ecx"
		);
	}
	asm __volatile__ ("rdtsc":"=a" (cal_low),"=d" (cal_high));

	/* Compute the overhead time */
	asm __volatile__ (
		"subl %2,%0\n\t"
		"sbbl %3,%1"
		:"=a" (cal_low), "=d" (cal_high)
		:"g" (st_low), "g" (st_high),
		"0" (cal_low), "1" (cal_high)
	);


	/* Now measure the speed */
	/* Do the first copy to prime the cache */
	asm __volatile__ (
		"movl %0,%%esi\n\t" \
		"movl %1,%%edi\n\t" \
       	 	"movl %2,%%ecx\n\t" \
       	 	"cld\n\t" \
       	 	"rep\n\t" \
       	 	"movsl\n\t" \
		:: "g" (src), "g" (dst), "g" (wlen)
		: "esi", "edi", "ecx"
	);
	asm __volatile__ ("rdtsc":"=a" (st_low),"=d" (st_high));
	for (i=0; i<iter; i++) {
	        asm __volatile__ (
			"movl %0,%%esi\n\t" \
			"movl %1,%%edi\n\t" \
       		 	"movl %2,%%ecx\n\t" \
       		 	"cld\n\t" \
       		 	"rep\n\t" \
       		 	"movsl\n\t" \
			:: "g" (src), "g" (dst), "g" (wlen)
			: "esi", "edi", "ecx"
		);
	}
	asm __volatile__ ("rdtsc":"=a" (end_low),"=d" (end_high));

	/* Compute the elapsed time */
	asm __volatile__ (
		"subl %2,%0\n\t"
		"sbbl %3,%1"
		:"=a" (end_low), "=d" (end_high)
		:"g" (st_low), "g" (st_high),
		"0" (end_low), "1" (end_high)
	);
	/* Subtract the overhead time */
	asm __volatile__ (
		"subl %2,%0\n\t"
		"sbbl %3,%1"
		:"=a" (end_low), "=d" (end_high)
		:"g" (cal_low), "g" (cal_high),
		"0" (end_low), "1" (end_high)
	);

#ifndef FP_ENA
       /* We have to fit the result into 32 bits so we dos some complicated
         * combinations to keep from over or underflow. Don't dink with unless
         * you know what you are doing. */
        end_low = end_low >> 4;
        end_low = ((end_high&0xf) << 28) | end_low;

        /* Convert to clocks/KB */
        end_low /= (len/1024);
        end_low *= 2048;
        end_low /= iter;
        end_low = (v->clks_msec)*256/end_low;
        return(end_low);
#else
	/* Convert to floating point */
	res = (float)end_high*4294967296.0 + (float)end_low;
	/* Convert from clocks to MB per second */
	res /= 2.0;
	res /= (float)len;
	res *= 1024.0;
	res /= (float)iter;
	res = (float)v->clks_msec/res;
	return((long)res);
#endif
}

/* Measure cache speed by copying a block of memory. */
/* Returned value is kbytes/second */
ulong memspeed_read(ulong src, ulong len, int iter)
{
	int i;
	ulong wlen;
	ulong st_low, st_high;
	ulong end_low, end_high;
	ulong cal_low, cal_high;
#ifdef FP_ENA
	float res;
#endif

	if (cpu_id.fid.bits.rdtsc == 0 || v->clks_msec == -1 || len == 0) {
		return(-1);
	}

	wlen = len / 4;  /* Length is bytes */

	/* Calibrate the overhead with a zero word copy */
	asm __volatile__ ("rdtsc":"=a" (st_low),"=d" (st_high));
	for (i=0; i<iter; i++) {
		asm __volatile__ (
			"movl %0,%%esi\n\t" \
			"movl %1,%%ecx\n\t" \

			"read_cached_dword_loop1:\n\t" \
			"addl $4,%%esi\n\t" \
			"decl %%ecx\n\t" \
			"jnz read_cached_dword_loop1\n\t"
			:: "g" (src), "g" (wlen)
			: "esi", "ecx"
		);
	}
	asm __volatile__ ("rdtsc":"=a" (cal_low),"=d" (cal_high));

	/* Compute the overhead time */
	asm __volatile__ (
		"subl %2,%0\n\t"
		"sbbl %3,%1"
		:"=a" (cal_low), "=d" (cal_high)
		:"g" (st_low), "g" (st_high),
		"0" (cal_low), "1" (cal_high)
	);


	/* Now measure the speed */
	/* Do the first copy to prime the cache */
	asm __volatile__ (
		"movl %0,%%esi\n\t" \
		"movl %1,%%ecx\n\t" \

		"read_cached_dword_loop2:\n\t" \
		"movl (%%esi),%%edx\n\t" \
		"addl $4,%%esi\n\t" \
		"decl %%ecx\n\t" \
		"jnz read_cached_dword_loop2\n\t"
		:: "g" (src), "g" (wlen)
		: "esi", "ecx", "edx"
	);
	asm __volatile__ ("rdtsc":"=a" (st_low),"=d" (st_high));
	for (i=0; i<iter; i++) {
		asm __volatile__ (
			"movl %0,%%esi\n\t" \
			"movl %1,%%ecx\n\t" \

			"read_cached_dword_loop3:\n\t" \
			"movl (%%esi),%%edx\n\t" \
			"addl $4,%%esi\n\t" \
			"decl %%ecx\n\t" \
			"jnz read_cached_dword_loop3\n\t"
			:: "g" (src), "g" (wlen)
			: "esi", "ecx", "edx"
		);
	}
	asm __volatile__ ("rdtsc":"=a" (end_low),"=d" (end_high));

	/* Compute the elapsed time */
	asm __volatile__ (
		"subl %2,%0\n\t"
		"sbbl %3,%1"
		:"=a" (end_low), "=d" (end_high)
		:"g" (st_low), "g" (st_high),
		"0" (end_low), "1" (end_high)
	);
	/* Subtract the overhead time */
	asm __volatile__ (
		"subl %2,%0\n\t"
		"sbbl %3,%1"
		:"=a" (end_low), "=d" (end_high)
		:"g" (cal_low), "g" (cal_high),
		"0" (end_low), "1" (end_high)
	);

#ifndef FP_ENA
       /* We have to fit the result into 32 bits so we dos some complicated
         * combinations to keep from over or underflow. Don't dink with unless
         * you know what you are doing. */
        end_low = end_low >> 4;
        end_low = ((end_high&0xf) << 28) | end_low;

        /* Convert to clocks/KB */
        end_low /= (len/1024);
        end_low *= 2048;
        end_low /= iter;
        end_low = (v->clks_msec)*256/end_low;
        return(end_low);
#else
	/* Convert to floating point */
	res = (float)end_high*4294967296.0 + (float)end_low;
	/* Convert from clocks to MB per second */
	res /= 2.0;
	res /= (float)len;
	res *= 1024.0;
	res /= (float)iter;
	res = (float)v->clks_msec/res;
	return((long)res);
#endif
}

/* Measure cache speed by copying a block of memory. */
/* Returned value is kbytes/second */
ulong memspeed_write(ulong src, ulong len, int iter)
{
	int i;
	ulong wlen;
	ulong st_low, st_high;
	ulong end_low, end_high;
	ulong cal_low, cal_high;
#ifdef FP_ENA
	float res;
#endif

	if (cpu_id.fid.bits.rdtsc == 0 || v->clks_msec == -1 || len == 0) {
		return(-1);
	}

	wlen = len / 4;  /* Length is bytes */

	/* Calibrate the overhead with a zero word copy */
	asm __volatile__ ("rdtsc":"=a" (st_low),"=d" (st_high));
	for (i=0; i<iter; i++) {
		asm __volatile__ (
			"movl %0,%%esi\n\t" \
			"movl %1,%%ecx\n\t" \

			"write_cached_dword_loop1:\n\t" \
			"addl $4,%%esi\n\t" \
			"decl %%ecx\n\t" \
			"jnz write_cached_dword_loop1\n\t"
			:: "g" (src), "g" (wlen)
			: "esi", "ecx"
		);
	}
	asm __volatile__ ("rdtsc":"=a" (cal_low),"=d" (cal_high));

	/* Compute the overhead time */
	asm __volatile__ (
		"subl %2,%0\n\t"
		"sbbl %3,%1"
		:"=a" (cal_low), "=d" (cal_high)
		:"g" (st_low), "g" (st_high),
		"0" (cal_low), "1" (cal_high)
	);


	/* Now measure the speed */
	/* Do the first copy to prime the cache */
	asm __volatile__ (
		"movl %0,%%esi\n\t" \
		"movl %1,%%ecx\n\t" \

		"write_cached_dword_loop2:\n\t" \
		"movl %%edx,(%%esi)\n\t" \
		"addl $4,%%esi\n\t" \
		"decl %%ecx\n\t" \
		"jnz write_cached_dword_loop2\n\t"
		:: "g" (src), "g" (wlen)
		: "esi", "ecx", "edx"
	);
	asm __volatile__ ("rdtsc":"=a" (st_low),"=d" (st_high));
	for (i=0; i<iter; i++) {
		asm __volatile__ (
			"movl %0,%%esi\n\t" \
			"movl %1,%%ecx\n\t" \

			"write_cached_dword_loop3:\n\t" \
			"movl %%edx,(%%esi)\n\t" \
			"addl $4,%%esi\n\t" \
			"decl %%ecx\n\t" \
			"jnz write_cached_dword_loop3\n\t"
			:: "g" (src), "g" (wlen)
			: "esi", "ecx", "edx"
		);
	}
	asm __volatile__ ("rdtsc":"=a" (end_low),"=d" (end_high));

	/* Compute the elapsed time */
	asm __volatile__ (
		"subl %2,%0\n\t"
		"sbbl %3,%1"
		:"=a" (end_low), "=d" (end_high)
		:"g" (st_low), "g" (st_high),
		"0" (end_low), "1" (end_high)
	);
	/* Subtract the overhead time */
	asm __volatile__ (
		"subl %2,%0\n\t"
		"sbbl %3,%1"
		:"=a" (end_low), "=d" (end_high)
		:"g" (cal_low), "g" (cal_high),
		"0" (end_low), "1" (end_high)
	);

#ifndef FP_ENA
       /* We have to fit the result into 32 bits so we dos some complicated
         * combinations to keep from over or underflow. Don't dink with unless
         * you know what you are doing. */
        end_low = end_low >> 4;
        end_low = ((end_high&0xf) << 28) | end_low;

        /* Convert to clocks/KB */
        end_low /= (len/1024);
        end_low *= 2048;
        end_low /= iter;
        end_low = (v->clks_msec)*256/end_low;
        return(end_low);
#else
	/* Convert to floating point */
	res = (float)end_high*4294967296.0 + (float)end_low;
	/* Convert from clocks to MB per second */
	res /= 2.0;
	res /= (float)len;
	res *= 1024.0;
	res /= (float)iter;
	res = (float)v->clks_msec/res;
	return((long)res);
#endif
}

#define rdmsr(msr,val1,val2) \
	__asm__ __volatile__("rdmsr" \
		  : "=a" (val1), "=d" (val2) \
		  : "c" (msr))
