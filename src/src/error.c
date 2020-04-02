/* error.c - MemTest-86  Version 4.1
 *
 * Released under version 2 of the Gnu Public License.
 * By Chris Brady
 */
#include "stdint.h"
#include "test.h"
#include "config.h"
#include "cpuid.h"
#include "smp.h"

extern struct cpu_ident cpu_id;
extern struct barrier_s *barr;
extern int test_ticks, nticks;
extern struct tseq tseq[];
extern volatile int test;
extern int smp_ord_to_cpu(int me);
void poll_errors();

static void update_err_counts(void);
static void print_err_counts(void);
static void common_err();
static int syn, chan, len=1;

#define MAX_ERRORS 0xFFFF

/*
 * Display data error message. Don't display duplicate errors.
 */
void error(ulong *adr, ulong good, ulong bad)
{
	ulong xor;

	spin_lock(&barr->mutex);
	xor = good ^ bad;
#ifdef USB_WAR
	/* Skip any errrors that appear to be due to the BIOS using location
	 * 0x4e0 for USB keyboard support.  This often happens with Intel
         * 810, 815 and 820 chipsets.  It is possible that we will skip
	 * a real error but the odds are very low.
	 */
	if ((ulong)adr == 0x4e0 || (ulong)adr == 0x410) {
		return;
	}
#endif
	common_err(adr, good, bad, xor, 0);
	spin_unlock(&barr->mutex);
}

/*
 * Display address error message.
 * Since this is strictly an address test, trying to create BadRAM
 * patterns does not make sense.  Just report the error.
 */
void ad_err1(ulong *adr1, ulong *mask, ulong bad, ulong good)
{
	spin_lock(&barr->mutex);
	common_err(adr1, good, bad, (ulong)mask, 1);
	spin_unlock(&barr->mutex);
}

/*
 * Display address error message.
 * Since this type of address error can also report data errors go
 * ahead and generate BadRAM patterns.
 */
void ad_err2(ulong *adr, ulong bad)
{
	spin_lock(&barr->mutex);
	common_err(adr, (ulong)adr, bad, ((ulong)adr) ^ bad, 0);
	spin_unlock(&barr->mutex);
}

static void update_err_counts(void)
{
	if (v->pass && v->ecount == 0) {
		cprint(LINE_MSG, COL_MSG,
			"                                            ");
	}

	if (v->ecount < MAX_ERRORS)
		++(v->ecount);

	if (tseq[test].errors < MAX_ERRORS)
		tseq[test].errors++;
		
}

static void print_err_counts(void)
{
	int i;
	char *pp;

	//if ((v->ecount > 4096) && (v->ecount % 256 != 0)) return;

	dprint(LINE_INFO, 72, v->ecount, 5, 0);
	if (v->ecount >= MAX_ERRORS)
		cprint(LINE_INFO, 77, "+");
/*
	dprint(LINE_INFO, 56, v->ecc_ecount, 6, 0);
*/

	/* Paint the error messages on the screen red to provide a vivid */
	/* indicator that an error has occured */ 
	if ((v->printmode == PRINTMODE_ADDRESSES ||
			v->printmode == PRINTMODE_PATTERNS) &&
			v->msg_line < 24) {
		for(i=0, pp=(char *)((SCREEN_ADR+v->msg_line*160+1));
				 i<76; i++, pp+=2) {
			*pp = 0x47;
		}
	}
}

/*
 * Print an individual error
 */
