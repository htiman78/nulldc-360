#include "ppc_intregalloc.h"

/*
//implement register allocators on a class , so we can swap em around?
//methods needed
//
//DoAllocation		: do allocation on the block
//BeforeEmit		: generate any code needed before the main emittion begins (other register allocators may have emited code tho)
//BeforeTrail		: generate any code needed after the main emittion has ended (other register allocators may emit code after that tho)
//AfterTrail		: generate code after the native block end (after the ret) , can be used to emit helper functions (other register allocators may emit code after that tho)
//IsRegAllocated	: *couh* yea .. :P
//GetRegister		: Get the register , needs flag if it's read or write. Carefull w/ register state , we may need to implement state push/pop
//PushRegister		: push register to stack (if allocated)
//PopRegister		: pop register from stack (if allocated)
//FlushRegister		: write reg to reg location , and dealloc it
//WriteBackRegister	: write reg to reg location
//ReloadRegister	: read reg from reg location , discard old result

*/

u32 ssenoalloc=0;
u32 ssealloc=0;
ppc_gpr_reg reg_to_alloc[16]=
{
//	R5,R6,R7,R8,R9,R10,R11,R12
	R16,R17,R18,R19,R20,R21,R22,R23,R24,R25,R26,R27,R28,R29,R30,R31
};

#define REG_ALLOC_COUNT 16

//////////////////////////////////////////////////
// new reg alloc class							//
//////////////////////////////////////////////////
class SimpleGPRAlloc : public IntegerRegAllocator
{

	ppc_block* ppce;
	//helpers & misc shit
	struct RegAllocInfo
	{
		ppc_gpr_reg ppcreg;
		bool InReg;
		bool Dirty;
	};
	RegAllocInfo r_alloced[16];
	struct sort_temp
	{
		int cnt;
		int reg;
		bool no_load;
	};

	//ebx, ebp, esi, and edi are preserved

	//Yay bubble sort
	void bubble_sort(sort_temp numbers[] , int array_size)
	{
		int i, j;
		sort_temp temp;
		for (i = (array_size - 1); i >= 0; i--)
		{
			for (j = 1; j <= i; j++)
			{
				if (numbers[j-1].cnt < numbers[j].cnt)
				{
					temp = numbers[j-1];
					numbers[j-1] = numbers[j];
					numbers[j] = temp;
				}
			}
		}
	}


