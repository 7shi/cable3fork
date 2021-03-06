#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <SDL.h>

#define POKE(d,opr,s) (dst=oprsz?*(uint16_t*)&d:d,val=oprsz?*(uint16_t*)&d opr(src=*(uint16_t*)&s):(d opr(src=*(uint8_t*)&s)))

#define ROMBASE 0xf0000
uint8_t mem[0x200000 /* 2MB */ ], ioport[0x10000];
uint8_t *const r8 = &mem[ROMBASE];
uint16_t *const r = (uint16_t *) &mem[ROMBASE];
uint32_t *const table = (uint32_t *) &mem[ROMBASE + 0x103];

uint8_t optype, oprsz;
uint16_t ip, src, dst, val;
int hassegpfx, segpfx;

#define AL r8[0]
#define AH r8[1]
#define CL r8[2]
#define CH r8[3]
#define DL r8[4]
#define DH r8[5]
#define BL r8[6]
#define BH r8[7]

#define AX r[0]
#define CX r[1]
#define DX r[2]
#define BX r[3]
#define SP r[4]
#define BP r[5]
#define SI r[6]
#define DI r[7]

#define ES r[8]
#define CS r[9]
#define SS r[10]
#define DS r[11]

#define CF r8[40]
#define PF r8[41]
#define AF r8[42]
#define ZF r8[43]
#define SF r8[44]
#define TF r8[45]
#define IF r8[46]
#define DF r8[47]
#define OF r8[48]

void
push(uint16_t src)
{
	oprsz = 4;
	*(uint16_t *) &mem[16 * SS + (SP -= 2)] = src;
}

uint16_t
pop(void)
{
	oprsz = 4;
	uint16_t ret = *(uint16_t *) &mem[16 * SS + SP];
	SP += 2;
	return ret;
}

int
regmap(int reg)
{
	return ROMBASE + (oprsz ? 2 * reg : (2 * reg + reg / 4) & 7);
}

int
lookup(int no, int offset)
{
	return mem[table[no] + offset];
}

int
isneg(int v)
{
	return (oprsz ? (int16_t) v : (int8_t) v) < 0;
}

void
setafof(void)
{
	AF = !!((src ^= dst ^ val) & 16);
	OF = (val - dst && 1 & (CF ^ src >> 8 * -~oprsz - 1));
}

uint16_t
getflags(void)
{
	uint16_t flags = 0xf002;
	for (int i = 0; i < 9; ++i)
		flags += r8[40 + i] << lookup(25, i);
	return flags;
}

void
setflags(uint16_t flags)
{
	for (int i = 0; i < 9; ++i)
		r8[40 + i] = !!(flags & (1 << lookup(25, i)));
}

void
intr(int n)
{
	optype = 76;
	push(getflags());
	push(CS);
	push(ip);
	POKE(CS, =, mem[4 * n + 2]);
	POKE(ip, =, mem[4 * n]);
	IF = 0;
}

uint32_t
modrm(int mode, int rm, int16_t disp)
{
	if (mode == 3)
		return regmap(rm);
	int tno = 4 * !mode;
	int seg = r[hassegpfx ? segpfx : lookup(tno + 3, rm)];
	int r1 = r[lookup(tno + 1, rm)];
	int hasdisp = lookup(tno + 2, rm);
	int r2 = r[lookup(tno, rm)];
	return 16 * seg + (uint16_t) (r1 + hasdisp * disp + r2);
}

void
getoprs(int dir, int reg, uint32_t addr, uint32_t *opr1, uint32_t *opr2)
{
	if (!dir)
		*opr1 = addr, *opr2 = regmap(reg);
	else
		*opr1 = regmap(reg), *opr2 = addr;
}