void common_err( ulong *adr, ulong good, ulong bad, ulong xor, int type) 
{
	int i, n, flag=0;
	ulong page, offset;
	int patnchg;
	ulong mb;

	update_err_counts();
	print_err_counts();

	switch(v->printmode) {
	case PRINTMODE_SUMMARY:
		
		for (i=0; tseq[i].msg != NULL; i++) {
			dprint(LINE_HEADER+1+i, 66, i, 2, 0);
			dprint(LINE_HEADER+1+i, 69, tseq[i].errors, 6, 0);
			if (tseq[i].errors >= MAX_ERRORS)
				cprint(LINE_HEADER+1+i, 75, "+");
	  	}

		/* Don't do anything for a parity error. */
		if (type == 3) {
			return;
		}

		/* Address error */
		if (type == 1) {
			xor = good ^ bad;
		}

		/* Ecc correctable errors */
		if (type == 2) {
			/* the bad value is the corrected flag */
			if (bad) {
				v->erri.cor_err++;
			}
			page = (ulong)adr;
			offset = good;
		} else {
			page = page_of(adr);
			offset = (ulong)adr & 0xFFF;
		}
		/* Normal data errors */
		if (type == 0) {
			page = page_of(adr);
			offset = (ulong)adr & 0xFFF;
		}

			
		/* Calc upper and lower error addresses */
		if (v->erri.low_addr.page > page){
			v->erri.low_addr.page = page;
			flag++; 
		}
		if (v->erri.low_addr.offset > offset){
			v->erri.low_addr.offset = offset;
			flag++; 
		}
		if (v->erri.high_addr.page < page){
			v->erri.high_addr.page = page;
			flag++; 
		}
		if (v->erri.high_addr.offset < offset){
			v->erri.high_addr.offset = offset;
			flag++; 
		}

		/* Calc bits in error */
		for (i=0, n=0; i<32; i++) {
			if (xor>>i & 1) {
				n++;
			}
		}

		// Don't increment total if we have reached max errors
		// Messes up average (total error bits / total errors)
		if (v->ecount < MAX_ERRORS)
			v->erri.tbits += n;

		if (n > v->erri.max_bits) {
			v->erri.max_bits = n;
			flag++;
		}
		if (n < v->erri.min_bits) {
			v->erri.min_bits = n;
			flag++;
		}
		if (v->erri.ebits ^ xor) {
			flag++;
		}
		v->erri.ebits |= xor;

	 	/* Calc max contig errors */
		int offset = (7 != test) ? 4 : 8;
		if ((ulong)adr == (ulong)v->erri.eadr+offset ||
				(ulong)adr == (ulong)v->erri.eadr-offset ) {
			if (len < MAX_ERRORS)
				len++;
		}
		else{
			len=1;
		}

		if (len > v->erri.maxl) {
			v->erri.maxl = len;
			flag++;
		}
		v->erri.eadr = (ulong)adr;

		if (v->erri.hdr_flag == 0) {
			clear_scroll();
			cprint(LINE_HEADER+0, 1,  "Error Confidence Value:");
			cprint(LINE_HEADER+1, 1,  "  Lowest Error Address:");
			cprint(LINE_HEADER+2, 1,  " Highest Error Address:");
			cprint(LINE_HEADER+3, 1,  "    Bits in Error Mask:");
			cprint(LINE_HEADER+4, 1,  " Bits in Error - Total:");
			cprint(LINE_HEADER+4, 29,  "Min:    Max:    Avg:");
			cprint(LINE_HEADER+5, 1,  " Max Contiguous Errors:");

			cprint(LINE_HEADER+0, 64,   "Test  Errors");
			v->erri.hdr_flag++;
		}
		if (flag) {
		  /* Calc bits in error */
		  for (i=0, n=0; i<32; i++) {
			if (v->erri.ebits>>i & 1) {
				n++;
			}
		  }
		  page = v->erri.low_addr.page;
		  offset = v->erri.low_addr.offset;
		  mb = page >> 8;
		  hprint(LINE_HEADER+1, 25, page);
		  hprint2(LINE_HEADER+1, 33, offset, 3);
		  cprint(LINE_HEADER+1, 36, " -      . MB");
		  dprint(LINE_HEADER+1, 39, mb, 5, 0);
		  dprint(LINE_HEADER+1, 45, ((page & 0xFF)*10) >> 8, 1, 0);
		  page = v->erri.high_addr.page;
		  offset = v->erri.high_addr.offset;
		  mb = page >> 8;
		  hprint(LINE_HEADER+2, 25, page);
		  hprint2(LINE_HEADER+2, 33, offset, 3);
		  cprint(LINE_HEADER+2, 36, " -      . MB");
		  dprint(LINE_HEADER+2, 39, mb, 5, 0);
		  dprint(LINE_HEADER+2, 45, ((page & 0xFF)*10) >> 8, 1, 0);
		  hprint(LINE_HEADER+3, 25, v->erri.ebits);
		  dprint(LINE_HEADER+4, 25, n, 2, 1);
		  dprint(LINE_HEADER+4, 34, v->erri.min_bits, 2, 1);
		  dprint(LINE_HEADER+4, 42, v->erri.max_bits, 2, 1);
		  dprint(LINE_HEADER+4, 50, v->erri.tbits/v->ecount, 2, 1);
		  if (v->erri.maxl < MAX_ERRORS)
			dprint(LINE_HEADER+5, 26, v->erri.maxl, 5, 1);
		  else{
			  dprint(LINE_HEADER+5, 25, v->erri.maxl, 5, 1);
			  cprint(LINE_HEADER+5, 30, "+");
		  }
		}
		if (v->erri.cor_err) {
		  dprint(LINE_HEADER+6, 25, v->erri.cor_err, 8, 1);
		}
		break;

	case PRINTMODE_ADDRESSES:
		/* Don't display duplicate errors */
		if ((ulong)adr == (ulong)v->erri.eadr &&
				 xor == v->erri.exor) {
			return;
		}
		if (v->erri.hdr_flag == 0) {
			clear_scroll();
			cprint(LINE_HEADER, 0,
"Tst  Pass   Failing Address          Good       Bad     Err-Bits  Count CPU");
			cprint(LINE_HEADER+1, 0,
"---  ----  -----------------------  --------  --------  --------  ----- ----");
			v->erri.hdr_flag++;
		}
		/* Check for keyboard input */
		check_input();
		scroll();
	
		if ( type == 2 || type == 3) {
			page = (ulong)adr;
			offset = good;
		} else {
			page = page_of(adr);
			offset = ((unsigned long)adr) & 0xFFF;
		}
		mb = page >> 8;
		dprint(v->msg_line, 0, test, 3, 0);
		dprint(v->msg_line, 4, v->pass, 5, 0);
		hprint(v->msg_line, 11, page);
		hprint2(v->msg_line, 19, offset, 3);
		cprint(v->msg_line, 22, " -      . MB");
		dprint(v->msg_line, 25, mb, 5, 0);
		dprint(v->msg_line, 31, ((page & 0xFF)*10)/0x100, 1, 0);

		if (type == 3) {
			/* ECC error */
			cprint(v->msg_line, 36, 
			  bad?"corrected           ": "uncorrected         ");
			hprint2(v->msg_line, 60, syn, 4);
			cprint(v->msg_line, 68, "ECC"); 
			dprint(v->msg_line, 74, chan, 2, 0);
		} else if (type == 2) {
			cprint(v->msg_line, 36, "Parity error detected                ");
		} else {
			hprint(v->msg_line, 36, good);
			hprint(v->msg_line, 46, bad);
			hprint(v->msg_line, 56, xor);
			dprint(v->msg_line, 66, v->ecount, 5, 0);
			dprint(v->msg_line, 74, smp_my_cpu_num(), 2,1);
			v->erri.exor = xor;
		}
		v->erri.eadr = (ulong)adr;
		break;

	case PRINTMODE_PATTERNS:
		if (v->erri.hdr_flag == 0) {
			clear_scroll();
			v->erri.hdr_flag++;
		}
		/* Do not do badram patterns from test 0 or 5 */
		if (test == 0 || test == 5) {
			return;
		}
		/* Only do patterns for data errors */
		if ( type != 0) {
			return;
		}
		/* Process the address in the pattern administration */
		patnchg=insertaddress ((ulong) adr);
		if (patnchg) { 
			printpatn();
		}
		break;

	case PRINTMODE_NONE:
		if (v->erri.hdr_flag == 0) {
			clear_scroll();
			v->erri.hdr_flag++;
		}
		break;
	}
}