	//check
	void checkvr(u32 reg)
	{
		//if (reg>=fr_0 && reg<=fr_15 )
		//	__asm int 3;
	//	if (reg>=xf_0 && reg<=xf_15 )
		//	__asm int 3; 
	}
	//Helper , olny internaly used now
	virtual void MarkDirty(u32 reg)
	{
		checkvr(reg);
		if (IsRegAllocated(reg))
		{
			r_alloced[reg].Dirty=true;
		}
	}
	//reg alloc interface :)
	//DoAllocation		: do allocation on the block
	virtual void DoAllocation(BasicBlock* block,ppc_block* ppce)
	{
		this->ppce=ppce;
		sort_temp used[16];
		for (int i=0;i<16;i++)
		{
			used[i].cnt=0;
			used[i].reg=r0+i;
			used[i].no_load=false;
			r_alloced[i].ppcreg=ERROR_REG;
			r_alloced[i].InReg=false;
			r_alloced[i].Dirty=false;
		}

		u32 op_count=block->ilst.op_count;
		shil_opcode* curop;

		for (u32 j=0;j<op_count;j++)
		{
			curop=&block->ilst.opcodes[j];
			for (int i = 0;i<16;i++)
			{
				Sh4RegType reg=(Sh4RegType)(r0+i);
				if ((curop->WritesReg(reg)==true) && (curop->ReadsReg(reg)==false) && (used[i].cnt==0))
				{
					used[i].no_load=true;
				}
				//both reads and writes , give it one more ;P
				if ( curop->UpdatesReg((Sh4RegType) (r0+i)) )
					used[i].cnt+=12;	//3 +rw (9)
				else if (curop->ReadsReg((Sh4RegType) (r0+i)))
					used[i].cnt+=6;		//3 +r (3)
				else if (curop->WritesReg((Sh4RegType) (r0+i)))
					used[i].cnt+=9;		//3 +w (6)
			}
		}

		bubble_sort(used,16);

		for (u32 i=0;i<REG_ALLOC_COUNT;i++)
		{
			if (used[i].cnt<14)	//3+3+3+6
				break;
			r_alloced[used[i].reg].ppcreg=reg_to_alloc[i];
			if (used[i].no_load)
			{
				r_alloced[used[i].reg].InReg=true;
				r_alloced[used[i].reg].Dirty=false;
			}
		}
	}
	//BeforeEmit		: generate any code needed before the main emittion begins (other register allocators may have emited code tho)
	virtual void BeforeEmit()
	{
		for (int i=0;i<16;i++)
		{
			if (IsRegAllocated(i))
			{
				GetRegister(R3,i,RA_DEFAULT);
			}
		}
	}
	//BeforeTrail		: generate any code needed after the main emittion has ended (other register allocators may emit code after that tho)
	virtual void BeforeTrail()
	{
		FlushRegCache();
	}
	//AfterTrail		: generate code after the native block end (after the ret) , can be used to emit helper functions (other register allocators may emit code after that tho)
	virtual void AfterTrail()
	{
	}
	//IsRegAllocated	: *couh* yea .. :P
	virtual bool IsRegAllocated(u32 sh4_reg)
	{
		checkvr(sh4_reg);
		if (sh4_reg<=r15)
			return r_alloced[sh4_reg].ppcreg!=ERROR_REG;
		else 
			return false;
	}
	//Carefull w/ register state , we may need to implement state push/pop
	//GetRegister		: Get the register , needs flag for mode
	virtual ppc_gpr_reg GetRegister(ppc_gpr_reg d_reg,u32 sh4_reg,u32 mode)
	{
		checkvr(sh4_reg);
		//No move , or RtR move + reload
		if (IsRegAllocated(sh4_reg))
		{
			if ( r_alloced[sh4_reg].InReg==false)
			{
				if ((mode & RA_NODATA)==0)
				{
					//if we must load , and its not loaded
					ppce->emitLoad32(r_alloced[sh4_reg].ppcreg,GetRegPtr(sh4_reg));
					
					r_alloced[sh4_reg].Dirty=false;//not dirty .ffs, we just loaded it....
				}
				else
				{
					r_alloced[sh4_reg].Dirty=true;//its dirty, we dint load data :)
				}
			}
			
			r_alloced[sh4_reg].InReg=true;

			//nada
			if (mode & RA_FORCE)
			{
				//move to forced reg , if needed
				if ((r_alloced[sh4_reg].ppcreg!=d_reg) && ((mode & RA_NODATA)==0))
					ppce->emitMoveRegister(d_reg,r_alloced[sh4_reg].ppcreg);
				return d_reg;
			}
			else
			{
				//return allocated reg
				return r_alloced[sh4_reg].ppcreg;
			}
		}
		else
		{
			//MtoR move , force has no effect (allways forced) ;)
			if (0==(mode & RA_NODATA))
			{
				ppce->emitLoad32(d_reg,GetRegPtr(sh4_reg));
			}
			return d_reg;
		}

		//yes it realy can't come here
//		__asm int 3;
		//return EAX;
	}
	//Save registers
	virtual void SaveRegister(u32 reg,ppc_gpr_reg from)
	{
		checkvr(reg);
		if (!IsRegAllocated(reg))
		{
			ppce->emitStore32(GetRegPtr(reg),from);
		}
		else
		{
			ppc_gpr_reg ppcreg=GetRegister(R3,reg,RA_NODATA);
			if (ppcreg!=from)
				ppce->emitMoveRegister(ppcreg,from);
		}
		MarkDirty(reg);
	}
	virtual void SaveRegister(u32 reg,u32 from)
	{
		checkvr(reg);
		if (!IsRegAllocated(reg))
		{
			ppce->emitLoadImmediate32(R3,from);
			ppce->emitStore32(GetRegPtr(reg),R3);
		}
		else
		{
			ppc_gpr_reg ppcreg=GetRegister(R3,reg,RA_NODATA);
			ppce->emitLoadImmediate32(ppcreg,from);
		}
		MarkDirty(reg);
	}
	virtual void SaveRegister(u32 reg,u32* from)
	{
		checkvr(reg);
		if (!IsRegAllocated(reg))
		{
			ppce->emitLoad32(R4,from);
			ppce->emitStore32(GetRegPtr(reg),R4);
		}
		else
		{
			ppc_gpr_reg ppcreg=GetRegister(R3,reg,RA_NODATA);
			ppce->emitLoad32(ppcreg,from);
		}
		MarkDirty(reg);
	}
	virtual void SaveRegister(u32 reg,s16* from)
	{
		checkvr(reg);
		if (!IsRegAllocated(reg))
		{
			ppce->emitLoad16(R4,from);
			ppce->emitStore16(GetRegPtr(reg),R4);
		}
		else
		{
			ppc_gpr_reg ppcreg=GetRegister(R3,reg,RA_NODATA);
			ppce->emitLoad16(ppcreg,from);
		}
		MarkDirty(reg);
	}
	virtual void SaveRegister(u32 reg,s8* from)
	{
		checkvr(reg);
		if (!IsRegAllocated(reg))
		{
			ppce->emitLoad8(R4,from);
			ppce->emitStore8(GetRegPtr(reg),R4);
		}
		else
		{
			ppc_gpr_reg ppcreg=GetRegister(R3,reg,RA_NODATA);
			ppce->emitLoad8(ppcreg,from);
		}
		MarkDirty(reg);
	}

	//FlushRegister		: write reg to reg location , and dealloc it
	virtual void FlushRegister(u32 reg)
	{
		checkvr(reg);
		if (IsRegAllocated(reg) && r_alloced[reg].InReg)
		{
			if (r_alloced[reg].Dirty)
			{
				ppce->emitStore32(GetRegPtr(reg),r_alloced[reg].ppcreg);
			}
			r_alloced[reg].InReg=false;
			r_alloced[reg].Dirty=false;
		}
	}
	//Flush all regs
	virtual void FlushRegCache()
	{
		for (int i=0;i<16;i++)
		{
			FlushRegister(i);
		}
	}
	//WriteBackRegister	: write reg to reg location
	virtual void WriteBackRegister(u32 reg)
	{
		checkvr(reg);
		if (IsRegAllocated(reg) && r_alloced[reg].InReg && r_alloced[reg].Dirty)
		{
			r_alloced[reg].Dirty=false;
			ppce->emitStore32(GetRegPtr(reg),r_alloced[reg].ppcreg);
		}
	}
	//ReloadRegister	: read reg from reg location , discard old result
	virtual void ReloadRegister(u32 reg)
	{
		checkvr(reg);
		if (IsRegAllocated(reg) && r_alloced[reg].InReg)
		{
			//r_alloced[reg].Dirty=false;
			//ppce->Emit(op_mov32,r_alloced[reg].ppcreg,GetRegPtr(reg));
			r_alloced[reg].InReg=false;
		}
	}
};

IntegerRegAllocator* GetGPRtAllocator()
{
	return new SimpleGPRAlloc();
}