int
main(int argc, char *argv[])
{
	CS = ROMBASE >> 4, ip = 0x100;

	FILE *files[] = {NULL /* HD */ , NULL /* FD */ , NULL /* BIOS */ };
	for (int i = 1; i <= 3 && i < argc; ++i)
		files[3 - i] = fopen(argv[i], "r+b");
	if (files[0])		/* CX:AX = HDD sectors */
		*(uint32_t *) r = fseek(files[0], 0, SEEK_END) >> 9;
	fread(&mem[ROMBASE + ip], 1, ROMBASE, files[2]);	/* read BIOS */

	int hasrep = 0, rep = 0, kb = 0;
	uint16_t counter = 0;
	SDL_Surface *surface = NULL;

	for (;;) {
		if (!hassegpfx && !hasrep && kb && IF) {
			intr(8);
#ifdef _WIN32
			if (kb = kbhit()) {
				mem[0x4a6] = getch();
				intr(7);
			}
#else
			if (kb = read(fileno(stdin), &mem[0x4a6], 1))
				intr(7);
#endif
		}
		if (CS == 0 && ip == 0)
			break;
		ioport[32] = 0;
		--ioport[64];
		uint8_t *p = &mem[16 * CS + ip], b = *p;
		oprsz = b & 1;
		int dir = b / 2 & 1;
		int rm = p[1] & 7, reg = p[1] / 8 & 7;
		int mode = p[1] >> 6;
		int16_t disp = mode != 1 ? *(int16_t *) &p[2] : (int8_t) p[2];
		int opr;
		if (!(mode == 0 && rm == 6) && mode != 2) {
			if (mode != 1)
				opr = disp;
			else
				opr = *(int16_t *) &p[3];
		} else
			opr = *(int16_t *) &p[4];
		if (hassegpfx)
			hassegpfx--;
		if (hasrep)
			hasrep--;
		uint32_t addr = modrm(mode, rm, disp), opr1, opr2;
		getoprs(dir, reg, addr, &opr1, &opr2);
		optype = lookup(51, b);
		uint8_t oprtype = lookup(14, optype);
		switch (lookup(8, optype)) {
			int tmp, tmp2;
			uint32_t utmp;
		case 0:	/* conditional jump, enter?, leave?, int1? */
			tmp = b / 2 & 7;
			ip += (int8_t) p[1] * (oprsz ^ (r8[lookup(21, tmp)] | r8[lookup(22, tmp)] | r8[lookup(23, tmp)] ^ r8[lookup(24, tmp)]));
			break;
		case 1:	/* mov */
			oprsz = b & 8;
			tmp = regmap(b & 7);
			POKE(mem[tmp], =, p[1]);
			break;
		case 2:	/* inc, dec */
			/* oprtype = 0, 1 */
			oprsz = 2, reg = oprtype;
			opr1 = addr = modrm(mode, rm, disp), opr2 = regmap(b & 7);
		case 5:	/* inc, dec, call, callf, jmp, jmpf, push */
			if (reg < 2) {
				POKE(mem[opr2], +=1 - 2 * reg +, mem[ROMBASE + 24]);
				src = 1;
				setafof();
				OF = (dst + 1 - reg == 1 << 8 * -~oprsz - 1);
				optype = optype & 4 ? 19 : 57;
			} else if (reg != 6) {
				ip += (mode % 3 + 2 * !(!mode * rm - 6)) + 2;
				if (reg == 3)
					push(CS);
				if (reg & 2)
					push(ip);
				if (reg & 1)
					POKE(mem[ROMBASE + 18], =, mem[opr2 + 2]);
				POKE(ip, =, mem[opr2]);
				optype = 67;
			} else
				push(*(uint16_t *) &mem[addr]);
			break;
		case 3:	/* push */
			push(r[b & 7]);
			break;
		case 4:	/* pop */
			r[b & 7] = pop();
			break;
		case 6:	/* test, not, neg, mul, imul, div, idiv */
			opr1 = opr2;
			switch (reg) {
			case 0:/* test */
				optype = 21, ip -= ~oprsz;
				POKE(mem[opr1], &, opr);
				break;
			case 2:/* not */
				POKE(mem[opr1], = ~, mem[opr2]);
				break;
			case 3:/* neg */
				POKE(mem[opr1], = -, mem[opr2]);
				dst = 0, optype = 22;
				CF = val > dst;
				break;
			case 4:/* mul */
				optype = 19;
				if (oprsz) {
					AX = val = *(uint16_t *) &mem[addr] * (uint16_t) *r;
					DX = val >> 16;
					OF = CF = !!(val - (uint16_t) val);
				} else {
					AX = val = *(uint8_t *) &mem[addr] * (uint8_t) *r8;
					OF = CF = !!(val - (uint8_t) val);
				}
				break;
			case 5:/* imul */
				optype = 19;
				if (oprsz) {
					AX = val = *(int16_t *) &mem[addr] * (int16_t) *r;
					DX = val >> 16;
					OF = CF = !!(val - (int16_t) val);
				} else {
					AX = val = *(int8_t *) &mem[addr] * (int8_t) *r8;
					OF = CF = !!(val - (int8_t) val);
				}
				break;
			case 6:/* div */
				if (oprsz) {
					if (tmp2 = *(uint16_t *) &mem[addr]) {
						utmp = (uint32_t) (tmp = (DX << 16) + AX) / tmp2;
						if (!(utmp - (uint16_t) utmp))
							DX = tmp - tmp2 * (AX = utmp);
						else
							intr(0);
					}
				} else {
					if (tmp2 = *(uint8_t *) &mem[addr]) {
						utmp = (uint16_t) (tmp = (AH << 16) + AX) / tmp2;
						if (!(utmp - (uint8_t) utmp))
							AH = tmp - tmp2 * (AL = utmp);
						else
							intr(0);
					}
				}
				break;
			case 7:/* idiv */
				if (oprsz) {
					if (tmp2 = *(int16_t *) &mem[addr]) {
						utmp = (int) (tmp = (DX << 16) + AX) / tmp2;
						if (!(utmp - (int16_t) utmp))
							DX = tmp - tmp2 * (AX = utmp);
						else
							intr(0);
					}
				} else {
					if (tmp2 = *(int8_t *) &mem[addr]) {
						utmp = (int16_t) (tmp = (AH << 16) + AX) / tmp2;
						if (!(utmp - (int8_t) utmp))
							AH = tmp - tmp2 * (AL = utmp);
						else
							intr(0);
					}
				}
				break;
			}
			break;
		case 7:	/* add, or, adc, sbb, and, sub, xor, cmp */
			/* oprtype = 0, 1, 2, 3, 4, 5, 6, 7 */
			addr = ROMBASE, opr = *(int16_t *) &p[1], mode = 3, reg = oprtype, ip--;
		case 8:	/* add, or, adc, sbb, and, sub, xor, cmp */
			opr1 = addr, opr2 = ((uint8_t *) &r[13]) - mem;
			r[13] = (dir |= !oprsz) ? (int8_t) opr : opr;
			ip -= ~!dir, optype = 17 + (oprtype = reg);
		case 9:	/* add, or, adc, sbb, and, sub, xor, cmp, mov */
			/* oprtype = 0, 1, 2, 3, 4, 5, 6, 7, 8 */
			switch (oprtype) {
			case 0:/* add */
				POKE(mem[opr1], +=, mem[opr2]);
				CF = val < dst;
				break;
			case 1:/* or */
				POKE(mem[opr1], |=, mem[opr2]);
				break;
			case 2:/* adc */
				POKE(mem[opr1], +=CF +, mem[opr2]);
				CF = !!(CF & val == dst | +val < +(int) dst);
				setafof();
				break;
			case 3:/* sbb */
				POKE(mem[opr1], -=CF +, mem[opr2]);
				CF = !!(CF & val == dst | -val < -(int) dst);
				setafof();
				break;
			case 4:/* and */
				POKE(mem[opr1], &=, mem[opr2]);
				break;
			case 5:/* sub */
				POKE(mem[opr1], -=, mem[opr2]);
				CF = val > dst;
				break;
			case 6:/* xor */
				POKE(mem[opr1], ^=, mem[opr2]);
				break;
			case 7:/* cmp */
				POKE(mem[opr1], -, mem[opr2]);
				CF = val > dst;
				break;
			case 8:/* mov */
				POKE(mem[opr1], =, mem[opr2]);
				break;
			}
			break;
		case 10:	/* mov, lea, pop */
			if (!oprsz) {
				oprsz = reg + 8;
				getoprs(dir, oprsz, modrm(mode, rm, disp), &opr1, &opr2);
				POKE(mem[opr1], =, mem[opr2]);
			} else if (!dir) {
				hassegpfx = 1, segpfx = 12, addr = modrm(mode, rm, disp);
				POKE(mem[opr2], =, addr);
			} else
				*(uint16_t *) &mem[addr] = pop();
			break;
		case 11:	/* mov */
			getoprs(dir, 0, modrm(0, 6, *(uint16_t *) &p[1]), &opr1, &opr2);
			POKE(mem[opr2], =, mem[opr1]);
			break;
		case 12:	/* rcl, rol, ror, rcl, rcr, shl, sal, shr, sar */
			utmp = isneg(*(int16_t *) &mem[addr]);
			/* oprtype = 0, 1 */
			if (tmp = oprtype ? ++ip, (int8_t) disp : dir ? 31 & CL : 1) {
				if (reg < 4) {
					tmp %= reg / 2 + 8 * -~oprsz;
					POKE(utmp, =, mem[addr]);
				}
				if (reg & 1)
					POKE(mem[addr], >>=, tmp);
				else
					POKE(mem[addr], <<=, tmp);
				if (reg > 3)
					optype = 19;
				if (reg >= 5)
					CF = dst >> tmp - 1 & 1;
			}
			switch (reg) {
			case 0:/* rol */
				POKE(mem[addr], +=, utmp >> 8 * -~oprsz - tmp);
				OF = isneg(*(int16_t *) &val) ^ (CF = val & 1);
				break;
			case 1:/* ror */
				utmp &= (1 << tmp) - 1;
				POKE(mem[addr], +=, utmp << 8 * -~oprsz - tmp);
				CF = isneg(*(int16_t *) &val);
				OF = isneg(*(int16_t *) &val * 2) ^ CF;
				break;
			case 2:/* rcl */
				POKE(mem[addr], +=(CF << tmp - 1) +, utmp >> 1 + 8 * -~oprsz - tmp);
				CF = !!(utmp & 1 << 8 * -~oprsz - tmp);
				OF = isneg(*(int16_t *) &val) ^ CF;
				break;
			case 3:/* rcr */
				POKE(mem[addr], +=(CF << 8 * -~oprsz - tmp) +, utmp << 1 + 8 * -~oprsz - tmp);
				CF = !!(utmp & 1 << tmp - 1);
				OF = isneg(*(int16_t *) &val) ^ isneg(*(int16_t *) &val * 2);
				break;
			case 4:/* shl */
				CF = isneg(*(int16_t *) &dst << tmp - 1);
				OF = isneg(*(int16_t *) &val) ^ CF;
				break;
			case 5:/* shr */
				OF = isneg(*(int16_t *) &dst);
				break;
			case 7:/* sar */
				if (tmp >= 8 * -~oprsz)
					CF = !!utmp;
				OF = 0;
				POKE(mem[addr], +=, utmp *= ~((1 << 8 * -~oprsz) - 1 >> tmp));
				break;
			}
			break;
		case 13:	/* loopnz, loopne, loopz, loope, loop, jcxz */
			tmp = !!--CX;
			switch (b & 7) {
			case 0:/* loopnz, loopne */
				tmp &= !ZF;
				break;
			case 1:/* loopz, loope */
				tmp &= ZF;
				break;
			case 3:/* jcxz */
				tmp = !++CX;
				break;
			}
			ip += tmp * (int8_t) p[1];
			break;
		case 14:	/* call, jmp, jmpf */
			ip += 3 - dir;
			if (!oprsz) {
				if (dir)
					CS = *(uint16_t *) &p[3], ip = 0;
				else
					push(ip);
			}
			ip += dir * oprsz ? (int8_t) p[1] : *(int16_t *) &p[1];
			break;
		case 15:	/* test */
			POKE(mem[opr2], &, mem[opr1]);
			break;
		case 16:	/* xchg */
			oprsz = 7, opr1 = ROMBASE, opr2 = regmap(b & 7);
		case 24:	/* xchg */
			if (opr1 != opr2) {
				POKE(mem[opr1], ^=, mem[opr2]);
				POKE(mem[opr2], ^=, mem[opr1]);
				POKE(mem[opr1], ^=, mem[opr2]);
			}
			break;
		case 17:	/* movsb, movsw, stosb, stosw, lodsb, lodsw */
			if (!hasrep || CX) {
				/* oprtype = 0, 1, 2 */
				opr1 = oprtype < 2 ? 16 * ES + DI : ROMBASE;
				opr2 = oprtype & 1 ? ROMBASE : 16 * r[hassegpfx ? segpfx : 11] + SI;
				POKE(mem[opr1], =, mem[opr2]);
				tmp = ~(-2 * DF) * ~oprsz;
				if (!(oprtype & 1))
					SI += tmp;
				if (!(oprtype & 2))
					DI += tmp;
				if (hasrep && --CX) {
					hasrep++;
					if (hassegpfx)
						hassegpfx++;
					ip--;
				}
			}
			break;
		case 18:	/* cmpsb, cmpsw, scasb, scasw */
			if (!hasrep || CX) {
				/* oprtype = 0, 1 */
				opr1 = oprtype ? ROMBASE : 16 * r[hassegpfx ? segpfx : 11] + SI;
				opr2 = 16 * ES + DI;
				POKE(mem[opr1], -, mem[opr2]);
				optype = 92;
				ZF = !val;
				CF = val > dst;
				tmp = ~(-2 * DF) * ~oprsz;
				if (!oprtype)
					SI += tmp;
				DI += tmp;
				if (hasrep && --CX && !val == rep) {
					hasrep++;
					if (hassegpfx)
						hassegpfx++;
					ip--;
				}
			}
			break;
		case 19:	/* ret, retf, iret */
			dir = oprsz;
			ip = pop();
			/* oprtype = 0, 1, 2 */
			if (oprtype)
				CS = pop();
			if (oprtype & 2)
				setflags(pop());
			else if (!dir)
				SP += *(int16_t *) &p[1];
			break;
		case 20:	/* mov */
			POKE(mem[opr2], =, opr);
			break;
		case 21:	/* in */
			ioport[0x3da] ^= 9;
			/* oprtype = 0, 1 */
			POKE(AL, =, ioport[oprtype ? DX : (int8_t) p[1]]);
			break;
		case 22:	/* out */
			/* oprtype = 0, 1 */
			POKE(ioport[oprtype ? DX : (int8_t) p[1]], =, AL);
			break;
		case 23:	/* repnz, repz */
			hasrep = 2, rep = oprsz;
			if (hassegpfx)
				hassegpfx++;
			break;
		case 25:	/* push */
			/* oprtype = 8: ES, 9: CS, 10: SS, 11: DS */
			push(r[oprtype]);
			break;
		case 26:	/* pop */
			/* oprtype = 8: ES, 10: SS, 11: DS */
			r[oprtype] = pop();
			break;
		case 27:	/* es:, cs:, ss:, ds: */
			/* oprtype = 8: ES, 9: CS, 10: SS, 11: DS */
			hassegpfx = 2, segpfx = oprtype;
			if (hasrep)
				hasrep++;
			break;
		case 28:	/* daa, das */
			oprsz = 0;
			/* oprtype = 27, 39 */
			CF = !!lookup(oprtype += 3 * AF + 6 * CF, AL);
			AF = !!(lookup(1 + oprtype, AL));
			val = AL = lookup(oprtype - 1, AL);
			break;
		case 29:	/* aaa, aas */
			/* oprtype = 0, 2 */
			AX += 262 * (oprtype - 1) * (AF = CF = !!((AL & 15) > 9 | AF));
			val = AL &= 15;
			break;
		case 30:	/* cbw */
			AH = -isneg(AL);
			break;
		case 31:	/* cwd */
			DX = -isneg(AX);
			break;
		case 32:	/* callf */
			push(CS);
			push(ip + 5);
			CS = *(uint16_t *) &p[3], ip = *(uint16_t *) &p[1];
			break;
		case 33:	/* pushf */
			push(getflags());
			break;
		case 34:	/* popf */
			setflags(pop());
			break;
		case 35:	/* sahf */
			setflags((getflags() & ~255) + AH);
			break;
		case 36:	/* lahf */
			AH = getflags();
			break;
		case 37:	/* les, lds */
			oprsz = 1;
			opr1 = regmap(reg), opr2 = modrm(mode, rm, disp);
			POKE(mem[opr1], =, mem[opr2]);
			/* oprtype = 16, 22 */
			POKE(mem[ROMBASE + oprtype], =, mem[opr2 + 2]);
			break;
		case 38:	/* int3 */
			++ip;
			intr(3);
			break;
		case 39:	/* int */
			ip += 2;
			intr(p[1]);
			break;
		case 40:	/* into */
			++ip;
			if (OF)
				intr(4);
			break;
		case 41:	/* aam */
			if (p[1]) {
				AH = AL / p[1];
				val = AL %= p[1];
			} else
				intr(0);
			break;
		case 42:	/* aad */
			oprsz = 0, AX = val = AL + (*(int16_t *) &p[1]) * AH;
			break;
		case 43:	/* salc */
			AL = -CF;
			break;
		case 44:	/* xlat */
			AL = mem[16 * r[hassegpfx ? segpfx : 11] + (uint16_t) (AL + BX)];
			break;
		case 45:	/* cmc */
			CF ^= 1;
			break;
		case 46:	/* clc, stc, cli, sti, cld, std */
			/* oprtype = 80-81: CF, 92-93: IF, 94-95: DF */
			r8[oprtype / 2] = oprtype & 1;
			break;
		case 47:	/* test */
			POKE(AL, &, p[1]);
			break;
		case 48:	/* hyper call */
			switch (p[1]) {
				time_t t;
#ifdef _WIN32
				static int skipcnt;
#endif
			case 0:/* output char */
#ifdef _WIN32
				if (AL == 27)
					skipcnt = 6;
				if (skipcnt && skipcnt--)
					break;
#endif
				write(fileno(stdout), &AL, 1);
				break;
			case 1:/* get time */
				time(&t);
				memcpy(&mem[16 * ES + BX], localtime(&t), 36);
				break;
			case 2:/* read disk */
				if (fseek(files[DL], (*(uint32_t *) &BP) << 9, SEEK_SET) != -1)
					AL = fread(&mem[16 * ES + BX], 1, AX, files[DL]);
				else
					AL = 0;
				break;
			case 3:/* write disk */
				if (fseek(files[DL], (*(uint32_t *) &BP) << 9, SEEK_SET) != -1)
					AL = fwrite(&mem[16 * ES + BX], 1, AX, files[DL]);
				else
					AL = 0;
				break;
			}
			break;
		}
		if (lookup(16, optype))
			setafof();
		else if (lookup(17, optype))
			OF = CF = 0;
		ip += (mode % 3 + 2 * !(!mode * rm - 6)) * lookup(20, optype) + lookup(18, optype) - lookup(19, optype) * ~!!oprsz;
		if (lookup(15, optype)) {
			SF = isneg(*(int16_t *) &val);
			ZF = !val;
			PF = lookup(50, (uint8_t) val);
		}
		if (!++counter) {
			kb = 1;
			if (ioport[0]) {
				/* Hercules Graphics Card (HGC) */
				SDL_PumpEvents();
				if (!surface)
					surface = SDL_SetVideoMode(720, 348, 32, 0);
				uint32_t *pix = (uint32_t *) surface->pixels;
				uint8_t *fb = &mem[0xb0000 + ioport[0x3b8] / 128 * 0x8000];
				for (int y = 0, i = 0; y < 348; ++y)
					for (int x = 0; x < 720; ++x, ++i)
						pix[i] = -!!((1 << (7 - (x & 7))) & fb[x / 8 + y / 4 * 90 + ((y & 3) << 13)]);
				SDL_Flip(surface);
			} else if (surface) {
				SDL_Quit();
				surface = NULL;
			}
		}
	}
}