/*
 * Print an ecc error
 */
void print_ecc_err(unsigned long page, unsigned long offset, 
	int corrected, unsigned short syndrome, int channel)
{
	++(v->ecc_ecount);
	syn = syndrome;
	chan = channel;
	common_err((ulong *)page, offset, corrected, 0, 2);
}

#ifdef PARITY_MEM
/*
 * Print a parity error message
 */
void parity_err( unsigned long edi, unsigned long esi) 
{
	unsigned long addr;

	if (test == 5) {
		addr = esi;
	} else {
		addr = edi;
	}
	common_err((ulong *)addr, addr & 0xFFF, 0, 0, 3);
}
#endif

/*
 * Print the pattern array as a LILO boot option addressing BadRAM support.
 */
void printpatn (void)
{
       int idx=0;
       int x;

	/* Check for keyboard input */
	check_input();

       if (v->numpatn == 0)
               return;

       scroll();

       cprint (v->msg_line, 0, "badram=");
       x=7;

       for (idx = 0; idx < v->numpatn; idx++) {

               if (x > 80-22) {
                       scroll();
                       x=7;
               }
               cprint (v->msg_line, x, "0x");
               hprint (v->msg_line, x+2,  v->patn[idx].adr );
               cprint (v->msg_line, x+10, ",0x");
               hprint (v->msg_line, x+13, v->patn[idx].mask);
               if (idx+1 < v->numpatn)
                       cprint (v->msg_line, x+21, ",");
               x+=22;
       }
}
	
/*
 * Show progress by displaying elapsed time and update bar graphs
 */
short spin_idx[MAX_CPUS];
char spin[4] = {'|','/','-','\\'};

void do_tick(int me)
{
	int i, pct, cpu;
	ulong h, l, n, t;
	extern int mstr_cpu;

	/* Get the cpu number */
	cpu = smp_ord_to_cpu(me);
	if (++spin_idx[cpu] > 3) {
		spin_idx[cpu] = 0;
	}
	cplace(8, cpu+7, spin[spin_idx[cpu]]);
	
	/* Check for keyboard input */
	if (me == mstr_cpu) {
		check_input();
	}
	/* A barrier here holds the other CPUs until the configuration
	 * changes are done */
	s_barrier();

	/* Only the first selected CPU does the update */
	if (me !=  mstr_cpu) {
		return;
	}

	/* FIXME only print serial error messages from the tick handler */
	if (v->ecount) {
		print_err_counts();
	}
	
	nticks++;
	v->total_ticks++;

	if (test_ticks) {
		pct = 100*nticks/test_ticks;
		if (pct > 100) {
			pct = 100;
		}
	} else {
		pct = 0;
	}
	dprint(2, COL_MID+4, pct, 3, 0);
	i = (BAR_SIZE * pct) / 100;
	while (i > v->tptr) {
		if (v->tptr >= BAR_SIZE) {
			break;
		}
		cprint(2, COL_MID+9+v->tptr, "#");
		v->tptr++;
	}
	
	if (v->pass_ticks) {
		pct = 100*v->total_ticks/v->pass_ticks;
		if (pct > 100) {
			pct = 100;
		}
	} else {
		pct = 0;
        }
	dprint(1, COL_MID+4, pct, 3, 0);
	i = (BAR_SIZE * pct) / 100;
	while (i > v->pptr) {
		if (v->pptr >= BAR_SIZE) {
			break;
		}
		cprint(1, COL_MID+9+v->pptr, "#");
		v->pptr++;
	}

	if (v->ecount && v->printmode == PRINTMODE_SUMMARY) {
		/* Compute confidence score */
		pct = 0;

		/* If there are no errors within 1mb of start - end addresses */
		h = v->pmap[v->msegs - 1].end - 0x100;
		if (v->erri.low_addr.page >  0x100 &&
				 v->erri.high_addr.page < h) {
			pct += 8;
		}

		/* Errors for only some tests */
		if (v->pass) {
			for (i=0, n=0; tseq[i].msg != NULL; i++) {
				if (tseq[i].errors == 0) {
					n++;
				}
			}
			pct += n*3;
		} else {
			for (i=0, n=0; i<test; i++) {
				if (tseq[i].errors == 0) {
					n++;
				}
			}
			pct += n*2;
			
		}

		/* Only some bits in error */
		n = 0;
		if (v->erri.ebits & 0xf) n++;
		if (v->erri.ebits & 0xf0) n++;
		if (v->erri.ebits & 0xf00) n++;
		if (v->erri.ebits & 0xf000) n++;
		if (v->erri.ebits & 0xf0000) n++;
		if (v->erri.ebits & 0xf00000) n++;
		if (v->erri.ebits & 0xf000000) n++;
		if (v->erri.ebits & 0xf0000000) n++;
		pct += (8-n)*2;

		/* Adjust the score */
		pct = pct*100/22;
/*
		if (pct > 100) {
			pct = 100;
		}
*/
		dprint(LINE_HEADER+0, 25, pct, 3, 1);
	}
		

	/* We can't do the elapsed time unless the rdtsc instruction
	 * is supported
	 */
	if (cpu_id.fid.bits.rdtsc) {
		asm __volatile__(
			"rdtsc":"=a" (l),"=d" (h));
		asm __volatile__ (
			"subl %2,%0\n\t"
			"sbbl %3,%1"
			:"=a" (l), "=d" (h)
			:"g" (v->startl), "g" (v->starth),
			"0" (l), "1" (h));
		t = h * ((unsigned)0xffffffff / v->clks_msec) / 1000;
		t += (l / v->clks_msec) / 1000;
		i = t % 60;
		dprint(LINE_INFO, COL_INF1-2, i%10, 1, 0);
		dprint(LINE_INFO, COL_INF1-3, i/10, 1, 0);
		t /= 60;
		i = t % 60;
		dprint(LINE_INFO, COL_INF1-5, i % 10, 1, 0);
		dprint(LINE_INFO, COL_INF1-6, i / 10, 1, 0);
		t /= 60;
		dprint(LINE_INFO, COL_INF1-11, t, 4, 0);
	}


	/* Poll for ECC errors */
/*
	poll_errors();
*/
}

